#include "pflash.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    CFI_READ_ARRAY  = 0xff,
    CFI_READ_ID     = 0x90,
    CFI_QUERY       = 0x98,
    CFI_READ_STATUS = 0x70,
    CFI_WORD_PROG   = 0x40,
    CFI_BUF_WRITE   = 0xe8,
    CFI_BLOCK_ERASE = 0x20,
    CFI_LOCK_SETUP  = 0x60,
};

static void persist(HvPflash *f, uint64_t off, uint64_t len) {
    if (f->fd < 0 || f->readonly || !len) return;
    if (off + len > f->size) len = f->size - off;
    (void)pwrite(f->fd, f->data + off, (size_t)len, (off_t)off);
}

static uint64_t pflash_read(void *opaque, uint64_t off, unsigned size) {
    HvPflash *f = opaque;
    if (off + size > f->size) return ~0ull;

    if (f->cmd == CFI_READ_ARRAY) {
        uint64_t v = 0;
        memcpy(&v, f->data + off, size);
        return v;
    }

    uint32_t word = 0;
    if (f->cmd == CFI_READ_STATUS || f->cmd == CFI_BUF_WRITE) {
        uint32_t st = f->status;
        word = st | (st << 16);
    } else if (f->cmd == CFI_READ_ID) {
        uint32_t idx = (uint32_t)((off >> 2) & 0x3f);
        if (idx == 0) word = 0x00890089u;      /* Intel manufacturer ID */
        else if (idx == 1) word = 0x00180018u; /* Device ID */
        else word = 0x00000000u;               /* Block lock status: unlocked */
    } else if (f->cmd == CFI_QUERY) {
        uint32_t idx = (uint32_t)((off >> 2) & 0xff);
        uint32_t b = idx < sizeof(f->cfi) ? f->cfi[idx] : 0;
        word = b | (b << 16);
    }

    uint64_t shifted = word >> ((off & 3) * 8);
    if (size == 8) shifted |= (uint64_t)word << 32;
    return size >= 8 ? shifted : shifted & ((1ull << (size * 8)) - 1);
}

static void pflash_write(void *opaque, uint64_t off, uint64_t val, unsigned size) {
    HvPflash *f = opaque;
    if (f->readonly || off >= f->size) return;

    uint8_t c = (uint8_t)(val & 0xff);

    /* Multi-cycle operations in progress */
    if (f->wcycle > 0) {
        if (f->cmd == CFI_WORD_PROG) {
            unsigned n = off + size <= f->size ? size : (unsigned)(f->size - off);
            for (unsigned i = 0; i < n; i++)
                f->data[off + i] &= (uint8_t)(val >> (i * 8));
            persist(f, off, n);
            f->status = 0x80;
            f->cmd = CFI_READ_STATUS;
            f->wcycle = 0;
            return;
        }
        if (f->cmd == CFI_BUF_WRITE) {
            if (f->wcycle == 1) {
                /* Word count minus 1 (in bank-width 32-bit transfers) */
                f->wcount = (uint32_t)(val & 0xffff) + 1;
                f->wcycle = 2;
                return;
            }
            if (f->wcount > 0) {
                unsigned n = off + size <= f->size ? size : (unsigned)(f->size - off);
                for (unsigned i = 0; i < n; i++)
                    f->data[off + i] &= (uint8_t)(val >> (i * 8));
                if (off < f->wbase) f->wbase = off;
                f->wcount--;
                if (f->wcount == 0) f->wcycle = 3; /* expect 0xd0 confirm */
                return;
            }
            /* wcycle == 3: confirm 0xd0 */
            uint64_t sec = off & ~((uint64_t)HV_PFLASH_SECTOR - 1);
            persist(f, sec, HV_PFLASH_SECTOR);
            f->status = 0x80;
            f->cmd = CFI_READ_STATUS;
            f->wcycle = 0;
            return;
        }
        if (f->cmd == CFI_BLOCK_ERASE) {
            if (c == 0xd0) {
                uint64_t sec = off & ~((uint64_t)HV_PFLASH_SECTOR - 1);
                memset(f->data + sec, 0xff, HV_PFLASH_SECTOR);
                persist(f, sec, HV_PFLASH_SECTOR);
                f->status = 0x80;
                f->cmd = CFI_READ_STATUS;
            } else {
                f->cmd = CFI_READ_ARRAY;
            }
            f->wcycle = 0;
            return;
        }
        if (f->cmd == CFI_LOCK_SETUP) {
            /* 0x01 lock, 0xd0 unlock, 0x2f lock-down: accept all as unlocked/ready */
            f->status = 0x80;
            f->cmd = (c == 0x01 || c == 0xd0 || c == 0x2f) ? CFI_READ_STATUS : CFI_READ_ARRAY;
            f->wcycle = 0;
            return;
        }
        f->wcycle = 0;
    }

    /* Cycle 0 command decode */
    switch (c) {
    case 0xff:
    case 0xf0:
        f->cmd = CFI_READ_ARRAY;
        break;
    case 0x70:
        f->cmd = CFI_READ_STATUS;
        break;
    case 0x50:
        f->status = 0x80;
        f->cmd = CFI_READ_ARRAY;
        break;
    case 0x90:
        f->cmd = CFI_READ_ID;
        break;
    case 0x98:
        f->cmd = CFI_QUERY;
        break;
    case 0x40:
    case 0x10:
        f->cmd = CFI_WORD_PROG;
        f->wcycle = 1;
        break;
    case 0xe8:
        f->cmd = CFI_BUF_WRITE;
        f->status = 0x80;
        f->wbase = off & ~((uint64_t)HV_PFLASH_SECTOR - 1);
        f->wcycle = 1;
        break;
    case 0x20:
        f->cmd = CFI_BLOCK_ERASE;
        f->wcycle = 1;
        break;
    case 0x60:
        f->cmd = CFI_LOCK_SETUP;
        f->wcycle = 1;
        break;
    default:
        f->cmd = CFI_READ_ARRAY;
        break;
    }
}

