/***********************************************************************************
 * int ioperm(unsigned long from, unsigned long num, int turn_on)
 *     set port input/output permissions
 *     /proc/ioports shows the I/O ports that are currently allocated on the system
 * Give access to ports 0x000 through 0x3FF
 *
 * User space outb and inb are x86-specific. Program needs to run as root or with
 *     CAP_SYS_RAWIO capability.
 * Kernel space outb and inb are implemented in an architecture-specific manner.
 * Kernel  character device driver would work on any PCI-supporting architecture.
 * Can use UNIX permissions on the character device file to control user space access.
 * Kernel function request_region can ensure no IO port clashes with other driver.
 ***********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/io.h>       // ioperm
#include <string.h>       // strcmp
#include <stdint.h>       // uint32_t, etc

/***********************************************************************************
 * CMOS: complementary metal-oxide semiconductor
 *       small amount of memory, typically 256 bytes,  on a computer motherboard to
 *       store the BIOS settings, including the system time and date, start-up options,
 *       boot order
 *
 * Most newer x86 systems have two register banks, the first used for RTC and NVRAM and
 *     the second only for NVRAM.
 *     IO_RTC_BANK0_INDEX_PORT 0x70 and IO_RTC_BANK0_DATA_PORT 0x71: bank 0,
 *         accessing 128 byte RTC + NVRAM address space
 *     IO_RTC_BANK1_INDEX_PORT 0x72 and IO_RTC_BANK1_DATA_PORT 0x73: bank 1,
 *         accessing 128 byte extended NVRAM address space
 *     Specify the desired CMOS bank 0/1 offset, e.g., 0x7F for last byte, in the
 *         index register, and then read/write data from/to data register
 ***********************************************************************************/

#define IO_RTC_BANK1_INDEX_PORT              0x72    // extended CMOS NVRAM

static inline unsigned char ext_cmos_read(unsigned char addr)
{
    outb(addr, IO_RTC_BANK1_INDEX_PORT);
    return inb(IO_RTC_BANK1_INDEX_PORT + 1);
}

static inline void ext_cmos_write(unsigned char addr, unsigned char val)
{
    outb(addr, IO_RTC_BANK1_INDEX_PORT);
    outb(val, IO_RTC_BANK1_INDEX_PORT + 1);
}

int main(int argc, char *argv[])
{
    if( argc > 4 )
    {
        printf("Too many arguments supplied: %d\n", argc);
        return -1;
    }

    char* action = argv[1];
    uint32_t offset = (uint32_t)strtol(argv[2], NULL, 0);   // argv[2] = 0x????
    uint8_t data = 0;
    printf("%s %s %d\n", argv[0], action, offset);

    // Request access to the ports; avoid general protection fault
    // Need root privileges
    if (ioperm(IO_RTC_BANK1_INDEX_PORT, 2, 1))
    {
        printf("Error requesting IO port access");
        return -1;
    }

    if( strcmp(action, "read") == 0)
    {
        printf("Offset %02x: %02hhx\n", offset, ext_cmos_read(offset));
    }
    else
    {
        data = (uint8_t)strtol(argv[3], NULL, 0);
        printf("Offset %02x: %02hhx, before writing\n", offset, ext_cmos_read(offset));
        ext_cmos_write(offset, data);
        printf("Offset %02x: %02hhx, after writing\n", offset, ext_cmos_read(offset));
    }

    if (ioperm(IO_RTC_BANK1_INDEX_PORT, 2, 0))
    {
        printf("Error release IO ports");
        return -1;
    }

    return 0;
}

