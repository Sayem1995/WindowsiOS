#ifndef WINVM_FW_CFG_H
#define WINVM_FW_CFG_H

#include <stddef.h>
#include <stdint.h>

#include "machine.h"

#define HV_FW_CFG_MAX_FILES 32

typedef void (*HvFwCfgWriteCb)(void *opaque, const uint8_t *data, uint32_t len);

typedef struct HvFwCfg {
    HvMachine *m;
    uint16_t cur_sel;
    uint32_t cur_off;
    uint32_t dma_hi;

    /* Standard fixed keys (0x0000..0x001f) */
    uint8_t fixed[0x20][64];
    uint32_t fixed_len[0x20];

    /* File directory (selector 0x0019, files at 0x0020 + i) */
    uint8_t *dir_blob;
    uint32_t dir_len;
    int nfiles;
    struct {
        char name[56];
        uint8_t *data;
        uint32_t len;
        int writable;
        HvFwCfgWriteCb on_write;
        void *write_opaque;
    } files[HV_FW_CFG_MAX_FILES];
} HvFwCfg;

HvFwCfg *hv_fw_cfg_create(HvMachine *m, uint64_t pa, uint16_t ncpus);
/* Add a read-only named file (copies `data`). */
int hv_fw_cfg_add_file(HvFwCfg *fc, const char *name, const void *data, uint32_t len);
/* Add a guest-writable named file (e.g. "etc/ramfb") with a callback fired when DMA writes it. */
int hv_fw_cfg_add_writable_file(HvFwCfg *fc, const char *name, uint32_t len, HvFwCfgWriteCb cb, void *opaque);

#endif
