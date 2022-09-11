/****************************************************************************************************************
 * https://docs.kernel.org/PCI/pci.html
 *
 * Begin or continue searching for a PCI device by vendor/device id.
 * @from: Previous PCI device found in search, or %NULL for new search.
 *    struct pci_dev *pci_get_device(unsigned int vendor, unsigned int device, struct pci_dev *from)
 *
 * Initialize device before it's used by a driver. 
 * Ask low-level code to enable I/O and memory. Wake up the device if it was suspended.
 *    int pci_enable_device(struct pci_dev *dev)
 *
 * Mark the PCI region associated with PCI device @pdev BAR @bar as being reserved by owner @res_name.
 * Returns 0 on success, or %EBUSY on error.  A warning message is also printed on failure.
 *     int pci_request_region(struct pci_dev *pdev, int bar, const char *res_name)
 *
 * Release a PCI bar
 *     void pci_release_region(struct pci_dev *pdev, int bar)
 *
 * Create a virtual mapping cookie for a PCI BAR.
 * @maxlen specifies the maximum length to map. If you want to get access to the complete BAR without checking for
 *         its length first, pass %0 here.
 *     void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
 *
 *     void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
 *
 ***************************************************************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/kernel.h>

// lspci -v -d 10b5:1009 | grep Memory
// lspci -vvvt -d 10b5:

#define VENDOR_ID 0x10b5
#define DEVICE_ID 0x9781   // 1009
#define BAR0_ID   0

#define DRV_NAME    "my-dev-drv"

static struct pci_dev *my_pci_dev      = NULL;
static void __iomem   *my_pci_dev_bar0 = NULL; 


void print_pci_header(struct pci_dev *pdev);

static int __init my_dev_init(void)
{
//    uint16_t       val;
    pci_bus_addr_t bar0_size;

    pr_info(DRV_NAME "my_dev_init\n");

    my_pci_dev = pci_get_device(VENDOR_ID, DEVICE_ID, NULL);
    if (!my_pci_dev)
    {
        pr_err(DRV_NAME ": PCI adaptor not available\n");
        return -1;
    }

    if (pci_enable_device(my_pci_dev))
    {
        pr_err(DRV_NAME ": PCI adaptor cannot be enabled\n");
        return -1;
    }

/*
    pci_read_config_word(my_pci_dev, PCI_VENDOR_ID, &val);
    pr_info("VENDOR ID: 0x%x\n", val);
    pci_read_config_word(my_pci_dev, PCI_DEVICE_ID, &val);
    pr_info("DEVICE ID: 0x%x\n", val);
*/

    print_pci_header(my_pci_dev);

    // Request and map BAR0
    if (pci_request_region(my_pci_dev, BAR0_ID, DRV_NAME))
    {
        pr_err(DRV_NAME ": cannot request BAR0\n");
        return -1;
    }

    bar0_size = pci_resource_len(my_pci_dev, BAR0_ID);
    my_pci_dev_bar0 = pci_iomap(my_pci_dev, BAR0_ID, bar0_size);
    pr_info(DRV_NAME ": my_pci_dev_bar0:%p, size:%lld\n", my_pci_dev_bar0, bar0_size);

    pr_info(DRV_NAME ": my_dev_init done\n");
    return 0;
}

static void __exit my_dev_exit(void)
{
    pr_info(DRV_NAME ": my_dev_exit\n");
    pci_iounmap(my_pci_dev, my_pci_dev_bar0);
    pci_release_region(my_pci_dev, BAR0_ID);
}

