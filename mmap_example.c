/**********************************************************************************************
 * void* mmap(void *address, size_t length, int protect, int flags, int filedes, off_t offset)
 *    address: preferred starting address for the mapping; kernel will pick a nearby page boundary
 *             NULL: the kernel can place the mapping anywhere it sees fit
 *    length:  number of bytes to be mapped
 *    protect: permitted access; PROT_READ | PROT_WRITE | PROT_EXEC | PROT_NONE
 *    flags:   MAP_SHARED - share the mapping with all other processes, which are mapped to this object.
 *                          Changes made to the mapping region will be written back to the file.
 *             MAP_PRIVATE - the mapping will not be seen by any other processes, and
 *                           the changes made will not be written to the file.
 *             MAP_ANONYMOUS/MAP_ANON - mapping is not connected to any files.
 *                                      used as the basic primitive to extend the heap.
 *             MAP_FIXED - system is forced to use the exact mapping address. Fail if not poossible.
 *    filedes: file descriptor to be mapped.
 *    offset:  offset from where the file mapping starts.
 *    returns: mapping address on success; MAP_FAILED on failure
 *
 * int munmap(void *address, size_t length)
 *    returns 0 on successful unmap; returns -1 on failure
 *********************************************************************************************/

/**********************************************************************************************
 * Accessing PCI device resources through sysfs: https://docs.kernel.org/PCI/sysfs-pci.html
 *
 * Simple program to read & write to a pci device from userspace: https://github.com/billfarrow/pcimem
 *     lspci -v -s 0001:00:07.0: look for starting "Memory at xxxxxxxx"
 *     ls -l /sys/devices/pci0001\:00/0001\:00\:07.0/: look for resources0..N
 *     PCI device makes memory regions available to host via mmap
 *
 * Get the complete list of all registers in the standard configuration headers:
 *    setpci --dumpregs
 *
 * Enable memory space access for the specified PCI device: 
 *    setpci -s B:D:F 04.B=02:02
 *    PCI Command Register 0x0004, Bit 1 Memory_Access_Enable
 *    .B, .W, or .L: 1 or 2 or 4 bytes
 *    BITs:MASK: the bits in the MASK will be changed to values of the corresponding bits in the BITs
 *
 * Code snippets:
 *     MEM_REGION_SIZE   = 4096;
 *     MEM_REGION_OFFSET = 0;
 *     fd = open("/sys/devices/pci0001\:00/0001\:00\:07.0/resource0", O_RDWR | O_SYNC);
 *     bar0 = mmap(NULL, MEM_REGION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, MEM_REGION_OFFSET);
 *     close(fd);
 *     printf("PCI BAR0 0x0000 = 0x%4x\n",  *((unsigned short *)bar0);
 *     mlock(bar0, MEM_REGION_SIZE);
 *     ......
 *     munmap(bar0, MEM_REGION_SIZE);
 *
 *     # Open config space for reading PCI header
 *     uint32_t config[64];
 *     fd = open("/sys/devices/pci0001\:00/0001\:00\:07.0/config", O_RDONLY);
 *     i = read(fd, config, 64);
 *
 * BusDeviceFunction: lspci -n | grep VendorID_DeviceID | cut -d' ' -f1
 * /sys/bus/pci/devices/0000:BusDeviceFunction/resource0/
 *********************************************************************************************/

#include <stdio.h>
#include <stdlib.h>       // strtol
#include <fcntl.h>        // open
#include <unistd.h>       // close
#include <sys/mman.h>     // mmap
#include <string.h>       // strcmp
#include <stdint.h>       // uint32_t, etc

typedef unsigned int   bool_t;

#define DEV_SYS_MAP_BASE_ADDR         0xface0000 // specific to the platfrom
#define DEV_REG_FILE_LENGTH           0x200      // specific to the device
#define DEV_ADDR_UPPER_BOUND          0x100      // specific to the device

