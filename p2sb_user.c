/****************************************************************************************************************
 *
 * https://wiki.osdev.org/PCI
 * https://burgers.io/pci-access-without-a-driver
 * https://nixhacker.com/playing-with-pci-device-memory/
 * https://github.com/shubham0d/pci-mem-drivers
 *
 ***************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>       // strtol
#include <fcntl.h>        // open
#include <unistd.h>       // close
#include <sys/mman.h>     // mmap
#include <string.h>       // strcmp
#include <stdint.h>       // uint32_t, etc
#include <sys/io.h>       // outl, inl
#include <sys/stat.h>

// Enable memory space access for the specified PCI device:
//    setpci -s B:D:F 04.B=02:02
//    PCI Command Register 0x0004, Bit 1 Memory_Access_Enable
//    .B, .W, or .L: 1 or 2 or 4 bytes
//    BITs:MASK: the bits in the MASK will be changed to values of the corresponding bits in the BITs
#define PCI_COMMAND_REG                 "0x04.B"

// P2SB device is hidden by the BIOS before PCI enumeration step by setting P2SB PCI offset E1h[0] to 1
// Unhide the device in OS kernel:
//     setpci -s B:D:F E1.B=00:01
#define PCI_P2SB_HIDE_REG               "0xE1.B"

#define SYSFS_DEV_PREFIX                "/sys/bus/pci/devices/0000:"

#define VendorID_DeviceID               "8086:A1A0"
#define PCI_GET_BDF(VID_DID)            "lspci -n | grep -i " VID_DID " | cut -d' ' -f1"

// P2SB configuration space register
#define PCI_P2SB_BAR                    0x10
#define PCI_P2SB_BAR_H                  0x14
#define PCI_P2SB_CTRL                   0xE0

// P2SB private registers
#define GPIO_COMMUNITY_1_PORT_ID        0xAE
#define GPIO_COMMUNITY_0_PORT_ID        0xAF
#define GPIO_PORT_ID_SHIFT              16

#define GPIO_COMMUNITY_1_SIZE           (64 * 1024)       // 65536 = 0x10000
#define GPIO_COMMUNITY_0_SIZE           (64 * 1024)
#define GPIO_COMMUNITY_OFFSET(PortID)   (PortID << GPIO_PORT_ID_SHIFT)

// GPIO sideband registers
#define PCI_P2SB_GPIO_PAD_BAR           0x0C         // default 0x400
#define PCI_P2SB_GPIO_PAD_OWNERSHIP     0x20         // 00 - host GPIO ACPI mode or GPIO Driver mode, 01 - ME GPIO mode, 10 - reserved, or 11 - IE GPIO mode
#define PCI_P2SB_GPIO_PAD_HOSTSW_OWNSHIP 0x80        // 0 - ACPI mode, 1 - GPIO driver mode
#define PCI_P2SB_GPIO_NMI_ENABLE        0x178        // bit 31:9 reserved; 0 - disable NMI generation, 1 - enable NMI generation

// PCI configuration space registers: accessed via CF8/CFC ports
#define PCI_CFG_ADDR                    0xCF8        // index register for PCI config space access
#define PCI_CFG_DATA                    0xCFC        // data register for PCI config space access
#define PCI_CFGTAG_ENABLE               0x80000000
#define PCI_CFGTAG(bus, dev, func, reg) (PCI_CFGTAG_ENABLE | ((bus) << 16) | ((dev) << 11) | ((func) << 8) | (reg))
static inline uint32_t pci_cfg_reg_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uintptr_t address = (uintptr_t)PCI_CFGTAG(bus, dev, func, (reg & 0xFC));
    outl(address, PCI_CFG_ADDR);
    return inl(PCI_CFG_DATA);
}

typedef enum { F, T } boolean;

static inline boolean isMemorySpace(uint32_t bar)
{
    return (bar & 0x00000001) ? F : T;
}

static inline boolean isMemory64bit(uint32_t bar)
{
    return (isMemorySpace(bar) == T) ? ((bar & 0x00000006) ? T : F) : F;
}

static inline uint64_t bar64bit(uint32_t bar, uint32_t barh)
{
    return ((uint64_t)barh << 32) + (bar & 0xFFFFFFF0);
}

static int p2sb_config_registers(uint8_t bus, uint8_t dev, uint8_t func)
{
    // Request access to the ports; avoid general protection fault
    // Need root privileges
    if (ioperm(PCI_CFG_ADDR, 8, 1))
    {
        printf("Error requesting IO port access");
        return -1;
    }

    printf("Selected configuration registers for device %x:%x:%x\n", bus, dev, func);
    uint32_t bar  = pci_cfg_reg_read_dword(bus, dev, func, PCI_P2SB_BAR);
    uint32_t barh = pci_cfg_reg_read_dword(bus, dev, func, PCI_P2SB_BAR_H);
    printf("  PCI_P2SB_BAR:    %08x\n", bar);
    printf("  PCI_P2SB_BAR_H:  %08x\n", barh);
    printf("  PCI_P2SB_CTRL:   %08x\n", pci_cfg_reg_read_dword(bus, dev, func, PCI_P2SB_CTRL));

    if (isMemory64bit(bar) == T)
    {
        uint64_t bar64 = bar64bit(bar, barh);
        printf("  PCI_P2SB_BAR_64: %016lx\n", bar64bit(bar, barh));
    }

    if (ioperm(PCI_CFG_ADDR, 8, 0))
    {
        printf("Error requesting IO port access");
        return -1;
    }

    return 0;
}

// GPIO community registers: accessed via SBREG_BAR + PortID << GPIO_PORT_ID_SHIFT + Register Offset
// In driver space, get BAR from reading PCI_P2SB_BAR and PCI_P2SB_BAR_H, and then use ioremap
static inline uint32_t p2sb_gpio_reg_read(void *p2sb_bar, uint8_t port_id, uint16_t reg)
{
    uintptr_t address = ((uintptr_t)p2sb_bar + (port_id << GPIO_PORT_ID_SHIFT) + reg);
    return *((volatile uint32_t *)address);
}

static inline uint32_t p2sb_gpio_reg_read2(void *gpio_community_bar, uint16_t reg)
{
    uintptr_t address = ((uintptr_t)gpio_community_bar + reg);
    return *((volatile uint32_t *)address);
}

// bus: g_bdf[0]; dev: g_bdf[1]; func: g_bdf[2]
static uint8_t g_bdf[3];

#define BDF_STR_LEN 16
static int p2sb_dev()
{
    // Device bus:device:function
    char dev_bdf[2][BDF_STR_LEN];

    // Open the shell cmd for reading
    char* cmd = PCI_GET_BDF(VendorID_DeviceID);
    FILE *fp = popen(cmd, "r");
    if (fp == NULL)
    {
        printf("Failed to run command: %s\n", cmd);
        return -1;
    }

    // Read the output a line at a time
    int i = 0;
    while (fgets(dev_bdf[i], sizeof(dev_bdf[i]), fp) != NULL)
    {
        // Get rid of \n at the end
        // strcspn counts the number of characters until it hits a '\r' or a '\n'
        dev_bdf[i][strcspn(dev_bdf[i], "\n")] = 0;
        printf("Dev%d, B:D:F - %s\n", i, dev_bdf[i]);
        i++;
    }
    pclose(fp);

    // Unhide the device in OS kernel
    char cmd_line[256];
    snprintf(cmd_line, sizeof(cmd_line), "setpci -s %s %s=00:01", dev_bdf[0], PCI_P2SB_HIDE_REG);
    system(cmd_line);

    // Open resource0 file
    char dev_resurce0_file[256];
    snprintf(dev_resurce0_file, sizeof(dev_resurce0_file), "%s%s/resource0", SYSFS_DEV_PREFIX, dev_bdf[0]);
    int fd = open(dev_resurce0_file, O_RDWR|O_SYNC);
    if (fd == -1)
    {
        printf("Failed to open: %s\n", dev_resurce0_file);
        return -1;
    }

    struct stat filestat;
    if (fstat(fd, &filestat) == -1)
    {
        printf("Failed to read stats: %s\n", dev_resurce0_file);
        return -1;
    }

    printf("File:%s, size:%ld\n", dev_resurce0_file, filestat.st_size);

    off_t gpio_comm1_offset = GPIO_COMMUNITY_OFFSET(GPIO_COMMUNITY_1_PORT_ID);
    void* gpio_comm1_bar = mmap(NULL, GPIO_COMMUNITY_1_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpio_comm1_offset);
    if (gpio_comm1_bar == MAP_FAILED)
    {
        printf("mmap failed: %s\n", dev_resurce0_file);
        close(fd);
        return -1;
    }

    printf("P2SB GPIO Community 1 bar: %p\n", gpio_comm1_bar);

    // Lock the memory so it will not be paged out
    if (mlock(gpio_comm1_bar, GPIO_COMMUNITY_1_SIZE))
    {
        printf("mlock failed: %s\n", dev_resurce0_file);
        munmap(gpio_comm1_bar, GPIO_COMMUNITY_1_SIZE);
        close(fd);
        return -1;
    }

    // Enable memory space access
    snprintf(cmd_line, sizeof(cmd_line), "setpci -s %s %s=02:02", dev_bdf[0], PCI_COMMAND_REG);
    system(cmd_line);

    char dev_bdf_str[BDF_STR_LEN];
    strcpy(dev_bdf_str, dev_bdf[0]);
    // strtok performs one split and returns a pointer to the token split up
    // A null pointer is returned if the string cannot be split
    // Convert string to long int: long int strtol(const char *str, char **endptr, int base)
    //     dev_bdf_str format: Bus:Dev.Func
    char* bus_str      = strtok(dev_bdf_str, ":");
    char* dev_func_str = strtok(NULL, ":");
    char* dev_str      = strtok(dev_func_str, ".");
    char* func_str     = strtok(NULL, ".");;
    g_bdf[0] = (uint8_t)strtol(bus_str,  NULL, 16);
    g_bdf[1] = (uint8_t)strtol(dev_str,  NULL, 16);
    g_bdf[2] = (uint8_t)strtol(func_str, NULL, 16);

    printf("Selected GPIO_COMMUNITY_1 registers:\n");
    printf("  PCI_P2SB_GPIO_PAD_BAR:           %08x\n", p2sb_gpio_reg_read2(gpio_comm1_bar, PCI_P2SB_GPIO_PAD_BAR));
    printf("  PCI_P2SB_GPIO_PAD_OWNERSHIP:     %08x\n", p2sb_gpio_reg_read2(gpio_comm1_bar, PCI_P2SB_GPIO_PAD_OWNERSHIP));
    printf("  PCI_P2SB_GPIO_PAD_HOSTSW_OWNSHIP:%08x\n", p2sb_gpio_reg_read2(gpio_comm1_bar, PCI_P2SB_GPIO_PAD_HOSTSW_OWNSHIP));
    printf("  PCI_P2SB_GPIO_NMI_ENABLE:        %08x\n", p2sb_gpio_reg_read2(gpio_comm1_bar, PCI_P2SB_GPIO_NMI_ENABLE));
    printf("\n");

    // Unlock the memory
    if (munlock(gpio_comm1_bar, GPIO_COMMUNITY_1_SIZE))
    {
        printf("munlock failed: %s\n", dev_resurce0_file);
        munmap(gpio_comm1_bar, GPIO_COMMUNITY_1_SIZE);
        close(fd);
        return -1;
    }

    p2sb_config_registers(g_bdf[0], g_bdf[1], g_bdf[2]);

    // Hide the device in OS kernel
    snprintf(cmd_line, sizeof(cmd_line), "setpci -s %s %s=01:01", dev_bdf[0], PCI_P2SB_HIDE_REG);
    system(cmd_line);

    printf("P2SB is hidden now so all register reads will return FFFFFFFF ... ");
    p2sb_config_registers(g_bdf[0], g_bdf[1], g_bdf[2]);

    if (munmap(gpio_comm1_bar, GPIO_COMMUNITY_1_SIZE))
    {
        printf("munmap failed: %s\n", dev_resurce0_file);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    p2sb_dev();
    p2sb_config_registers(g_bdf[0], g_bdf[1], g_bdf[2]);
}

/****************************************************************************************************************

P2SB: Primary to Sideband bridge

# Intel® C620 Series Chipset Platform Controller Hub BIOS Specification
# Intel® C620 Series Chipset Platform Controller Hub External Design Specification 
#     P2SB Configuration Registers Summary
#         0h-3h,   PCI Identifier (PCIID)—Offset 0h, A1A08086h
#         10h-13h, Sideband Register Access BAR (SBREG_BAR)—Offset 10h, 4h
#         14h-17h, Sideband Register BAR High DWORD (SBREG_BARH)—Offset 14h, 0h
#         E0h-E4h, P2SB Control (P2SBC) - Offset E0h, 0h
#             Bit 31-9: reserved
#             Bit 8: Device Hide(HIDE): When this bit is set, the P2SB will return 1s on any PCI Configuration Read on IOSF-P
#             Bit 7-0: Reserved

# GPIO Community Registers:
#    within the PCH Private Configuration Space
#    accessible through the PCH Sideband Interface
#    accessed via (SBREG_BAR + PortID + Register Offset)
# 
# P2SB GPIO PortID (PortID shift 16):
#    Community0: PortID = 0xAF
#    Community1: PortID = 0xAE
#    Community2: PortID = 0xAD
#    Community3: PortID = 0xAC

$ lspci -n | grep 8086:a1a0
00:1f.1 0580: 8086:a1a0 (rev ff)
$ lspci -n | grep -i 8086:A1A0 | cut -d' ' -f1
00:1f.1

$ lspci -vvv -d 8086:A1A0
00:1f.1 Memory controller: Intel Corporation C620 Series Chipset Family P2SB (rev ff) (prog-if ff)
	!!! Unknown header type 7f

# Unhide P2SB for Memory Access
$ setpci -d 8086:A1A0 0xE1.B
ff
$ setpci -d 8086:A1A0 0xE1.B=00:01
$ setpci -d 8086:A1A0 0xE1.B
fe
$ lspci -vvv -d 8086:A1A0
00:1f.1 Memory controller: Intel Corporation C620 Series Chipset Family P2SB (rev 04)
	Subsystem: Intel Corporation C620 Series Chipset Family P2SB
	Control: I/O- Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-
	Status: Cap- 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
	Latency: 0
	Region 0: Memory at d000000000 (64-bit, non-prefetchable) [size=16M]

# Disable memory space access
$ setpci -d 8086:A1A0 0x04.B=00:02
$ lspci -vvv -d 8086:A1A0
00:1f.1 Memory controller: Intel Corporation C620 Series Chipset Family P2SB (rev 04)
	Subsystem: Intel Corporation C620 Series Chipset Family P2SB
	Control: I/O- Mem- BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-
	Status: Cap- 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
	Latency: 0
	Region 0: Memory at d000000000 (64-bit, non-prefetchable) [disabled] [size=16M]

# Re-enable memory space access
$ setpci -d 8086:A1A0 0x04.B=02:02

$ ls /sys/devices/pci0000:00/0000:00:1f.1/resource0
/sys/devices/pci0000:00/0000:00:1f.1/resource0
$ ls /sys/bus/pci/devices/0000:00:1f.1/resource0
/sys/bus/pci/devices/0000:00:1f.1/resource0

# https://wiki.osdev.org/PCI
# A PCI configuration header has room for up to six 32-bit BAR values or three 64-bit BAR values
# A PCI configuration header may also contain a mix of both 32-bit BAR values and 64-bit BAR values
# Each BAR is 32 bit , out of which first 4 bits, i.e., 3:0, are always Read Only
# Each BAR corresponds to an address range that serves as a separate communication channel to the PCI device
# Bar has three states: uninitialized, all 1s, and written address
#     Bit 0: 0 - memory space, 1 - IO space
#     If Bit 0 is 1: Bit 1 is reserved, Bit 31-2 is 4-byte aligned base address
#     If Bit 0 is 0:
#         Bit 2-1: if Bit 0 is 0, 00 - 32 bit, 01 - reserved, 10 - 64 bit
#         Bit 3: if Bit 0 indicates prefetchable or not
# 32 bit base address = BAR[x] & BAR[x] & 0xFFFFFFF0
# 64 bit base address = ((BAR[x + 1] & 0xFFFFFFFF) << 32) + (BAR[x] & 0xFFFFFFF0)
# To get BAR size, save the original BAR value, write a value of all 1's to the register, then read it back
#     See kernel code __pci_read_base
# 
$ ./pci_header 0 0x1f 1 0x0e
./pci_header 0 1f 1
Selected device 0:1f:1 is an Endpoint
|    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |    |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |
|-----------------------------------------------------------|    |-----------------------------------------------------------|    Address
|          Vendor ID          |          Device ID          |    |            0x8086           |            0xA1A0           |    0x00
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|           Command           |           Status            |    |            0x0006           |            0x0000           |    0x04
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Revision ID  |                 Class Code                 |    |     0x04     |                   0x000580                 |    0x08
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Cache Line S |  Lat. Timer  | Header Type  |     BIST     |    |     0x00     |     0x00     |     0x00     |     0x00     |    0x0c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 0                           |    |                          0x00000004                       |    0x10
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 1                           |    |                          0x000000D0                       |    0x14
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 2                           |    |                          0x00000000                       |    0x18
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 3                           |    |                          0x00000000                       |    0x1c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 4                           |    |                          0x00000000                       |    0x20
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 5                           |    |                          0x00000000                       |    0x24
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                    Cardbus CIS Pointer                    |    |                          0x00000000                       |    0x28
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|     Subsystem Vendor ID     |        Subsystem ID         |    |            0x8086           |            0x7270           |    0x2c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                   Expansion ROM Address                   |    |                          0x00000000                       |    0x30
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Cap. Pointer |                  Reserved                  |    |     0x00     |                   0x000000                 |    0x34
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                         Reserved                          |    |                          0x00000000                       |    0x38
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|     IRQ      |   IRQ Pin    |   Min Gnt.   |   Max Lat.   |    |     0x00     |     0x00     |     0x00     |     0x00     |    0x3c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
reg 0e: 00000000
reg 0e: 0000
reg 0e: 00

****************************************************************************************************************/
