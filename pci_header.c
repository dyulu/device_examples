/****************************************************************************************************************

https://en.wikipedia.org/wiki/PCI_configuration_space

Access PCIe config registers:
256 buses/system
32 devices/bus
8 functions/device
For each function:
    256 bytes PCI config registers: first 64 bytes are standardized; the remainder are available for vendor-defined purposes
    4 KB PCIe config registers: first four bytes at 0x100 are the start of an extended capability list

For PCI config space access:
    IO port:   0xCF8, index register
    Data port: 0xCFC, data register

Write bus:device:function:register to IO port 0xCF8: e.g., 3:2:5:40, 0x80031540,
    32 bits: 8 bits for Register, 3 bits for Function, 5 bits for Device and 8 bits for Busesa; Register needs to be DWORD aligned
    1000 0000 BBBB BBBB DDDD DFFF RRRR RRRR

Read from IO port 0xCFC:
    MOV DX, 0CFCh
    IN  EAX, DX

Memory mapped IO access to PCIe config registers:
    256 MB MM-config-space: 4KB * 8 * 32 * 256
    1 MB for each bus
    32 KB for each device
    4 KB for each function

Status register: used to report which features are supported and whether certain kinds of errors have occurred

Command register: contains a bitmask of features that can be individually enabled and disabled

Type 1 headers for Root Complex, switches, and bridges.
Type 0 for endpoints.

****************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/io.h>       // ioperm, outl, inl
#include <string.h>       // strcmp
#include <stdint.h>       // uint32_t, etc

#define PCI_CFG_ADDR                    0xCF8        // index register for PCI config space access
#define PCI_CFG_DATA                    0xCFC        // data register for PCI config space access

// Write bus:device:function:register to index port 0xCF8: 1000 0000 BBBB BBBB DDDD DFFF RRRR RRRR
// Register needs to be DWORD, e.g., 32 bits, aligned: if ((address & 0x3) == 0) { // 4-byte aligned here }
// Read from data port 0xCFC
#define PCI_CFGTAG_ENABLE               0x80000000
#define PCI_CFGTAG(bus, dev, func, reg) (PCI_CFGTAG_ENABLE | ((bus) << 16) | ((dev) << 11) | ((func) << 8) | (reg))

// linux/pci_regs.h
// Under PCI, each device has 256 bytes of configuration address space, of which the 1st 64 bytes are standardized:
#define PCI_VENDOR_ID                  0x00     // 16 bits 
#define PCI_DEVICE_ID                  0x02     // 16 bits
#define PCI_COMMAND                    0x04     // 16 bits
#define  PCI_COMMAND_IO                   0x1      // Enable response in I/O space
#define  PCI_COMMAND_MEMORY               0x2      // Enable response in Memory space
#define  PCI_COMMAND_MASTER               0x4      // Enable bus mastering
#define  PCI_COMMAND_SPECIAL              0x8      // Enable response to special cycles
#define  PCI_COMMAND_INVALIDATE           0x10     // Use memory write and invalidate
#define  PCI_COMMAND_VGA_PALETTE          0x20     // Enable palette snooping
#define  PCI_COMMAND_PARITY               0x40     // Enable parity checking
#define  PCI_COMMAND_WAIT                 0x80     // Enable address/data stepping
#define  PCI_COMMAND_SERR                 0x100    // Enable SERR
#define  PCI_COMMAND_FAST_BACK            0x200    // Enable back-to-back writes
#define  PCI_COMMAND_INTX_DISABLE         0x400    // INTx Emulation Disable

#define PCI_STATUS                     0x06     // 16 bits
#define   PCI_STATUS_IMM_READY            0x01     // Immediate Readiness
#define   PCI_STATUS_INTERRUPT            0x08     // Interrupt status
#define   PCI_STATUS_CAP_LIST             0x10     // Support Capability List
#define   PCI_STATUS_66MHZ                0x20     // Support 66 MHz PCI 2.1 bus
#define   PCI_STATUS_UDF                  0x40     // Support User Definable Features [obsolete]
#define   PCI_STATUS_FAST_BACK            0x80     // Accept fast-back to back
#define   PCI_STATUS_PARITY               0x100    // Detected parity error
#define   PCI_STATUS_DEVSEL_MASK          0x600    // DEVSEL timing
#define   PCI_STATUS_DEVSEL_FAST          0x000
#define   PCI_STATUS_DEVSEL_MEDIUM        0x200
#define   PCI_STATUS_DEVSEL_SLOW          0x400
#define   PCI_STATUS_SIG_TARGET_ABORT     0x800    // Set on target abort
#define   PCI_STATUS_REC_TARGET_ABORT     0x1000   // Master ack of "
#define   PCI_STATUS_REC_MASTER_ABORT     0x2000   // Set on master abort
#define   PCI_STATUS_SIG_SYSTEM_ERROR     0x4000   // Set when we drive SERR
#define   PCI_STATUS_DETECTED_PARITY      0x8000   // Set on parity error

#define PCI_CLASS_REVISION              0x08    // High 24 bits are class, low 8 revision
#define PCI_REVISION_ID                 0x08    // Revision ID
#define PCI_CLASS_PROG                  0x09    // Reg. Level Programming Interface
#define PCI_CLASS_DEVICE                0x0a    // Device class

#define PCI_CACHE_LINE_SIZE             0x0c    // 8 bits
#define PCI_LATENCY_TIMER               0x0d    // 8 bits
#define PCI_HEADER_TYPE                 0x0e    // 8 bits
#define   PCI_HEADER_TYPE_MASK            0x7f
#define   PCI_HEADER_TYPE_NORMAL          0
#define   PCI_HEADER_TYPE_BRIDGE          1
#define   PCI_HEADER_TYPE_CARDBUS         2

static inline uint8_t pci_cfg_reg_read_byte(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{   
    uint32_t address = (uint32_t)PCI_CFGTAG(bus, dev, func, (reg & 0xFC));
    outl(address, PCI_CFG_ADDR);
    // (reg & 3) * 8) = 0 will choose the first byte of the 32-bit register
    return (uint8_t)((inl(PCI_CFG_DATA) >> ((reg & 3) * 8)) & 0xFF);
}

static inline uint16_t pci_cfg_reg_read_word(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uint32_t address = (uint32_t)PCI_CFGTAG(bus, dev, func, (reg & 0xFC));
    outl(address, PCI_CFG_ADDR);
    // (reg & 2) * 8) = 0 will choose the first word of the 32-bit register
    return (uint16_t)((inl(PCI_CFG_DATA) >> ((reg & 2) * 8)) & 0xFFFF);
}

static inline uint32_t pci_cfg_reg_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uint32_t address = (uint32_t)PCI_CFGTAG(bus, dev, func, (reg & 0xFC));
    outl(address, PCI_CFG_ADDR);
    return inl(PCI_CFG_DATA);
}

static inline uint8_t pci_cfg_reg_read_header_type(uint8_t bus, uint8_t dev, uint8_t func)
{
    return pci_cfg_reg_read_byte(bus, dev, func, PCI_HEADER_TYPE);
}

/****************************************************************************************************************
 * https://wiki.osdev.org/PCI
 * https://github.com/pciutils/pciutils
 * https://github.com/Johannes4Linux/pciutils/blob/master/pciheader.c
 *
 ***************************************************************************************************************/

