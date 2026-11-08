/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "fdt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FDT_MAGIC      0xd00dfeedU
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE   2U
#define FDT_PROP       3U
#define FDT_NOP        4U
#define FDT_END        9U

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} Buf;

static void buf_grow(Buf *b, size_t add) {
    if (b->len + add <= b->cap) return;
    size_t ncap = b->cap ? b->cap * 2 : 4096;
    while (ncap < b->len + add) ncap *= 2;
    uint8_t *p = realloc(b->buf, ncap);
    if (!p) abort();
    b->buf = p;
    b->cap = ncap;
}

static void buf_put(Buf *b, const void *src, size_t n) {
    buf_grow(b, n);
    memcpy(b->buf + b->len, src, n);
    b->len += n;
}

static void buf_align4(Buf *b) {
    while (b->len & 3) {
        uint8_t z = 0;
        buf_put(b, &z, 1);
    }
}

static void buf_u32(Buf *b, uint32_t v) {
    uint32_t be = __builtin_bswap32(v);
    buf_put(b, &be, 4);
}

static uint32_t str_off(Buf *strs, const char *name) {
    size_t nlen = strlen(name) + 1;
    size_t i = 0;
    while (i < strs->len) {
        const char *s = (const char *)(strs->buf + i);
        size_t slen = strlen(s) + 1;
        if (slen == nlen && memcmp(s, name, nlen) == 0) return (uint32_t)i;
        i += slen;
    }
    uint32_t off = (uint32_t)strs->len;
    buf_put(strs, name, nlen);
    return off;
}

static void fdt_begin_node(Buf *st, const char *name) {
    buf_u32(st, FDT_BEGIN_NODE);
    buf_put(st, name, strlen(name) + 1);
    buf_align4(st);
}

static void fdt_end_node(Buf *st) {
    buf_u32(st, FDT_END_NODE);
}

static void fdt_prop(Buf *st, Buf *strs, const char *name, const void *data, uint32_t len) {
    buf_u32(st, FDT_PROP);
    buf_u32(st, len);
    buf_u32(st, str_off(strs, name));
    if (len) buf_put(st, data, len);
    buf_align4(st);
}

static void fdt_prop_empty(Buf *st, Buf *strs, const char *name) {
    fdt_prop(st, strs, name, NULL, 0);
}

static void fdt_prop_u32(Buf *st, Buf *strs, const char *name, uint32_t v) {
    uint32_t be = __builtin_bswap32(v);
    fdt_prop(st, strs, name, &be, 4);
}

static void fdt_prop_cells(Buf *st, Buf *strs, const char *name, const uint32_t *cells, size_t count) {
    uint32_t tmp[256];
    if (count > 256) abort();
    for (size_t i = 0; i < count; i++) tmp[i] = __builtin_bswap32(cells[i]);
    fdt_prop(st, strs, name, tmp, (uint32_t)(count * 4));
}

static void fdt_prop_str(Buf *st, Buf *strs, const char *name, const char *s) {
    fdt_prop(st, strs, name, s, (uint32_t)(strlen(s) + 1));
}

static void fdt_prop_strlist(Buf *st, Buf *strs, const char *name, const char *const *list, size_t count) {
    Buf tmp = {0};
    for (size_t i = 0; i < count; i++) buf_put(&tmp, list[i], strlen(list[i]) + 1);
    fdt_prop(st, strs, name, tmp.buf, (uint32_t)tmp.len);
    free(tmp.buf);
}

#define PHANDLE_CLK  0x8000U
#define PHANDLE_GIC  0x8005U
#define PHANDLE_GPIO 0x8006U
#define PHANDLE_V2M  0x8007U
#define PHANDLE_CPU0 0x8100U

