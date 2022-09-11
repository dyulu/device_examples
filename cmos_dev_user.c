/******************************************************************************************
 * Kernel driver implements access to device register space.
 * sysfs entry: /dev/DEV_NAME
 *****************************************************************************************/

#include <stdio.h>
#include <stdlib.h>       // strtol
#include <fcntl.h>        // open
#include <unistd.h>       // close
#include <sys/ioctl.h>    // ioctl
#include <string.h>       // strcmp
#include <stdint.h>       // uint32_t, etc

#include "cmos_dev.h"

#define MY_DEV "/dev/"DEV_NAME

int main(int argc, char *argv[])
{
    if( argc > 4 )
    {
        printf("Too many arguments supplied: %d\n", argc);
        return -1;
    }

    char* action    = argv[1];
    mydev_data_t dev_data;
    dev_data.offset = (uint32_t)strtol(argv[2], NULL, 0);   // argv[2] = 0x????
    dev_data.data   = 0;

    int fd = open(MY_DEV, O_RDWR);
    if( fd < 0 )
    {
        printf("Failed to open MY_DEV\n");
        return -1;
    }

    if( strcmp(action, "read") == 0)
    {
        if( ioctl(fd, MY_DEV_READ, &dev_data) != 0 )
        {
            printf("Failed to read from MY_DEV\n");
            return -1;
        }

        printf("IOCTL: %lx, Offset %04x: %02x\n", MY_DEV_READ, dev_data.offset, dev_data.data);
    }
    else
    {
        dev_data.data = (uint8_t)strtol(argv[3], NULL, 0);
        printf("IOCTL: %lx, Offset %04x: %02x\n", MY_DEV_WRITE, dev_data.offset, dev_data.data);
        if( ioctl(fd, MY_DEV_WRITE, &dev_data) != 0 )
        {
            printf("Failed to write to MY_DEV\n");
            return -1;
        }
    }

    close(fd);
    return 0;
}

/**************************************************************************************************
$ modprobe cmos_dev
[  144.936922] my_dev_init
[  144.946069] my_dev_probe -- pdev:000000009a22629f
[  144.957416] My nmi handler: register
[  144.958158] my_dev_probe end
[  144.975926] my_dev_init done

$ ls /dev/my-dev 
/dev/my-dev

$ ls /sys/devices/platform/my-dev-drv/
driver  driver_override  modalias  power  subsystem  uevent

$  cat /sys/devices/platform/my-dev-drv/modalias 
platform:my-dev-drv

$ ls /sys/bus/platform/drivers/my-dev-drv/
bind  module  my-dev-drv  uevent  unbind

$ ls /sys/bus/platform/devices/my-dev-drv
driver  driver_override  modalias  power  subsystem  uevent

$ /sys/class/my-dev-class/my-dev# ls *
dev  uevent

my-dev-attrs:
my_attr_7e  my_attr_7f

power:
autosuspend_delay_ms  runtime_active_time  runtime_suspended_time
control               runtime_status

subsystem:
my-dev

$ /sys/class/my-dev-class/my-dev/my-dev-attrs# echo 17 >my_attr_7e
[  358.879771] my_attr_7e_store -- buf:17
[  358.879771] , count:3, value:17

/sys/class/my-dev-class/my-dev/my-dev-attrs$ cat my_attr_7e
11

/sys/class/my-dev-class/my-dev/my-dev-attrs$ echo 255 > my_attr_7e
[  600.867784] my_attr_7e_store -- buf:255
[  600.867784] , count:4, value:255

/sys/class/my-dev-class/my-dev/my-dev-attrs$ cat my_attr_7e
ff

/sys/class/my-dev-class/my-dev/my-dev-attrs$ echo 170 > my_attr_7f
[  814.132791] my_attr_7f_store -- buf:170
[  814.132791] , count:4, value:170

/sys/class/my-dev-class/my-dev/my-dev-attrs$ cat my_attr_7f
aa

$ grep my-dev /proc/ioports
  0072-0073 : my-dev-drv

$ cat /dev/my-dev 
[  163.709637] my_dev_open -- inode:0000000042c4a9f4, file:00000000c5bc3398
[  163.731502] my_dev_read -- count:131072, offset:0
[  163.736206] my_dev_read -- count:131072, offset:15
My device read
[  163.744129] my_dev_release -- inode:0000000042c4a9f4, file:00000000c5bc3398

$ echo quit > /dev/my-dev 
[  218.314087] my_dev_open -- inode:0000000042c4a9f4, file:0000000012204cf8
[  218.320781] my_dev_ioctl -- ioctl:5401, offset:0, data:0
[  218.326081] my_dev_ioctl -- unsupported ioctl: 21505
[  218.345032] my_dev_write -- count:5, offset:0
[  218.349379] CMD q received
[  218.355483] my_dev_release -- inode:0000000042c4a9f4, file:0000000012204cf8

$ echo exit > /dev/my-dev 
[  249.767057] my_dev_open -- inode:0000000042c4a9f4, file:00000000e5760fd2
[  249.773749] my_dev_write -- count:5, offset:0
[  249.778097] CMD unknown
[  249.780785] my_dev_release -- inode:0000000042c4a9f4, file:00000000e5760fd2

$ ./cmos_dev_user write 0x7F 0xaa
[ 1156.578781] my_dev_open -- inode:000000005c305211, file:0000000085eee9c1
IOCTL: 40084601,[ 1156.601437] my_dev_ioctl -- ioctl:40084601, offset:7f, data:aa
 Offset 007f: aa[ 1156.608634] my_dev_release -- inode:000000005c305211, file:0000000085eee9c1

$ ./cmos_dev_user read 0x7F
[ 1172.878909] my_dev_open -- inode:000000005c305211, file:000000003873d0bb
[ 1172.886972] my_dev_ioctl -- ioctl:80084600, offset:7f, data:0
IOCTL: 80084600,[ 1172.893775] my_dev_release -- inode:000000005c305211, file:000000003873d0bb
 Offset 007f: aa

$ in /var/syslog
[ 1653.675441] My nmi_test, addr 0x7F:xaa, addr 0x7E:xff, addr 0x7D:xbb
****************************************************************************************************/