// Struct to represent a bitfield in the PCI configuration space
struct config_space_bitfield {
    char name[64];
    unsigned int offset;
    unsigned int size;
};

// PCI Type 0 header
struct config_space_bitfield type_0_header[] = {
    {"Vendor ID",                0x0,    2},
    {"Device ID",                0x2,    2},
    {"Command",                  0x4,    2},
    {"Status",                   0x6,    2},
    {"Revision ID",              0x8,    1},
    {"Class Code",               0xA,    3},
    {"Cache Line S",             0xC,    1},
    {"Lat. Timer",               0xD,    1},
    {"Header Type",              0xE,    1},
    {"BIST",                     0xF,    1},
    {"BAR 0",                    0x10,   4},
    {"BAR 1",                    0x14,   4},
    {"BAR 2",                    0x18,   4},
    {"BAR 3",                    0x1C,   4},
    {"BAR 4",                    0x20,   4},
    {"BAR 5",                    0x24,   4},
    {"Cardbus CIS Pointer",      0x28,   4},
    {"Subsystem Vendor ID",      0x2C,   2},
    {"Subsystem ID",             0x2E,   2},
    {"Expansion ROM Address",    0x30,   4},
    {"Cap. Pointer",             0x34,   1},
    {"Reserved",                 0x35,   3},
    {"Reserved",                 0x38,   4},
    {"IRQ",                      0x3C,   1},
    {"IRQ Pin",                  0x3D,   1},
    {"Min Gnt.",                 0x3E,   1},
    {"Max Lat.",                 0x3F,   1},
    {"End",                      0x40,   5},
};

