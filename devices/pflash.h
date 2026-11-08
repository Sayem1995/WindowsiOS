#ifndef WINVM_PFLASH_H
#define WINVM_PFLASH_H

#include <stdint.h>

#include "machine.h"

#define HV_PFLASH_SIZE (64ull << 20)      /* 64 MB per flash bank on QEMU virt */
#define HV_PFLASH_SECTOR (256u << 10)     /* 256 KB interleaved sector (2 x 128 KB x16 chips) */

typedef struct HvPflash {
    HvMachine *m;
    uint64_t pa;
    uint64_t size;
    uint8_t *data;
    int fd;
    int readonly;
    /* CFI01 state machine */
    int cmd;
    int wcycle;
    uint32_t wcount;
    uint64_t wbase;
    uint8_t status;
    uint8_t cfi[0x60];
} HvPflash;

/* Create a 64 MB flash bank at `pa`.
   If `readonly` is non-zero (e.g. pflash0 UEFI code), maps directly as read-only HvRam
   so the ARM64 JIT executes firmware in place at native speed.
   If `readonly` is 0 (e.g. pflash1 UEFI NVRAM vars), registers CFI01 MMIO and persists
   writes/erases back to `path`. */
HvPflash *hv_pflash_create(HvMachine *m, uint64_t pa, const char *path, int readonly);
void hv_pflash_close(HvPflash *f);

#endif
