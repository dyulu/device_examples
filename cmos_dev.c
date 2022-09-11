/****************************************************************************************************************
 * Linux device driver programming: https://www.youtube.com/playlist?list=PLlrqp8hxLfoqSIQFrGbAM5lv5uAnZBB61
 *
 * cat /proc/ioports
 *
 * Documentation/driver-api/driver-model/platform.rst
 * include/linux/platform_device.h
 *
 * Platform devices are given a name, used in driver binding, and a list of resources such as addresses and IRQs.
 *
 * struct platform_device {
 *      const char      *name;
 *      u32             id;
 *      struct device   dev;
 *      u32             num_resources;
 *      struct resource *resource;
 *      ......
 * };
 *
 * struct platform_driver {
 *      int (*probe)(struct platform_device *);
 *      int (*remove)(struct platform_device *);
 *      void (*shutdown)(struct platform_device *);
 *      int (*suspend)(struct platform_device *, pm_message_t state);
 *      int (*resume)(struct platform_device *);
 *      struct device_driver driver;
 *      const struct platform_device_id *id_table;
 *      bool prevent_deferred_probe;
 * };
 *
 * Life cycle of a device driver:
 *     init (leading to registration of the driver)
 *     probe
 *     remove
 *     exit (if module is removed)
 * include/linux/ioport.h:
 * struct resource *__devm_request_region(struct device *dev, struct resource *parent,
 *                                        resource_size_t start, resource_size_t n, const char *name)
 *
 * #define devm_request_region(dev,start,n,name) \
 *         __devm_request_region(dev, &ioport_resource, (start), (n), (name))
 *
 * #define devm_request_mem_region(dev,start,n,name) \
 *         __devm_request_region(dev, &iomem_resource, (start), (n), (name))
 *
 * void __iomem *devm_ioremap(struct device *dev, resource_size_t offset, resource_size_t size)
 *
 * Character Device:
 *  static inline int register_chrdev(unsigned int major, const char *name, const struct file_operations *fops)
 *     Create and register a cdev.
 *     If major == 0, this functions will dynamically allocate a major and return its number.
 *     The name of this device has nothing to do with the name of the device in /dev/. It only helps
 *         to keep track of the different owners of devices.
 *     If your module name has only one type of devices it's ok to use e.g. the name of the module here.
 *  #define MKDEV(major, minor)    (((major) << MINORBITS) | (minor))
 *  class_create(owner, name)
 *     Create a struct class pointer that can then be used in calls to device_create().
 *     owner: pointer to the module that is to "own" this struct class
 *     name: pointer to a string for the name of this class.
 *  struct device *device_create(struct class *class, struct device *parent, dev_t devt, void *drvdata, const char *fmt, ...)
 *     Creates a device and registers it with sysfs
 *     class: pointer to the struct class that this device should be registered to
 *     parent: pointer to the parent struct device of this new device, if any
 *     devt: the dev_t for the char device to be added
 *     drvdata: the data to be added to the device for callbacks
 *     fmt: string for the device's name
 *
 * Device attributes:
 *    struct device_attribute {
 *        struct attribute attr;
 *        ssize_t (*show)(struct device *dev, struct device_attribute *attr, char *buf);
 *        ssize_t (*store)(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
 *     };
 *    #define DEVICE_ATTR(name,mode,show,store)
 *
 ***************************************************************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <asm/nmi.h>
#include <linux/umh.h>

#include "cmos_dev.h"

#define IO_RTC_BANK0_INDEX_PORT              0x70    // CMOS RTC & NVRAM
#define IO_RTC_BANK0_DATA_PORT               0x71    // CMOS RTC & NVRAM
#define IO_RTC_BANK1_INDEX_PORT              0x72    // extended CMOS NVRAM
#define IO_RTC_BANK1_DATA_PORT               0x73    // extended CMOS NVRAM
#define IO_RTC_NUM_PORTS                     4

#define DRV_NAME    "my-dev-drv"

static DEFINE_RWLOCK(my_dev_lock);
static struct class           *my_dev_class = 0;
static struct device          *my_dev = 0;

static struct platform_device *mypdev = 0;

static int my_dev_probe(struct platform_device *pdev);
static int my_dev_remove(struct platform_device *pdev);

static struct platform_driver my_dev_driver = {
    .driver = {
        .name  = DRV_NAME,
        .owner = THIS_MODULE,
    },
    .probe     = my_dev_probe,
    .remove    = my_dev_remove,
};

static int my_dev_major;

static ssize_t my_dev_read(struct file *file, char __user *buf, size_t count, loff_t *offset);
static ssize_t my_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset);
static long    my_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int     my_dev_open(struct inode *inode, struct file *file);
static int     my_dev_release(struct inode *inode, struct file *file);

static ssize_t my_attr_7f_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t my_attr_7f_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t my_attr_7e_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t my_attr_7e_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR(my_attr_7f, 0644, my_attr_7f_show, my_attr_7f_store);
static DEVICE_ATTR(my_attr_7e, 0644, my_attr_7e_show, my_attr_7e_store);
static struct attribute *my_dev_attrs[] = {
    &dev_attr_my_attr_7f.attr,
    &dev_attr_my_attr_7e.attr,
    NULL,
};
static struct attribute_group my_dev_attr_group = {
    .attrs = my_dev_attrs,
    .name  = "my-dev-attrs",
};

static inline uint8_t ext_cmos_read(uint8_t addr)
{
    outb(addr, IO_RTC_BANK1_INDEX_PORT);
    return inb(IO_RTC_BANK1_INDEX_PORT + 1);
}

static inline void ext_cmos_write(uint8_t addr, uint8_t val)
{
    outb(addr, IO_RTC_BANK1_INDEX_PORT);
    outb(val, IO_RTC_BANK1_INDEX_PORT + 1);
}

static ssize_t my_attr_7f_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%hhx\n", ext_cmos_read(0x7F));
}

static ssize_t my_attr_7f_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    long value;
    if (kstrtol(buf, 10, &value) == -EINVAL)
    {
        pr_info("my_attr_7e_store -- buf:%s, count:%ld\n", buf, count);
        return count;
    }

    pr_info("my_attr_7f_store -- buf:%s, count:%ld, value:%ld\n", buf, count, value);

    ext_cmos_write(0x7F, (uint8_t)value);
    return count;
}

static ssize_t my_attr_7e_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%hhx\n", ext_cmos_read(0x7E));
}

static ssize_t my_attr_7e_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    long value;
    if (kstrtol(buf, 10, &value) == -EINVAL)
    {
        pr_info("my_attr_7e_store -- buf:%s, count:%ld\n", buf, count);
        return count;
    }

    pr_info("my_attr_7e_store -- buf:%s, count:%ld, value:%ld\n", buf, count, value);
    ext_cmos_write(0x7E, (uint8_t)value);
    return count;
}

static const struct file_operations my_dev_fops = {
    .owner          = THIS_MODULE,
    .read           = my_dev_read,
    .write          = my_dev_write,
    .unlocked_ioctl = my_dev_ioctl,
    .open           = my_dev_open,
    .release        = my_dev_release,
};

static ssize_t my_dev_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    uint8_t *data  = "My device read\n";
    size_t datalen = strlen(data);
    size_t ret     = 0;

    pr_info("my_dev_read -- count:%ld, offset:%lld\n", count, *offset);

    if (count > datalen)
        count = datalen;

    if (*offset >= datalen)
        ret = 0;           // Return 0 to indicate end of file
    else
    {
        if (copy_to_user(buf, data, count))
        {
            pr_info("my_dev_read -- error reading user input\n");
            return -EFAULT;
        }
        else
        {
            ret = count;
            *offset += ret;
        }
    }

    return ret;
}

static ssize_t my_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
    #define datalen  8
    uint8_t databuf[datalen];
    size_t  ret = 0;

    pr_info("my_dev_write -- count:%ld, offset:%lld\n", count, *offset);

    if (count > datalen)
        count = datalen;

    if (*offset > datalen)
        ret = 0;
    else
    {
        if (copy_from_user(databuf, buf, count))
        {
            pr_info("my_dev_write -- error reading user input\n");
            return -EFAULT;
        }
        else
        {
            ret = count;
            *offset += ret;
        }
    }

    // Expecting just one character and treating it as a command
    switch(databuf[0])
    {
        case 'q':
            pr_info("CMD q received\n");
            // TODO: run the command
            break;

        default:
            pr_info("CMD unknown\n");
            break;
    }

    return ret;
}

static long my_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    mydev_data_t mydev_data;

    if( copy_from_user(&mydev_data, (void __user *)arg, sizeof(mydev_data)) )
    {
        pr_info("my_dev_ioctl -- error reading user input\n");
        return -EFAULT;
    }

    pr_info("my_dev_ioctl -- ioctl:%x, offset:%x, data:%x\n", cmd, mydev_data.offset, mydev_data.data);

    // TODO: sanity check the offset/address
    switch(cmd)
    {
        case MY_DEV_READ:
            read_lock(&my_dev_lock);
            mydev_data.data = ext_cmos_read(mydev_data.offset);
            read_unlock(&my_dev_lock);
            if( copy_to_user((void __user *)arg, &mydev_data, sizeof(mydev_data)) )
            {
                pr_info("my_dev_ioctl -- error reading user input\n");
                return -EFAULT;
            }
            break;

        case MY_DEV_WRITE:
            write_lock(&my_dev_lock);
            ext_cmos_write(mydev_data.offset, mydev_data.data);
            write_unlock(&my_dev_lock);
            break;

        default:
            pr_info("my_dev_ioctl -- unsupported ioctl: %d\n", cmd);
            return -EFAULT;

    }

    return 0;
}

static int my_dev_open(struct inode *inode, struct file *file)
{
    pr_info("my_dev_open -- inode:%p, file:%p\n", inode, file);
    return 0;
}

static int my_dev_release(struct inode *inode, struct file *file)
{
    pr_info("my_dev_release -- inode:%p, file:%p\n", inode, file);
    return 0;
}

static int my_nmi_test(unsigned int val, struct pt_regs* regs);
static int my_dev_probe(struct platform_device *pdev)
{
    int   retval;
    dev_t dev;

    pr_info("my_dev_probe -- pdev:%p\n", pdev);

    if (!devm_request_region(&pdev->dev, IO_RTC_BANK1_INDEX_PORT, IO_RTC_NUM_PORTS / 2, dev_name(&pdev->dev))) {
        dev_err(&pdev->dev, "Cannot get IO port at 0x%x for size of %d\n", IO_RTC_BANK1_INDEX_PORT, IO_RTC_NUM_PORTS / 2);
        return -EBUSY;
    }

    pr_info("My nmi handler: register");
    register_nmi_handler(NMI_LOCAL, my_nmi_test, 0, "my_nmi_test");

    // Passing 0 to major# so that system dynamically allocates one and return it
    retval = register_chrdev(0, dev_name(&pdev->dev), &my_dev_fops);
    if (retval < 0) {
        dev_err(&pdev->dev, "Failed register_chrdev\n");
        devm_release_region(&pdev->dev, IO_RTC_BANK1_INDEX_PORT, IO_RTC_NUM_PORTS / 2);
        return retval;
    }
    my_dev_major = retval;
    dev = MKDEV(my_dev_major, 0);
    // Create a struct class pointer to be used in calls to device_create()
    my_dev_class = class_create(THIS_MODULE, "my-dev-class");
    // Create char device in sysfs, registered to the specified class
    my_dev = device_create(my_dev_class, NULL, dev, NULL, DEV_NAME);
    // Add attributes to sys fs
    sysfs_create_group(&my_dev->kobj, &my_dev_attr_group);

    pr_info("my_dev_probe end\n");
    return 0;
}

static int my_dev_remove(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;

    pr_info("my_dev_remove -- pdev:%p", pdev);

    sysfs_remove_group(&my_dev->kobj, &my_dev_attr_group);
    device_destroy(my_dev_class, MKDEV(my_dev_major, 0));
    class_destroy(my_dev_class);
    unregister_chrdev(my_dev_major, dev_name(dev));

    unregister_nmi_handler(NMI_LOCAL, "my_nmi_test"); 
    devm_release_region(dev, IO_RTC_BANK1_INDEX_PORT, IO_RTC_NUM_PORTS / 2);
    return 0;
}

static void cleanupPdev(void)
{
    if (mypdev) {
        platform_device_put(mypdev);    // Free all memory associated with the platform device
        platform_device_del(mypdev);    // Release all memory- and port-based resources owned by the device (@dev->resource)
        mypdev = 0;
    }
}

static int __init my_dev_init(void)
{
    int ret;

    pr_info("my_dev_init\n");
 
    ret = platform_driver_register(&my_dev_driver);
    if (ret) {
        pr_err(DRV_NAME ": cannot register driver: %d\n", ret);
        return ret;
    }

    mypdev = platform_device_alloc(DRV_NAME, -1);         // DRV_NAME here causes my_dev_probe to be called
    if (!mypdev) {
        pr_err(DRV_NAME ": cannot allocate device\n");
        platform_driver_unregister(&my_dev_driver);
        return ENOMEM;
    }

    ret = platform_device_add(mypdev);
    if (ret) {
        pr_err(DRV_NAME ": cannot register device\n");
        platform_driver_unregister(&my_dev_driver);
        cleanupPdev();
        return ret;
    }

    pr_info("my_dev_init done\n");
    return 0;
}

static void __exit my_dev_exit(void)
{
    pr_info("my_dev_exit\n");
    platform_driver_unregister(&my_dev_driver);
    cleanupPdev();
}

module_init(my_dev_init);
module_exit(my_dev_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("Example CMOS DEV driver");
MODULE_AUTHOR("dyulu <dyulu@example.com>");

uint8_t my_dev_read0(uint16_t offset)
{
    uint8_t data;
    read_lock(&my_dev_lock);
    data = ext_cmos_read(offset);
    read_unlock(&my_dev_lock);
    return data;
}
EXPORT_SYMBOL_GPL(my_dev_read0);    // Only modules that declare a GPL-compatible license will be able to see the symbol

void my_dev_write0(uint16_t offset, uint8_t data)
{
    write_lock(&my_dev_lock);
    ext_cmos_write(offset, data);
    write_unlock(&my_dev_lock);
}
EXPORT_SYMBOL_GPL(my_dev_write0);

static int my_nmi_test(unsigned int val, struct pt_regs* regs)
{
    pr_info("My nmi_test, addr 0x7F:x%02hhx, addr 0x7E:x%02hhx, addr 0x7D:x%02hhx\n",  my_dev_read0(0x7F), my_dev_read0(0x7E), my_dev_read0(0x7D));
    return NMI_DONE;         // NMI_HANDLED;
}