// PCI Type 1, PCI-to-PCI bridge, header
struct config_space_bitfield type_1_header[] = {
    {"Vendor ID",                0x0,    2},
    {"Device ID",                0x2,    2},
    {"Command",                  0x4,    2},
    {"Status",                   0x6,    2},
    {"Revision ID",              0x8,    1},
    {"Class Code",               0xA,    3},
    {"Cache Line S",             0xC,    1},
    {"Lat. Timer",               0xD,    1},
    {"Header Type",              0xE,    1},
    {"BIST",                     0xF,    1},
    {"BAR 0",                    0x10,   4},
    {"BAR 1",                    0x14,   4},
    {"Primary Bus",              0x18,   1},
    {"Secondary Bus",            0x19,   1},
    {"Sub. Bus",                 0x1A,   1},
    {"Sec Lat timer",            0x1B,   1},
    {"IO Base",                  0x1C,   1},
    {"IO Limit",                 0x1D,   1},
    {"Sec. Status",              0x1E,   2},
    {"Memory Limit",             0x20,   2},
    {"Memory Base",              0x22,   2},
    {"Pref. Memory Limit",       0x24,   2},
    {"Pref. Memory Base",        0x26,   2},
    {"Pref. Memory Base U",      0x28,   4},
    {"Pref. Memory Base L",      0x2C,   4},
    {"IO Base Upper",            0x30,   2},
    {"IO Limit Upper",           0x32,   2},
    {"Cap. Pointer",             0x34,   1},
    {"Reserved",                 0x35,   3},
    {"Exp. ROM Base Addr",       0x38,   4},
    {"IRQ Line",                 0x3C,   1},
    {"IRQ Pin",                  0x3D,   1},
    {"Min Gnt.",                 0x3E,   1},
    {"Max Lat.",                 0x3F,   1},
    {"End",                      0x40,   5},
};

struct config_space_bitfield *types[2] = {&type_0_header[0], &type_1_header[0]};