module_init(my_dev_init);
module_exit(my_dev_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("Example PCIe switch driver");
MODULE_AUTHOR("dyulu <dyulu@example.com>");

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

void int_2_hexstr(u32 value, unsigned int size, char *destination) {
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

void print_pci_header(struct pci_dev *pdev) {
    u8  header_type = 0;
    u32 value, bf_value;
    u64 mask;
    unsigned int i, space_available, padding, bitfield = 0, bf2;
    int j;
    struct config_space_bitfield *ptr;
    char str_value[16];
    const char *ctypes[] = {"n Endpoint", " Bridge"};

    if (pdev == NULL)
        return;

    // Check if device is bridge or EP
    // pci_read_config_byte(pdev, PCI_HEADER_TYPE, &header_type);
    header_type = pdev->hdr_type;
    ptr = types[header_type];

    pr_info("Selected device %x:%x:%x is a%s\n", pdev->bus->number, pdev->devfn & 0xF8, pdev->devfn & 0x07, ctypes[header_type]);

    // Read config space and dump it to console
    pr_info("|    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |    |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |\n");
    pr_info("|-----------------------------------------------------------|    |-----------------------------------------------------------|    Address\n");

    for (i=0; i<0x40; i+=4){
        bf2 = bitfield;
        // Print defintion of PCI header line
        pr_info("|");
        while (ptr[bitfield].offset < i+4){
            space_available = 14 * ptr[bitfield].size + (ptr[bitfield].size -1);
            padding = (space_available - strlen(ptr[bitfield].name)) / 2;
            for (j=0; j<(int) padding; j++)
                pr_cont(" ");
            pr_cont("%s", ptr[bitfield].name);
            for (j=(int) padding + strlen(ptr[bitfield].name); j<(int) space_available; j++)
                pr_cont(" ");
            pr_cont("|");
            bitfield++;
        }

        pci_read_config_dword(pdev, i, &value);

        // Print Values of PCI header line
        bitfield = bf2;
        pr_cont("    |");
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
                pr_cont(" ");

            int_2_hexstr(bf_value, ptr[bitfield].size, str_value);
            pr_cont("%s", str_value);
            for (j=(int) padding+strlen(str_value); j<(int) space_available; j++)
                pr_cont(" ");
            pr_cont("|");
            bitfield++;
        }
        pr_cont("    0x%02x", i);
        pr_info("|-----------------------------------------------------------|    |-----------------------------------------------------------|\n");
    }
}

/****************************************************************************************************************
 * PCI IDs:        https://pci-ids.ucw.cz/
 * Vendors:        https://pci-ids.ucw.cz/read/PC/
 *     e.g., 10B5: PLX Technology, Inc.
 * Device classes: https://pci-ids.ucw.cz/read/PD/
 *     e.g., 880: Generic system peripheral, System peripheral
 *           604: Bridge, PCI bridge
 *
 ***************************************************************************************************************/

/****************************************************************************************************************

$ modprobe switch_dev

[98952.861471] my-dev-drvmy_dev_init
[98952.864811] Selected device 26:0:0 is an Endpoint
[98952.869501] |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |    |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |
[98952.881877] |-----------------------------------------------------------|    |-----------------------------------------------------------|    Address
[98952.895206] |          Vendor ID          |          Device ID          |    |            0x10B5           |            0x1009           |    0x00
[98952.895237] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98952.920688] |           Command           |           Status            |    |            0x0007           |            0x0010           |    0x04
[98952.920719] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98952.946158] | Revision ID  |                 Class Code                 |    |     0xB0     |                   0x000880                 |    0x08
[98952.946187] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98952.971637] | Cache Line S |  Lat. Timer  | Header Type  |     BIST     |    |     0x08     |     0x00     |     0x00     |     0x00     |    0x0c
[98952.971662] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98952.997108] |                           BAR 0                           |    |                          0xC2000000                       |    0x10
[98952.997142] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.022588] |                           BAR 1                           |    |                          0x00000000                       |    0x14
[98953.022621] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.048067] |                           BAR 2                           |    |                          0x00000000                       |    0x18
[98953.048100] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.073538] |                           BAR 3                           |    |                          0x00000000                       |    0x1c
[98953.073572] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.099023] |                           BAR 4                           |    |                          0x00000000                       |    0x20
[98953.099056] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.124505] |                           BAR 5                           |    |                          0x00000000                       |    0x24
[98953.124538] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.149984] |                    Cardbus CIS Pointer                    |    |                          0x00000000                       |    0x28
[98953.150013] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.175461] |     Subsystem Vendor ID     |        Subsystem ID         |    |            0x10B5           |            0x9781           |    0x2c
[98953.175487] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.200932] |                   Expansion ROM Address                   |    |                          0x00000000                       |    0x30
[98953.200961] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.226404] | Cap. Pointer |                  Reserved                  |    |     0x40     |                   0x000000                 |    0x34
[98953.226433] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.251876] |                         Reserved                          |    |                          0x00000000                       |    0x38
[98953.251908] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.277345] |     IRQ      |   IRQ Pin    |   Min Gnt.   |   Max Lat.   |    |     0xFF     |     0x01     |     0x00     |     0x00     |    0x3c
[98953.277373] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[98953.304629] my-dev-drv: my_pci_dev_bar0:000000004e78d58e, size:8388608
[98953.311149] my-dev-drv: my_dev_init done


# For 10b5:9781,

[99317.745734] Selected device 17:0:0 is a Bridge
[99317.750168] |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |    |    Byte 0    |   Byte 1     |    Byte 2    |    Byte 3    |
[99317.762546] |-----------------------------------------------------------|    |-----------------------------------------------------------|    Address
[99317.775877] |          Vendor ID          |          Device ID          |    |            0x10B5           |            0x9781           |    0x00
[99317.775908] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99317.801349] |           Command           |           Status            |    |            0x0547           |            0x0010           |    0x04
[99317.801380] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99317.826825] | Revision ID  |                 Class Code                 |    |     0xB0     |                   0x000604                 |    0x08
[99317.826854] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99317.852304] | Cache Line S |  Lat. Timer  | Header Type  |     BIST     |    |     0x08     |     0x00     |     0x01     |     0x00     |    0x0c
[99317.852329] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99317.877768] |                           BAR 0                           |    |                          0x00000000                       |    0x10
[99317.877802] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99317.903246] |                           BAR 1                           |    |                          0x00000000                       |    0x14
[99317.903280] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99317.928717] | Primary Bus  |Secondary Bus |   Sub. Bus   |Sec Lat timer |    |     0x17     |     0x18     |     0x26     |     0x00     |    0x18
[99317.928739] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99317.954178] |   IO Base    |   IO Limit   |         Sec. Status         |    |     0xF1     |     0x01     |            0x0000           |    0x1c
[99317.954206] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99317.979658] |        Memory Limit         |         Memory Base         |    |            0xC200           |            0xC580           |    0x20
[99317.979686] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99318.005131] |     Pref. Memory Limit      |      Pref. Memory Base      |    |            0xF001           |            0xFEF1           |    0x24
[99318.005155] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99318.030601] |                    Pref. Memory Base U                    |    |                          0x000000D7                       |    0x28
[99318.030630] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99318.056069] |                    Pref. Memory Base L                    |    |                          0x000000D7                       |    0x2c
[99318.056099] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99318.081551] |        IO Base Upper        |       IO Limit Upper        |    |            0x0000           |            0x0000           |    0x30
[99318.081579] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99318.107030] | Cap. Pointer |                  Reserved                  |    |     0x40     |                   0x000000                 |    0x34
[99318.107059] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99318.132508] |                    Exp. ROM Base Addr                     |    |                          0x00000000                       |    0x38
[99318.132538] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99318.157980] |   IRQ Line   |   IRQ Pin    |   Min Gnt.   |   Max Lat.   |    |     0xFF     |     0x01     |     0x13     |     0x00     |    0x3c
[99318.158007] |-----------------------------------------------------------|    |-----------------------------------------------------------|
[99318.183453] my-dev-drv: my_pci_dev_bar0:0000000000000000, size:0

****************************************************************************************************************/
