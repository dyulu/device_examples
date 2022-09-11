#include <linux/ioctl.h>

// _IOW means userland is writing and kernel is reading
// _IOR means userland is reading and kernel is writing.

typedef struct mydev_data
{
    uint8_t  data;
    uint32_t offset;
} mydev_data_t;

#define DEV_NAME      "my-dev"
#define MY_DEV_READ   _IOR('F', 0, mydev_data_t)
#define MY_DEV_WRITE  _IOW('F', 1, mydev_data_t)