void int_2_hexstr(uint32_t value, unsigned int size, char *destination) {
    const char letters[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    unsigned int i;

    // Init string
    strcpy(destination, "0x");
    for(i=0; i<size; i++)
        strcat(destination, "00");
    i=2+2*size - 1;

    // Copy value in string
    while((value > 0) && (i>1)){
        destination[i] = letters[(value & 0xf)];
        value = value >> 4;
        i--;
    }
}

void print_pci_header(uint8_t bus, uint8_t dev, uint8_t func) {
    uint8_t  header_type = 0;
    uint32_t value, bf_value;
    uint64_t mask;
    unsigned int i, space_available, padding, bitfield = 0, bf2;
    int j;
    struct config_space_bitfield *ptr;
    char str_value[16];
    const char *ctypes[] = {"n Endpoint", " Bridge"};

    // Check if device is bridge or EP
    header_type = pci_cfg_reg_read_header_type(bus, dev, func);
    if (header_type !=0 && header_type != 1)
    {
        printf("Unknown PCI header type: %x\n", header_type);
        printf("Selected device %x:%x:%x is an unknown type\n", bus, dev, func);
        return;
    }

    ptr = types[header_type];
    printf("Selected device %x:%x:%x is a%s\n", bus, dev, func, ctypes[header_type]);

    // Read config space and dump it to console
    printf("|    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |    |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |\n");
    printf("|-----------------------------------------------------------|    |-----------------------------------------------------------|    Address\n");

    for (i=0; i<0x40; i+=4){
        bf2 = bitfield;
        // Print defintion of PCI header line
        putchar('|');
        while (ptr[bitfield].offset < i+4){
            space_available = 14 * ptr[bitfield].size + (ptr[bitfield].size -1);
            padding = (space_available - strlen(ptr[bitfield].name)) / 2;
            for (j=0; j<(int) padding; j++)
                putchar(' ');
            printf("%s", ptr[bitfield].name);
            for (j=(int) padding + strlen(ptr[bitfield].name); j<(int) space_available; j++)
                putchar(' ');
            putchar('|');
            bitfield++;
        }

        value = pci_cfg_reg_read_dword(bus, dev, func, i);

        // Print Values of PCI header line
        bitfield = bf2;
        printf("    |");
        while (ptr[bitfield].offset < i+4){
            if (ptr[bitfield].size == 5)
                break;

            // Extracting Bitfield of interest
            mask = ((1L<<(ptr[bitfield].size * 8))-1) << (8*(ptr[bitfield].offset - i));
            bf_value = (value & mask) >> (8*(ptr[bitfield].offset - i));

            // Print Bitfield and table
            space_available = 14 * ptr[bitfield].size + ptr[bitfield].size -1;
            padding = (space_available - ( 2 + ptr[bitfield].size)) / 2;
            for (j=0; j<(int) padding; j++)
                putchar(' ');

            int_2_hexstr(bf_value, ptr[bitfield].size, str_value);
            printf("%s", str_value);
            for (j=(int) padding+strlen(str_value); j<(int) space_available; j++)
                putchar(' ');
            putchar('|');
            bitfield++;
        }
        printf("    0x%02x\n", i);
        printf("|-----------------------------------------------------------|    |-----------------------------------------------------------|\n");
    }
}


int main(int argc, char *argv[])
{
    if( argc != 4 && argc != 5)
    {
        printf("Need 4 or 5 arguments, supplied: %d\n", argc);
        return -1;
    }

    uint8_t bus  = (uint8_t)strtol(argv[1], NULL, 0);
    uint8_t dev  = (uint8_t)strtol(argv[2], NULL, 0);
    uint8_t func = (uint8_t)strtol(argv[3], NULL, 0);  // argv[3] = 0x????

    printf("%s %x %x %x\n", argv[0], bus, dev, func);
    if ((bus > 255) || (dev > 31) || (func > 7))
    {
        printf("Bad inputs for bus|dev|func\n");;
        return -1;
    }

    // Request access to the ports; avoid general protection fault
    // Need root privileges
    if (ioperm(PCI_CFG_ADDR, 8, 1))
    {
        printf("Error requesting IO port access");
        return -1;
    }

    print_pci_header(bus, dev, func);

    if (argc == 5)
    {
        uint8_t reg = (uint8_t)strtol(argv[4], NULL, 0);
        printf("reg %02x: %08x\n", reg, pci_cfg_reg_read_dword(bus, dev, func, reg));
        printf("reg %02x: %04x\n", reg, pci_cfg_reg_read_word(bus, dev, func, reg));
        printf("reg %02x: %02x\n", reg, pci_cfg_reg_read_byte(bus, dev, func, reg));
    }

    if (ioperm(PCI_CFG_ADDR, 8, 0))
    {
        printf("Error requesting IO port access");
        return -1;
    }

    return 0;
}

/****************************************************************************************************************

$  lspci -v -d 10b5:1009
26:00.0 System peripheral: PLX Technology, Inc. Device 1009 (rev b0)
	Subsystem: PLX Technology, Inc. Device 9781
	Flags: bus master, fast devsel, latency 0, IRQ 35
	Memory at c2000000 (32-bit, non-prefetchable) [size=8M]
	Capabilities: [40] Power Management version 3
	Capabilities: [48] MSI-X: Enable- Count=32 Masked-
	Capabilities: [68] Express Endpoint, MSI 00
	Capabilities: [100] Single Root I/O Virtualization (SR-IOV)
	Capabilities: [fb4] Advanced Error Reporting
	Capabilities: [148] Virtual Channel
	Capabilities: [b70] Vendor Specific Information: ID=0001 Rev=0 Len=010 <?>

$ ./pci_header 0x26 0 0 0x0e
./pci_header 26 0 0
Selected device 26:0:0 is an Endpoint
|    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |    |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |
|-----------------------------------------------------------|    |-----------------------------------------------------------|    Address
|          Vendor ID          |          Device ID          |    |            0x10B5           |            0x1009           |    0x00
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|           Command           |           Status            |    |            0x0007           |            0x0010           |    0x04
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Revision ID  |                 Class Code                 |    |     0xB0     |                   0x000880                 |    0x08
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Cache Line S |  Lat. Timer  | Header Type  |     BIST     |    |     0x08     |     0x00     |     0x00     |     0x00     |    0x0c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 0                           |    |                          0xC2000000                       |    0x10
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 1                           |    |                          0x00000000                       |    0x14
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
|     Subsystem Vendor ID     |        Subsystem ID         |    |            0x10B5           |            0x9781           |    0x2c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                   Expansion ROM Address                   |    |                          0x00000000                       |    0x30
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Cap. Pointer |                  Reserved                  |    |     0x40     |                   0x000000                 |    0x34
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                         Reserved                          |    |                          0x00000000                       |    0x38
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|     IRQ      |   IRQ Pin    |   Min Gnt.   |   Max Lat.   |    |     0xFF     |     0x01     |     0x00     |     0x00     |    0x3c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
reg 0e: 00000008
reg 0e: 0000
reg 0e: 00

$ ./pci_header 0x17 0 0 0x0e
./pci_header 17 0 0
Selected device 17:0:0 is a Bridge
|    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |    |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |
|-----------------------------------------------------------|    |-----------------------------------------------------------|    Address
|          Vendor ID          |          Device ID          |    |            0x10B5           |            0x9781           |    0x00
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|           Command           |           Status            |    |            0x0547           |            0x0010           |    0x04
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Revision ID  |                 Class Code                 |    |     0xB0     |                   0x000604                 |    0x08
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Cache Line S |  Lat. Timer  | Header Type  |     BIST     |    |     0x08     |     0x00     |     0x01     |     0x00     |    0x0c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 0                           |    |                          0x00000000                       |    0x10
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                           BAR 1                           |    |                          0x00000000                       |    0x14
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Primary Bus  |Secondary Bus |   Sub. Bus   |Sec Lat timer |    |     0x17     |     0x18     |     0x26     |     0x00     |    0x18
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|   IO Base    |   IO Limit   |         Sec. Status         |    |     0xF1     |     0x01     |            0x0000           |    0x1c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|        Memory Limit         |         Memory Base         |    |            0xC200           |            0xC580           |    0x20
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|     Pref. Memory Limit      |      Pref. Memory Base      |    |            0xF001           |            0xFEF1           |    0x24
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                    Pref. Memory Base U                    |    |                          0x000000D7                       |    0x28
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                    Pref. Memory Base L                    |    |                          0x000000D7                       |    0x2c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|        IO Base Upper        |       IO Limit Upper        |    |            0x0000           |            0x0000           |    0x30
|-----------------------------------------------------------|    |-----------------------------------------------------------|
| Cap. Pointer |                  Reserved                  |    |     0x40     |                   0x000000                 |    0x34
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|                    Exp. ROM Base Addr                     |    |                          0x00000000                       |    0x38
|-----------------------------------------------------------|    |-----------------------------------------------------------|
|   IRQ Line   |   IRQ Pin    |   Min Gnt.   |   Max Lat.   |    |     0xFF     |     0x01     |     0x13     |     0x00     |    0x3c
|-----------------------------------------------------------|    |-----------------------------------------------------------|
reg 0e: 00010008
reg 0e: 0001
reg 0e: 01

****************************************************************************************************************/