uint8_t *hv_fdt_build_virt(int ncpus, uint64_t ram_base, uint64_t ram_size, size_t *out_size) {
    if (ncpus < 1 || ncpus > 64) return NULL;

    Buf st = {0}, strs = {0};

    fdt_begin_node(&st, "");
    fdt_prop_u32(&st, &strs, "interrupt-parent", PHANDLE_GIC);
    fdt_prop_empty(&st, &strs, "dma-coherent");
    fdt_prop_str(&st, &strs, "model", "linux,dummy-virt");
    fdt_prop_u32(&st, &strs, "#size-cells", 2);
    fdt_prop_u32(&st, &strs, "#address-cells", 2);
    fdt_prop_str(&st, &strs, "compatible", "linux,dummy-virt");

    /* /psci */
    fdt_begin_node(&st, "psci");
    fdt_prop_u32(&st, &strs, "migrate", 0xc4000005U);
    fdt_prop_u32(&st, &strs, "cpu_on", 0xc4000003U);
    fdt_prop_u32(&st, &strs, "cpu_off", 0x84000002U);
    fdt_prop_u32(&st, &strs, "cpu_suspend", 0xc4000001U);
    fdt_prop_str(&st, &strs, "method", "hvc");
    {
        const char *const compat[] = {"arm,psci-1.0", "arm,psci-0.2", "arm,psci"};
        fdt_prop_strlist(&st, &strs, "compatible", compat, 3);
    }
    fdt_end_node(&st);

    /* /memory@40000000 */
    {
        char nodename[48];
        snprintf(nodename, sizeof nodename, "memory@%llx", (unsigned long long)ram_base);
        fdt_begin_node(&st, nodename);
        uint32_t reg[4] = {
            (uint32_t)(ram_base >> 32), (uint32_t)ram_base,
            (uint32_t)(ram_size >> 32), (uint32_t)ram_size,
        };
        fdt_prop_cells(&st, &strs, "reg", reg, 4);
        fdt_prop_str(&st, &strs, "device_type", "memory");
        fdt_end_node(&st);
    }

    /* /fw-cfg@9020000 */
    fdt_begin_node(&st, "fw-cfg@9020000");
    fdt_prop_empty(&st, &strs, "dma-coherent");
    {
        uint32_t reg[4] = {0, 0x09020000U, 0, 0x18U};
        fdt_prop_cells(&st, &strs, "reg", reg, 4);
    }
    fdt_prop_str(&st, &strs, "compatible", "qemu,fw-cfg-mmio");
    fdt_end_node(&st);

    /* /gpio-keys & /pl061@9030000 */
    fdt_begin_node(&st, "gpio-keys");
    fdt_prop_str(&st, &strs, "compatible", "gpio-keys");
    fdt_begin_node(&st, "poweroff");
    {
        uint32_t gpios[3] = {PHANDLE_GPIO, 3, 0};
        fdt_prop_cells(&st, &strs, "gpios", gpios, 3);
    }
    fdt_prop_u32(&st, &strs, "linux,code", 0x74U);
    fdt_prop_str(&st, &strs, "label", "GPIO Key Poweroff");
    fdt_end_node(&st);
    fdt_end_node(&st);

    fdt_begin_node(&st, "pl061@9030000");
    fdt_prop_u32(&st, &strs, "phandle", PHANDLE_GPIO);
    fdt_prop_str(&st, &strs, "clock-names", "apb_pclk");
    fdt_prop_u32(&st, &strs, "clocks", PHANDLE_CLK);
    {
        uint32_t irq[3] = {0, 7, 4};
        uint32_t reg[4] = {0, 0x09030000U, 0, 0x1000U};
        fdt_prop_cells(&st, &strs, "interrupts", irq, 3);
        fdt_prop_empty(&st, &strs, "gpio-controller");
        fdt_prop_u32(&st, &strs, "#gpio-cells", 2);
        const char *const compat[] = {"arm,pl061", "arm,primecell"};
        fdt_prop_strlist(&st, &strs, "compatible", compat, 2);
        fdt_prop_cells(&st, &strs, "reg", reg, 4);
    }
    fdt_end_node(&st);

    /* /pcie@10000000 */
    fdt_begin_node(&st, "pcie@10000000");
    {
        uint32_t imap_mask[4] = {0x1800U, 0, 0, 7};
        uint32_t imap[16 * 10];
        size_t idx = 0;
        for (uint32_t slot = 0; slot < 4; slot++) {
            for (uint32_t pin = 1; pin <= 4; pin++) {
                uint32_t spi = 3U + ((slot + pin - 1U) & 3U);
                imap[idx++] = slot << 11;
                imap[idx++] = 0;
                imap[idx++] = 0;
                imap[idx++] = pin;
                imap[idx++] = PHANDLE_GIC;
                imap[idx++] = 0;
                imap[idx++] = 0;
                imap[idx++] = 0;
                imap[idx++] = spi;
                imap[idx++] = 4;
            }
        }
        fdt_prop_cells(&st, &strs, "interrupt-map-mask", imap_mask, 4);
        fdt_prop_cells(&st, &strs, "interrupt-map", imap, idx);
        fdt_prop_u32(&st, &strs, "#interrupt-cells", 1);

        uint32_t msi_map[4] = {0, PHANDLE_V2M, 0, 0x10000U};
        fdt_prop_cells(&st, &strs, "msi-map", msi_map, 4);

        uint32_t ranges[21] = {
            /* I/O space: 0x00000000 -> CPU 0x3eff0000 (64 KB) */
            0x01000000U, 0, 0x00000000U, 0, 0x3eff0000U, 0, 0x00010000U,
            /* 32-bit MMIO: 0x10000000 -> CPU 0x10000000 (0x2eff0000 bytes) */
            0x02000000U, 0, 0x10000000U, 0, 0x10000000U, 0, 0x2eff0000U,
            /* 64-bit MMIO: 0x80_00000000 -> CPU 0x80_00000000 (512 GB) */
            0x03000000U, 0x80U, 0x00000000U, 0x80U, 0x00000000U, 0x80U, 0x00000000U,
        };
        fdt_prop_cells(&st, &strs, "ranges", ranges, 21);

        /* High ECAM at 0x40_10000000 (256 MB for buses 0x00..0xff) */
        uint32_t reg[4] = {0x40U, 0x10000000U, 0, 0x10000000U};
        fdt_prop_cells(&st, &strs, "reg", reg, 4);
        fdt_prop_empty(&st, &strs, "dma-coherent");

        uint32_t bus_range[2] = {0x00U, 0xffU};
        fdt_prop_cells(&st, &strs, "bus-range", bus_range, 2);
        fdt_prop_u32(&st, &strs, "linux,pci-domain", 0);
        fdt_prop_u32(&st, &strs, "#size-cells", 2);
        fdt_prop_u32(&st, &strs, "#address-cells", 3);
        fdt_prop_str(&st, &strs, "device_type", "pci");
        fdt_prop_str(&st, &strs, "compatible", "pci-host-ecam-generic");
    }
    fdt_end_node(&st);

    /* /pl031@9010000 */
    fdt_begin_node(&st, "pl031@9010000");
    fdt_prop_str(&st, &strs, "clock-names", "apb_pclk");
    fdt_prop_u32(&st, &strs, "clocks", PHANDLE_CLK);
    {
        uint32_t irq[3] = {0, 2, 4};
        uint32_t reg[4] = {0, 0x09010000U, 0, 0x1000U};
        fdt_prop_cells(&st, &strs, "interrupts", irq, 3);
        fdt_prop_cells(&st, &strs, "reg", reg, 4);
        const char *const compat[] = {"arm,pl031", "arm,primecell"};
        fdt_prop_strlist(&st, &strs, "compatible", compat, 2);
    }
    fdt_end_node(&st);

    /* /pl011@9000000 */
    fdt_begin_node(&st, "pl011@9000000");
    {
        const char *const cnames[] = {"uartclk", "apb_pclk"};
        uint32_t clks[2] = {PHANDLE_CLK, PHANDLE_CLK};
        uint32_t irq[3] = {0, 1, 4};
        uint32_t reg[4] = {0, 0x09000000U, 0, 0x1000U};
        const char *const compat[] = {"arm,pl011", "arm,primecell"};
        fdt_prop_strlist(&st, &strs, "clock-names", cnames, 2);
        fdt_prop_cells(&st, &strs, "clocks", clks, 2);
        fdt_prop_cells(&st, &strs, "interrupts", irq, 3);
        fdt_prop_cells(&st, &strs, "reg", reg, 4);
        fdt_prop_strlist(&st, &strs, "compatible", compat, 2);
    }
    fdt_end_node(&st);

    /* /intc@8000000 (GICv3 + GICv2m frame) */
    fdt_begin_node(&st, "intc@8000000");
    fdt_prop_u32(&st, &strs, "phandle", PHANDLE_GIC);
    {
        uint32_t reg[8] = {
            0, 0x08000000U, 0, 0x00010000U,
            0, 0x080a0000U, 0, 0x00f60000U,
        };
        fdt_prop_cells(&st, &strs, "reg", reg, 8);
    }
    fdt_prop_u32(&st, &strs, "#redistributor-regions", 1);
    fdt_prop_str(&st, &strs, "compatible", "arm,gic-v3");
    fdt_prop_empty(&st, &strs, "ranges");
    fdt_prop_u32(&st, &strs, "#size-cells", 2);
    fdt_prop_u32(&st, &strs, "#address-cells", 2);
    fdt_prop_empty(&st, &strs, "interrupt-controller");
    fdt_prop_u32(&st, &strs, "#interrupt-cells", 3);

    fdt_begin_node(&st, "its@8080000");
    fdt_prop_u32(&st, &strs, "phandle", PHANDLE_V2M);
    {
        uint32_t reg[4] = {0, 0x08080000U, 0, 0x00020000U};
        fdt_prop_cells(&st, &strs, "reg", reg, 4);
    }
    fdt_prop_u32(&st, &strs, "#msi-cells", 1);
    fdt_prop_empty(&st, &strs, "msi-controller");
    fdt_prop_str(&st, &strs, "compatible", "arm,gic-v3-its");
    fdt_end_node(&st);
    fdt_end_node(&st);

    /* /flash@0 (2 x 64 MB CFI NOR banks) */
    fdt_begin_node(&st, "flash@0");
    fdt_prop_u32(&st, &strs, "bank-width", 4);
    {
        uint32_t reg[8] = {
            0, 0x00000000U, 0, 0x04000000U,
            0, 0x04000000U, 0, 0x04000000U,
        };
        fdt_prop_cells(&st, &strs, "reg", reg, 8);
    }
    fdt_prop_str(&st, &strs, "compatible", "cfi-flash");
    fdt_end_node(&st);

    /* /cpus */
    fdt_begin_node(&st, "cpus");
    fdt_prop_u32(&st, &strs, "#size-cells", 0);
    fdt_prop_u32(&st, &strs, "#address-cells", 1);

    fdt_begin_node(&st, "cpu-map");
    fdt_begin_node(&st, "socket0");
    fdt_begin_node(&st, "cluster0");
    for (int i = 0; i < ncpus; i++) {
        char cname[16];
        snprintf(cname, sizeof cname, "core%d", i);
        fdt_begin_node(&st, cname);
        fdt_prop_u32(&st, &strs, "cpu", PHANDLE_CPU0 + (uint32_t)i);
        fdt_end_node(&st);
    }
    fdt_end_node(&st);
    fdt_end_node(&st);
    fdt_end_node(&st);

    for (int i = 0; i < ncpus; i++) {
        char cname[16];
        snprintf(cname, sizeof cname, "cpu@%x", i);
        fdt_begin_node(&st, cname);
        fdt_prop_u32(&st, &strs, "phandle", PHANDLE_CPU0 + (uint32_t)i);
        fdt_prop_u32(&st, &strs, "reg", (uint32_t)i);
        fdt_prop_str(&st, &strs, "enable-method", "psci");
        fdt_prop_str(&st, &strs, "compatible", "arm,cortex-a76");
        fdt_prop_str(&st, &strs, "device_type", "cpu");
        fdt_end_node(&st);
    }
    fdt_end_node(&st);

    /* /timer */
    fdt_begin_node(&st, "timer");
    {
        uint32_t irqs[12] = {
            1, 13, 4,
            1, 14, 4,
            1, 11, 4,
            1, 10, 4,
        };
        fdt_prop_cells(&st, &strs, "interrupts", irqs, 12);
    }
    fdt_prop_empty(&st, &strs, "always-on");
    {
        const char *const compat[] = {"arm,armv8-timer", "arm,armv7-timer"};
        fdt_prop_strlist(&st, &strs, "compatible", compat, 2);
    }
    fdt_end_node(&st);

    /* /apb-pclk */
    fdt_begin_node(&st, "apb-pclk");
    fdt_prop_u32(&st, &strs, "phandle", PHANDLE_CLK);
    fdt_prop_str(&st, &strs, "clock-output-names", "clk24mhz");
    fdt_prop_u32(&st, &strs, "clock-frequency", 24000000U);
    fdt_prop_u32(&st, &strs, "#clock-cells", 0);
    fdt_prop_str(&st, &strs, "compatible", "fixed-clock");
    fdt_end_node(&st);

    /* /aliases */
    fdt_begin_node(&st, "aliases");
    fdt_prop_str(&st, &strs, "serial0", "/pl011@9000000");
    fdt_end_node(&st);

    /* /chosen */
    fdt_begin_node(&st, "chosen");
    fdt_prop_str(&st, &strs, "stdout-path", "/pl011@9000000");
    {
        uint32_t rng[8] = {
            0x72b73f60U, 0xac405828U, 0xbe5e2714U, 0x7785d1e6U,
            0x9efe39ddU, 0x2cd721deU, 0x7b432f9cU, 0xbb949b86U,
        };
        fdt_prop_cells(&st, &strs, "rng-seed", rng, 8);
    }
    fdt_end_node(&st);

    fdt_end_node(&st); /* root */
    buf_u32(&st, FDT_END);

    /* Assemble final DTB: 40-byte header + 16-byte empty mem_rsvmap + struct + strings.
     * EDK2 ArmVirtQemu copies DTB from 0x40000000 into its own HOB early in SEC/PEI. */
    uint32_t off_mem_rsvmap = 40U;
    uint32_t off_dt_struct = 56U;
    uint32_t off_dt_strings = off_dt_struct + (uint32_t)st.len;
    uint32_t totalsize = off_dt_strings + (uint32_t)strs.len;
    totalsize = (totalsize + 15U) & ~15U;

    Buf out = {0};
    buf_grow(&out, totalsize);
    buf_u32(&out, FDT_MAGIC);
    buf_u32(&out, totalsize);
    buf_u32(&out, off_dt_struct);
    buf_u32(&out, off_dt_strings);
    buf_u32(&out, off_mem_rsvmap);
    buf_u32(&out, 17U); /* version */
    buf_u32(&out, 16U); /* last_comp_version */
    buf_u32(&out, 0U);  /* boot_cpuid_phys */
    buf_u32(&out, (uint32_t)strs.len);
    buf_u32(&out, (uint32_t)st.len);

    /* Empty reservation entry (16 zero bytes) */
    uint64_t zero = 0;
    buf_put(&out, &zero, 8);
    buf_put(&out, &zero, 8);

    buf_put(&out, st.buf, st.len);
    buf_put(&out, strs.buf, strs.len);
    while (out.len < totalsize) {
        uint8_t z = 0;
        buf_put(&out, &z, 1);
    }

    free(st.buf);
    free(strs.buf);
    if (out_size) *out_size = out.len;
    return out.buf;
}