static void init_cfi_table(HvPflash *f) {
    memset(f->cfi, 0, sizeof f->cfi);
    /* Query unique ASCII string "QRY" */
    f->cfi[0x10] = 'Q';
    f->cfi[0x11] = 'R';
    f->cfi[0x12] = 'Y';
    /* Primary vendor command set: Intel/Sharp Extended (0x0001) */
    f->cfi[0x13] = 0x01;
    f->cfi[0x14] = 0x00;
    f->cfi[0x15] = 0x31;
    f->cfi[0x16] = 0x00;
    /* Voltage / timing info */
    f->cfi[0x1b] = 0x45;
    f->cfi[0x1c] = 0x55;
    f->cfi[0x1f] = 0x07;
    f->cfi[0x20] = 0x07;
    f->cfi[0x21] = 0x0a;
    f->cfi[0x22] = 0x00;
    f->cfi[0x23] = 0x04;
    f->cfi[0x24] = 0x04;
    f->cfi[0x25] = 0x04;
    /* Device geometry: two interleaved 32 MB x16 chips = 64 MB bank, 256 x 256 KB sectors */
    f->cfi[0x27] = 25;   /* 2^25 = 32 MB per chip */
    f->cfi[0x28] = 0x02; /* x8/x16 interface */
    f->cfi[0x29] = 0x00;
    f->cfi[0x2a] = 0x0b; /* max buffer write = 2^11 = 2048 B per chip (4096 B interleaved) */
    f->cfi[0x2b] = 0x00;
    f->cfi[0x2c] = 0x01; /* 1 erase block region */
    uint16_t nblocks_m1 = (uint16_t)(f->size / HV_PFLASH_SECTOR - 1);
    uint16_t chip_sec_256 = (uint16_t)((HV_PFLASH_SECTOR / 2) / 256);
    f->cfi[0x2d] = (uint8_t)(nblocks_m1 & 0xff);
    f->cfi[0x2e] = (uint8_t)(nblocks_m1 >> 8);
    f->cfi[0x2f] = (uint8_t)(chip_sec_256 & 0xff);
    f->cfi[0x30] = (uint8_t)(chip_sec_256 >> 8);
    /* Primary extended table "PRI" at 0x31 */
    f->cfi[0x31] = 'P';
    f->cfi[0x32] = 'R';
    f->cfi[0x33] = 'I';
    f->cfi[0x34] = '1';
    f->cfi[0x35] = '0';
}

HvPflash *hv_pflash_create(HvMachine *m, uint64_t pa, const char *path, int readonly) {
    int flags = readonly ? O_RDONLY : O_RDWR;
    int fd = open(path, flags);
    if (fd < 0 && !readonly) fd = open(path, O_RDONLY), readonly = 1;
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }

    HvPflash *f = calloc(1, sizeof *f);
    f->m = m;
    f->pa = pa;
    f->size = HV_PFLASH_SIZE;
    f->fd = fd;
    f->readonly = readonly;
    f->cmd = CFI_READ_ARRAY;
    f->status = 0x80;

    void *buf = mmap(NULL, (size_t)f->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buf == MAP_FAILED) {
        close(fd);
        free(f);
        return NULL;
    }
    f->data = buf;
    memset(f->data, 0xff, (size_t)f->size); /* erased NOR flash default */

    size_t to_read = (uint64_t)st.st_size < f->size ? (size_t)st.st_size : (size_t)f->size;
    size_t got = 0;
    while (got < to_read) {
        ssize_t n = pread(fd, f->data + got, to_read - got, (off_t)got);
        if (n <= 0) break;
        got += (size_t)n;
    }

    init_cfi_table(f);

    if (readonly) {
        close(f->fd);
        f->fd = -1;
        hv_machine_add_ram(m, pa, f->size, f->data, 1);
    } else {
        hv_machine_add_mmio(m, pa, f->size, f, pflash_read, pflash_write, "pflash-vars");
    }
    return f;
}

void hv_pflash_close(HvPflash *f) {
    if (!f) return;
    if (f->fd >= 0) {
        fsync(f->fd);
        close(f->fd);
        f->fd = -1;
    }
    if (f->data) {
        munmap(f->data, (size_t)f->size);
        f->data = NULL;
    }
    free(f);
}