static void* gDevSystemMapAddr = NULL;

int devSystemAddrMap()
{
    int    localFd = -1;
    void*  localAddr = NULL;
    size_t offset;

    // get the system map address for the device
    localFd = open("/dev/mem", O_RDWR | O_SYNC);
    if( localFd < 0 )
    {
        printf("unable to open device mem\n");
        return -1;
    }

    // align it to a page size. a page size is 4k in this system
    offset = DEV_SYS_MAP_BASE_ADDR & ~(sysconf(_SC_PAGE_SIZE) - 1);
    localAddr = mmap(NULL,
                    (DEV_REG_FILE_LENGTH + DEV_SYS_MAP_BASE_ADDR - offset),
                     (PROT_READ|PROT_WRITE),
                     MAP_SHARED,
                     localFd,
                     offset);
    if( localAddr == MAP_FAILED )
    {
        printf("mmap failed\n");
        close(localFd);
        localAddr = NULL;
        localFd = -1;
        return -2;
    }

    gDevSystemMapAddr = localAddr;
    close(localFd);

    return 0;
}

void devSystemAddrUnmap()
{
    size_t offset = DEV_SYS_MAP_BASE_ADDR & ~(sysconf(_SC_PAGE_SIZE) - 1);
    if( munmap(gDevSystemMapAddr,
               (DEV_REG_FILE_LENGTH + DEV_SYS_MAP_BASE_ADDR - offset)) )
        printf("Unmapping failed\n");
}

int devRegAction(bool_t read, uint32_t offset, uint8_t* val)
{
    uint16_t          shiftedOffset;
    volatile uint8_t* address;

    if( offset < DEV_ADDR_UPPER_BOUND )    
    {                          
        address = (uint8_t *)gDevSystemMapAddr + offset;
        if(read)
            *val = *address;
        else
            *address = *val;
    }
    else
    {       
        printf("devRegAction: no memory\n");
        return -1;
    }   

    return 0;
}

int main(int argc, char *argv[])
{
    if( argc > 4 )
    {
        printf("Too many arguments supplied: %d\n", argc);
        return -1;
    }

    char* action = argv[1];
    uint32_t reg = (uint32_t)strtol(argv[2], NULL, 0);   // argv[2] = 0x????
    uint8_t data = 0;
    printf("%s %s %d\n", argv[0], action, reg);

    if( devSystemAddrMap() != 0)
        return -1;

    if( strcmp(action, "read") == 0)
    {
        devRegAction(1, reg, &data);
        printf("Reg %04x: %02x\n", reg, data);
    }
    else
    {
        data = (uint8_t)strtol(argv[3], NULL, 0);
        devRegAction(0, reg, &data);
    }

    devSystemAddrUnmap();

    return 0;
}

/**********************************************************************************************
# Differences between ioremap and file operation mmap
# https://unix.stackexchange.com/questions/239205/whats-the-difference-between-ioremap-and-file-operation-mmap
# https://static.lwn.net/images/pdf/LDD3/ch09.pdf

# mmap: a syscall available in user space that maps a process memory region to content of a file, instead of RAM.
#       Need superuser to open "/dev/mem" then mmap to map the region of physical memory.
#       Mapping a device means associating a range of user-space addresses to device memory.
#       Whenever the program reads or writes in the assigned address range, it is actually accessing the device.

# ioremap: a kernel function that allows accessing hardware through a mechanism called I/O mapped memory.
#          There are certain addresses in memory that are intercepted by motherboard between CPU and RAM
#          and redirected to other hardware, like disks or keyboard.

# https://lists.kernelnewbies.org/pipermail/kernelnewbies/2016-September/016814.html
# Whether in user space or in kernel space, software cannot directly access the physical address of a device.
# mmap:    maps device physical address (device memory or device registers) to user virtual address
# ioremap: maps device physical address (device memory or device registers) to kernel virtual address
**********************************************************************************************/
