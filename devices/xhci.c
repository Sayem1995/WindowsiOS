#include "xhci.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "machine.h"

/* Register layout in BAR 0 (16 KB, MSI-X at +0x3000 / +0x3800). */
#define CAPLENGTH 0x40
#define OP 0x40          /* operational registers */
#define PORTREGS 0x440   /* OP + 0x400 */
#define RT 0x1000        /* runtime registers */
#define DB 0x2000        /* doorbells */
#define XECP 0x800       /* extended capabilities */
#define BAR_SIZE 0x4000

/* TRB types. */
enum {
    TRB_NORMAL = 1, TRB_SETUP = 2, TRB_DATA = 3, TRB_STATUS = 4, TRB_LINK = 6, TRB_EVENT_DATA = 7, TRB_NOOP = 8,
    TRB_ENABLE_SLOT = 9, TRB_DISABLE_SLOT = 10, TRB_ADDRESS_DEVICE = 11, TRB_CONFIGURE_EP = 12, TRB_EVALUATE_CTX = 13,
    TRB_RESET_EP = 14, TRB_STOP_EP = 15, TRB_SET_TR_DEQUEUE = 16, TRB_RESET_DEVICE = 17, TRB_NOOP_CMD = 23,
    TRB_TRANSFER_EVENT = 32, TRB_CMD_COMPLETION = 33, TRB_PORT_STATUS = 34,
};
/* Completion codes. */
enum { CC_SUCCESS = 1, CC_TRB_ERROR = 5, CC_STALL = 6, CC_SLOTS_FULL = 9, CC_SHORT_PACKET = 13, CC_CONTEXT_STATE = 19 };

/* PORTSC bits. */
#define PORT_CCS (1u << 0)
#define PORT_PED (1u << 1)
#define PORT_PR (1u << 4)
#define PORT_PLS_SHIFT 5
#define PORT_PP (1u << 9)
#define PORT_SPEED_SHIFT 10
#define PORT_LWS (1u << 16)
#define PORT_CSC (1u << 17)
#define PORT_PEC (1u << 18)
#define PORT_WRC (1u << 19)
#define PORT_OCC (1u << 20)
#define PORT_PRC (1u << 21)
#define PORT_PLC (1u << 22)
#define PORT_CEC (1u << 23)
#define PORT_CHANGES (PORT_CSC | PORT_PEC | PORT_WRC | PORT_OCC | PORT_PRC | PORT_PLC | PORT_CEC)
#define SPEED_HIGH 3
#define SPEED_FULL 1

#define HID_DCI_IN 3 /* endpoint 1 IN */

/* ---- Port 1: USB HID 1.11 Boot Keyboard ---- */

static const uint8_t kbd_report_desc[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05, 0x07, 0x19, 0x00, 0x29, 0xff, 0x81, 0x00, 0xc0,
};

static const uint8_t kbd_device_desc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64,
    0x27, 0x06, 0x02, 0x00, /* WindowsiOS / QEMU USB Keyboard (0627:0002) */
    0x00, 0x01, 1, 2, 3, 1,
};

static const uint8_t kbd_config_desc[34] = {
    9, 2, 34, 0, 1, 1, 0, 0xa0, 50,
    9, 4, 0, 0, 1, 3, 1, 1, 0,                                    /* HID, boot, keyboard */
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof kbd_report_desc, 0,
    7, 5, 0x81, 3, 8, 0, 1,                                       /* EP1 IN, 8 bytes, 1 ms */
};

/* ---- Port 2: USB HID Absolute Tablet (QEMU USB Tablet 0627:0001 style) ----
   6-byte report:
     byte 0: buttons (bit 0 = Left, bit 1 = Right, bit 2 = Middle)
     byte 1..2: X (0..32767, absolute)
     byte 3..4: Y (0..32767, absolute)
     byte 5: Wheel (-127..127, relative)
*/
static const uint8_t tab_report_desc[] = {
    0x05, 0x01,             /* Usage Page (Generic Desktop) */
    0x09, 0x02,             /* Usage (Mouse) */
    0xa1, 0x01,             /* Collection (Application) */
    0x09, 0x01,             /*   Usage (Pointer) */
    0xa1, 0x00,             /*   Collection (Physical) */
    0x05, 0x09,             /*     Usage Page (Button) */
    0x19, 0x01,             /*     Usage Minimum (1) */
    0x29, 0x03,             /*     Usage Maximum (3) */
    0x15, 0x00,             /*     Logical Minimum (0) */
    0x25, 0x01,             /*     Logical Maximum (1) */
    0x95, 0x03,             /*     Report Count (3) */
    0x75, 0x01,             /*     Report Size (1) */
    0x81, 0x02,             /*     Input (Data,Var,Abs) */
    0x95, 0x01,             /*     Report Count (1) */
    0x75, 0x05,             /*     Report Size (5) */
    0x81, 0x01,             /*     Input (Const) */
    0x05, 0x01,             /*     Usage Page (Generic Desktop) */
    0x09, 0x30,             /*     Usage (X) */
    0x09, 0x31,             /*     Usage (Y) */
    0x15, 0x00,             /*     Logical Minimum (0) */
    0x26, 0xff, 0x7f,       /*     Logical Maximum (32767) */
    0x75, 0x10,             /*     Report Size (16) */
    0x95, 0x02,             /*     Report Count (2) */
    0x81, 0x02,             /*     Input (Data,Var,Abs) */
    0x09, 0x38,             /*     Usage (Wheel) */
    0x15, 0x81,             /*     Logical Minimum (-127) */
    0x25, 0x7f,             /*     Logical Maximum (127) */
    0x75, 0x08,             /*     Report Size (8) */
    0x95, 0x01,             /*     Report Count (1) */
    0x81, 0x06,             /*     Input (Data,Var,Rel) */
    0xc0,                   /*   End Collection */
    0xc0,                   /* End Collection */
};

static const uint8_t tab_device_desc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64,
    0x27, 0x06, 0x01, 0x00, /* QEMU USB Tablet (0627:0001) */
    0x00, 0x01, 1, 4, 5, 1,
};

static const uint8_t tab_config_desc[34] = {
    9, 2, 34, 0, 1, 1, 0, 0xa0, 50,
    9, 4, 0, 0, 1, 3, 0, 0, 0,                                    /* HID, non-boot report protocol */
    9, 0x21, 0x01, 0x01, 0, 1, 0x22, sizeof tab_report_desc, 0,
    7, 5, 0x81, 3, 8, 0, 1,                                       /* EP1 IN, 8 bytes max, 1 ms */
};

static const char *const strings[] = {
    NULL,
    "WindowsiOS",
    "WindowsiOS USB Keyboard",
    "KBD01",
    "WindowsiOS USB Tablet",
    "TAB01",
};

static int xlog(void) {
    static int on = -1;
    if (on < 0) on = getenv("HV_XHCI_LOG") != NULL;
    return on;
}

static void rd(HvXhci *x, uint64_t pa, void *buf, unsigned len) {
    if (hv_phys_read(x->m, pa, buf, len) != 0) memset(buf, 0, len);
}

static void wr(HvXhci *x, uint64_t pa, const void *buf, unsigned len) {
    hv_phys_write(x->m, pa, buf, len);
}

static uint32_t rd32(HvXhci *x, uint64_t pa) {
    uint32_t v;
    rd(x, pa, &v, 4);
    return v;
}

/* ---- interrupts and the event ring ---- */

static void update_irq(HvXhci *x) {
    if (hv_pci_msix_enabled(&x->pci)) {
        hv_pci_set_intx(&x->pci, 0);
        return;
    }
    int level = 0;
    for (int i = 0; i < HV_XHCI_INTRS; i++)
        level |= (x->intr[i].iman & 1) && (x->intr[i].iman & 2);
    hv_pci_set_intx(&x->pci, level && (x->usbcmd & 4));
}

static void load_segment(HvXhci *x, struct HvXhciIntr *it) {
    uint64_t entry[2];
    rd(x, it->erstba + 16ull * it->seg_index, entry, 16);
    it->seg_base = entry[0] & ~0x3full;
    it->seg_size = (uint32_t)entry[1] & 0xffff;
    it->index = 0;
}

static void event_to(HvXhci *x, int target, uint64_t param, uint32_t status, uint32_t control) {
    int t = target >= 0 && target < HV_XHCI_INTRS ? target : 0;
    struct HvXhciIntr *it = &x->intr[t];
    if (!it->erstsz || !it->seg_size) return;
    uint32_t trb[4] = {(uint32_t)param, (uint32_t)(param >> 32), status, (control & ~1u) | (uint32_t)it->pcs};
    wr(x, it->seg_base + 16ull * it->index, trb, 16);
    if (++it->index == it->seg_size) {
        if (++it->seg_index >= it->erstsz) {
            it->seg_index = 0;
            it->pcs ^= 1;
        }
        load_segment(x, it);
    }
    x->usbsts |= 1u << 3; /* EINT */
    if (hv_pci_msix_enabled(&x->pci)) {
        if ((it->iman & 2) && (x->usbcmd & 4)) {
            it->iman &= ~1u; /* xHCI 5.5.2.1: IP auto-cleared when MSI-X fires */
            hv_pci_msix_notify(&x->pci, (uint16_t)t);
        } else {
            it->iman |= 1u;
        }
    } else {
        it->iman |= 1u;
        update_irq(x);
    }
}

static void event(HvXhci *x, uint64_t param, uint32_t status, uint32_t control) {
    event_to(x, 0, param, status, control);
}

static void port_event(HvXhci *x, int port) {
    if (!(x->usbcmd & 1)) return;
    event(x, (uint64_t)(port + 1) << 24, (uint32_t)CC_SUCCESS << 24, TRB_PORT_STATUS << 10);
}

static void cmd_completion(HvXhci *x, uint64_t trb_pa, int cc, int slot) {
    event(x, trb_pa, (uint32_t)cc << 24, TRB_CMD_COMPLETION << 10 | (uint32_t)slot << 24);
}

static void transfer_event(HvXhci *x, uint64_t trb_pa, int cc, uint32_t residual, int slot, int dci) {
    uint32_t dw2 = rd32(x, trb_pa + 8);
    event_to(x, (int)(dw2 >> 22), trb_pa, (uint32_t)cc << 24 | (residual & 0xffffff),
             TRB_TRANSFER_EVENT << 10 | (uint32_t)dci << 16 | (uint32_t)slot << 24);
}

static void transfer_event_ed(HvXhci *x, int target, uint64_t ev_data, int cc, uint32_t edtla, int slot, int dci) {
    event_to(x, target, ev_data, (uint32_t)cc << 24 | (edtla & 0xffffffu),
             (1u << 2) /* ED=1 */ | TRB_TRANSFER_EVENT << 10 | (uint32_t)dci << 16 | (uint32_t)slot << 24);
}

/* ---- rings ---- */

static int ring_peek(HvXhci *x, HvXhciRing *r, uint32_t trb[4], uint64_t *at) {
    for (int guard = 0; guard < 64; guard++) {
        uint32_t *ctl = hv_phys_ptr(x->m, r->dequeue + 12, 4);
        uint32_t c = ctl ? __atomic_load_n(ctl, __ATOMIC_ACQUIRE) : 0;
        if ((c & 1) != (uint32_t)r->ccs) return 0;
        rd(x, r->dequeue, trb, 12);
        trb[3] = c;
        if (((trb[3] >> 10) & 0x3f) == TRB_LINK) {
            if (trb[3] & 2) r->ccs ^= 1;
            r->dequeue = ((uint64_t)trb[1] << 32 | trb[0]) & ~0xfull;
            continue;
        }
        *at = r->dequeue;
        return 1;
    }
    return 0;
}

static void ring_advance(HvXhciRing *r) {
    r->dequeue += 16;
}

static uint64_t out_ctx(HvXhci *x, int slot) {
    uint64_t p;
    rd(x, x->dcbaap + 8ull * slot, &p, 8);
    return p & ~0x3full;
}

static void set_ep_state(HvXhci *x, int slot, int dci, uint32_t state) {
    uint64_t ctx = out_ctx(x, slot) + 32ull * dci;
    uint32_t dw0 = rd32(x, ctx);
    dw0 = (dw0 & ~7u) | state;
    wr(x, ctx, &dw0, 4);
}

/* ---- control requests for Port 1 (Keyboard) and Port 2 (Tablet) ---- */

static int control(HvXhci *x, HvXhciSlot *sl, const uint8_t setup[8], uint8_t *out, int max) {
    int is_tab = sl->port == 2;
    uint8_t type = setup[0], req = setup[1];
    uint16_t value = (uint16_t)(setup[2] | setup[3] << 8);
    if (xlog()) fprintf(stderr, "xhci port %d: request %02x %02x value %04x len %u\n", sl->port, type, req, value, setup[6] | setup[7] << 8);

    const uint8_t *src = NULL;
    int len = 0;
    uint8_t tmp[128];

    const uint8_t *dev_d = is_tab ? tab_device_desc : kbd_device_desc;
    const uint8_t *cfg_d = is_tab ? tab_config_desc : kbd_config_desc;
    const uint8_t *rep_d = is_tab ? tab_report_desc : kbd_report_desc;
    int rep_d_len = is_tab ? (int)sizeof(tab_report_desc) : (int)sizeof(kbd_report_desc);

    if (type == 0x80 && req == 6) { /* GET_DESCRIPTOR (device) */
        uint8_t dt = value >> 8, idx = value & 0xff;
        if (dt == 1) src = dev_d, len = 18;
        else if (dt == 2) src = cfg_d, len = 34;
        else if (dt == 3) {
            if (idx == 0) {
                tmp[0] = 4, tmp[1] = 3, tmp[2] = 0x09, tmp[3] = 0x04;
                src = tmp, len = 4;
            } else if (idx < sizeof strings / sizeof strings[0]) {
                const char *s = strings[idx];
                int n = (int)strlen(s);
                tmp[0] = (uint8_t)(2 + 2 * n), tmp[1] = 3;
                for (int i = 0; i < n; i++) tmp[2 + 2 * i] = (uint8_t)s[i], tmp[3 + 2 * i] = 0;
                src = tmp, len = 2 + 2 * n;
            } else return -1;
        } else return -1;
    } else if (type == 0x81 && req == 6) { /* GET_DESCRIPTOR (interface): HID, report */
        if ((value >> 8) == 0x22) src = rep_d, len = rep_d_len;
        else if ((value >> 8) == 0x21) src = cfg_d + 18, len = 9;
        else return -1;
    } else if ((type == 0x80 || type == 0x81 || type == 0x82) && req == 0) { /* GET_STATUS */
        tmp[0] = 0, tmp[1] = 0;
        src = tmp, len = 2;
    } else if (type == 0x80 && req == 8) { /* GET_CONFIGURATION */
        tmp[0] = (uint8_t)sl->configured;
        src = tmp, len = 1;
    } else if (type == 0x00 && req == 9) { /* SET_CONFIGURATION */
        sl->configured = value & 0xff;
        return 0;
    } else if (type == 0x81 && req == 10) { /* GET_INTERFACE */
        tmp[0] = 0;
        src = tmp, len = 1;
    } else if (type == 0xa1 && req == 1) { /* GET_REPORT */
        if (is_tab) src = x->tab_state, len = 6;
        else src = x->state, len = 8;
    } else if (type == 0xa1 && req == 2) { /* GET_IDLE */
        tmp[0] = sl->idle;
        src = tmp, len = 1;
    } else if (type == 0xa1 && req == 3) { /* GET_PROTOCOL */
        tmp[0] = sl->protocol;
        src = tmp, len = 1;
    } else if (type == 0x21 && req == 0x0a) { /* SET_IDLE */
        sl->idle = (uint8_t)(value >> 8);
        return 0;
    } else if (type == 0x21 && req == 0x0b) { /* SET_PROTOCOL */
        sl->protocol = (uint8_t)(value & 1);
        return 0;
    } else if ((type & 0x60) == 0x20 || (type == 0x00 && (req == 1 || req == 3)) ||
               (type == 0x01 && req == 11) || (type == 0x02 && (req == 1 || req == 3))) {
        return 0;
    } else {
        return -1;
    }
    if (len > max) len = max;
    memcpy(out, src, (size_t)len);
    return len;
}

static void run_ep0(HvXhci *x, int slot) {
    HvXhciSlot *sl = &x->slots[slot];
    HvXhciRing *r = &sl->ep[1];
    uint32_t trb[4];
    uint64_t at;
    uint8_t setup[8], reply[256];
    int reply_len = 0, reply_off = 0, stalled = 0, td_short = 0;
    uint32_t edtla = 0;
    while (ring_peek(x, r, trb, &at)) {
        int type = (trb[3] >> 10) & 0x3f;
        int ioc = (trb[3] >> 5) & 1;
        int ch = (trb[3] >> 4) & 1;
        int isp = (trb[3] >> 2) & 1;
        if (type == TRB_SETUP) {
            memcpy(setup, trb, 8);
            uint16_t wlen = (uint16_t)(setup[6] | setup[7] << 8);
            reply_len = control(x, sl, setup, reply, wlen < sizeof reply ? wlen : (int)sizeof reply);
            reply_off = 0;
            stalled = reply_len < 0;
            edtla = 0;
            td_short = 0;
            ring_advance(r);
            if (ioc && !stalled) transfer_event(x, at, CC_SUCCESS, 0, slot, 1);
        } else if (type == TRB_DATA) {
            uint64_t buf = (uint64_t)trb[1] << 32 | trb[0];
            uint32_t want = trb[2] & 0x1ffff;
            int in = (trb[3] >> 16) & 1;
            ring_advance(r);
            if (stalled) {
                if (xlog()) fprintf(stderr, "xhci port %d: EP0 STALL on DATA want=%u\n", sl->port, want);
                transfer_event(x, at, CC_STALL, want, slot, 1);
                set_ep_state(x, slot, 1, 2); /* EP_STATE_HALTED */
                sl->ep_running[1] = 0;
                return;
            }
            uint32_t n = 0;
            if (in) {
                uint32_t avail = reply_off < reply_len ? (uint32_t)(reply_len - reply_off) : 0;
                n = avail < want ? avail : want;
                if (n) wr(x, buf, reply + reply_off, n);
                reply_off += (int)n;
            } else {
                n = want;
            }
            edtla += n;
            if (n < want) td_short = 1;
            if (ioc || (isp && n < want))
                transfer_event(x, at, n < want ? CC_SHORT_PACKET : CC_SUCCESS, want - n, slot, 1);
            if (n < want && ch) {
                while (ch && ring_peek(x, r, trb, &at)) {
                    int nt = (trb[3] >> 10) & 0x3f;
                    if (nt == TRB_EVENT_DATA || nt == TRB_SETUP || nt == TRB_STATUS) break;
                    ch = (trb[3] >> 4) & 1;
                    ring_advance(r);
                }
            }
            if (!ch) {
                edtla = 0;
                td_short = 0;
            }
        } else if (type == TRB_STATUS) {
            ring_advance(r);
            if (stalled) {
                if (xlog()) fprintf(stderr, "xhci port %d: EP0 STALL on STATUS\n", sl->port);
                transfer_event(x, at, CC_STALL, 0, slot, 1);
                set_ep_state(x, slot, 1, 2); /* EP_STATE_HALTED */
                sl->ep_running[1] = 0;
                return;
            }
            edtla = 0;
            td_short = 0;
            if (ioc) transfer_event(x, at, CC_SUCCESS, 0, slot, 1);
        } else if (type == TRB_EVENT_DATA) {
            uint64_t ev_data = (uint64_t)trb[1] << 32 | trb[0];
            int target = (int)(trb[2] >> 22);
            ring_advance(r);
            if (ioc) {
                transfer_event_ed(x, target, ev_data, td_short ? CC_SHORT_PACKET : CC_SUCCESS, edtla, slot, 1);
            }
            edtla = 0;
            if (!ch) td_short = 0;
        } else {
            ring_advance(r);
            if (ioc) transfer_event(x, at, CC_SUCCESS, 0, slot, 1);
        }
    }
}

static int deliver_hid_in(HvXhci *x, int slot, const uint8_t *report, uint32_t rep_len) {
    HvXhciRing *r = &x->slots[slot].ep[HID_DCI_IN];
    uint32_t trb[4];
    uint64_t at;
    while (ring_peek(x, r, trb, &at)) {
        int type = (trb[3] >> 10) & 0x3f;
        int ioc = (trb[3] >> 5) & 1;
        int ch = (trb[3] >> 4) & 1;
        int isp = (trb[3] >> 2) & 1;
        if (type != TRB_NORMAL) {
            ring_advance(r);
            if (type == TRB_EVENT_DATA && ioc) {
                uint64_t ev = (uint64_t)trb[1] << 32 | trb[0];
                transfer_event_ed(x, (int)(trb[2] >> 22), ev, CC_SUCCESS, 0, slot, HID_DCI_IN);
            } else if (ioc) {
                transfer_event(x, at, CC_SUCCESS, 0, slot, HID_DCI_IN);
            }
            continue;
        }
        uint64_t buf = (uint64_t)trb[1] << 32 | trb[0];
        uint32_t want = trb[2] & 0x1ffff;
        uint32_t n = want < rep_len ? want : rep_len;
        if (n) wr(x, buf, report, n);
        int short_pkt = n < want;
        uint32_t edtla = n;
        ring_advance(r);
        if (ioc || (isp && short_pkt))
            transfer_event(x, at, short_pkt ? CC_SHORT_PACKET : CC_SUCCESS, want - n, slot, HID_DCI_IN);
        while (ch && ring_peek(x, r, trb, &at)) {
            int t2 = (trb[3] >> 10) & 0x3f;
            int ioc2 = (trb[3] >> 5) & 1;
            ch = (trb[3] >> 4) & 1;
            ring_advance(r);
            if (t2 == TRB_EVENT_DATA && ioc2) {
                uint64_t ev = (uint64_t)trb[1] << 32 | trb[0];
                transfer_event_ed(x, (int)(trb[2] >> 22), ev, short_pkt ? CC_SHORT_PACKET : CC_SUCCESS, edtla, slot, HID_DCI_IN);
                edtla = 0;
            } else if (ioc2) {
                transfer_event(x, at, short_pkt ? CC_SHORT_PACKET : CC_SUCCESS, 0, slot, HID_DCI_IN);
            }
        }
        return 1;
    }
    return 0;
}

static void run_ep_in(HvXhci *x) {
    pthread_mutex_lock(&x->mu);
    /* Port 1: Keyboard */
    if (x->kbd_slot && x->slots[x->kbd_slot].ep_running[HID_DCI_IN]) {
        int slot = x->kbd_slot;
        while (x->rhead != x->rtail) {
            const uint8_t *rep = x->reports[x->rhead % HV_XHCI_REPORTS];
            if (!deliver_hid_in(x, slot, rep, 8)) break;
            x->rhead++;
            x->delivered++;
            if (xlog() && x->delivered <= 24)
                fprintf(stderr, "xhci: delivered kbd report #%llu (%02x %02x)\n", (unsigned long long)x->delivered, rep[0], rep[2]);
        }
    }
    /* Port 2: Absolute Tablet */
    if (x->tab_slot && x->slots[x->tab_slot].ep_running[HID_DCI_IN]) {
        int slot = x->tab_slot;
        while (x->thead != x->ttail) {
            const uint8_t *rep = x->tab_reports[x->thead % HV_XHCI_REPORTS];
            if (!deliver_hid_in(x, slot, rep, 6)) break;
            x->thead++;
            x->delivered++;
            if (xlog() && x->delivered <= 24)
                fprintf(stderr, "xhci: delivered tab report #%llu (btn=%02x)\n", (unsigned long long)x->delivered, rep[0]);
        }
    }
    pthread_mutex_unlock(&x->mu);
}

/* ---- commands ---- */

static void init_ring_from_ctx(HvXhci *x, int slot, int dci) {
    HvXhciSlot *sl = &x->slots[slot];
    uint64_t ctx = out_ctx(x, slot) + 32ull * dci;
    uint64_t deq;
    rd(x, ctx + 8, &deq, 8);
    sl->ep[dci].dequeue = deq & ~0xfull;
    sl->ep[dci].ccs = (int)(deq & 1);
    sl->ep_running[dci] = 1;
}

static int address_device(HvXhci *x, int slot, uint64_t input, int bsr) {
    uint64_t out = out_ctx(x, slot);
    uint8_t slot_ctx[32], ep0[32];
    rd(x, input + 32, slot_ctx, 32);
    rd(x, input + 64, ep0, 32);
    int port = slot_ctx[6]; /* root hub port number (1 or 2) */
    if (port < 1 || port > HV_XHCI_PORTS) return CC_TRB_ERROR;

    uint32_t dw3 = (uint32_t)(bsr ? 1 : 2) << 27 | (uint32_t)(bsr ? 0 : slot);
    memcpy(slot_ctx + 12, &dw3, 4);
    uint32_t e0 = 0;
    memcpy(&e0, ep0, 4);
    e0 = (e0 & ~7u) | 1; /* running */
    memcpy(ep0, &e0, 4);
    wr(x, out, slot_ctx, 32);
    wr(x, out + 32, ep0, 32);

    x->slots[slot].port = port;
    x->slots[slot].protocol = 1;
    x->slots[slot].idle = 0;
    if (port == 1) x->kbd_slot = slot;
    else if (port == 2) x->tab_slot = slot;
    init_ring_from_ctx(x, slot, 1);
    return CC_SUCCESS;
}

static int configure_endpoints(HvXhci *x, int slot, uint64_t input, int deconfigure) {
    HvXhciSlot *sl = &x->slots[slot];
    uint64_t out = out_ctx(x, slot);
    if (deconfigure) {
        for (int dci = 2; dci < HV_XHCI_EPS; dci++) sl->ep_running[dci] = 0;
        return CC_SUCCESS;
    }
    uint32_t drop = rd32(x, input), add = rd32(x, input + 4);
    for (int dci = 2; dci < HV_XHCI_EPS; dci++) {
        if (drop & (1u << dci)) {
            sl->ep_running[dci] = 0;
            uint8_t zero[32] = {0};
            wr(x, out + 32ull * dci, zero, 32);
        }
        if (add & (1u << dci)) {
            uint8_t ep[32];
            rd(x, input + 32ull * (dci + 1), ep, 32);
            uint32_t e0;
            memcpy(&e0, ep, 4);
            e0 = (e0 & ~7u) | 1;
            memcpy(ep, &e0, 4);
            wr(x, out + 32ull * dci, ep, 32);
            init_ring_from_ctx(x, slot, dci);
        }
    }
    if (add & 1) {
        uint8_t slot_ctx[32];
        rd(x, input + 32, slot_ctx, 32);
        uint32_t dw3 = (uint32_t)3 << 27 | (uint32_t)slot;
        memcpy(slot_ctx + 12, &dw3, 4);
        wr(x, out, slot_ctx, 32);
    } else {
        uint32_t dw3 = (uint32_t)3 << 27 | (uint32_t)slot;
        wr(x, out + 12, &dw3, 4);
    }
    return CC_SUCCESS;
}

static void run_commands(HvXhci *x) {
    uint32_t trb[4];
    uint64_t at;
    while (ring_peek(x, &x->cmd, trb, &at)) {
        ring_advance(&x->cmd);
        int type = (trb[3] >> 10) & 0x3f;
        int slot = (int)(trb[3] >> 24);
        uint64_t ptr = ((uint64_t)trb[1] << 32 | trb[0]) & ~0xfull;
        int cc = CC_SUCCESS;
        if (xlog()) fprintf(stderr, "xhci: command %d slot %d ptr %llx dw3 %08x\n", type, slot, (unsigned long long)ptr, trb[3]);
        switch (type) {
        case TRB_ENABLE_SLOT: {
            int s = 1;
            while (s <= HV_XHCI_SLOTS && x->slots[s].used) s++;
            if (s > HV_XHCI_SLOTS) {
                cmd_completion(x, at, CC_SLOTS_FULL, 0);
                continue;
            }
            memset(&x->slots[s], 0, sizeof x->slots[s]);
            x->slots[s].used = 1;
            cmd_completion(x, at, CC_SUCCESS, s);
            continue;
        }
        case TRB_DISABLE_SLOT:
            if (slot >= 1 && slot <= HV_XHCI_SLOTS) {
                if (slot == x->kbd_slot) x->kbd_slot = 0;
                if (slot == x->tab_slot) x->tab_slot = 0;
                memset(&x->slots[slot], 0, sizeof x->slots[slot]);
            }
            break;
        case TRB_ADDRESS_DEVICE:
            if (slot < 1 || slot > HV_XHCI_SLOTS || !x->slots[slot].used) cc = CC_TRB_ERROR;
            else cc = address_device(x, slot, ptr, (trb[3] >> 9) & 1);
            break;
        case TRB_CONFIGURE_EP:
            if (slot < 1 || slot > HV_XHCI_SLOTS || !x->slots[slot].used) cc = CC_TRB_ERROR;
            else cc = configure_endpoints(x, slot, ptr, (trb[3] >> 9) & 1);
            break;
        case TRB_EVALUATE_CTX: {
            if (slot >= 1 && slot <= HV_XHCI_SLOTS) {
                uint32_t add = rd32(x, ptr + 4);
                uint64_t out = out_ctx(x, slot);
                if (add & 1) {
                    /* Update interrupter target etc. in slot context dw2 */
                    uint32_t dw2 = rd32(x, ptr + 32 + 8);
                    wr(x, out + 8, &dw2, 4);
                }
                if (add & 2) {
                    uint32_t dw1 = rd32(x, ptr + 64 + 4);
                    wr(x, out + 32 + 4, &dw1, 4);
                }
            }
            break;
        }
        case TRB_RESET_EP: {
            if (slot >= 1 && slot <= HV_XHCI_SLOTS) {
                int dci = (trb[3] >> 16) & 0x1f;
                if (dci >= 1 && dci < HV_XHCI_EPS) set_ep_state(x, slot, dci, 3);
            }
            break;
        }
        case TRB_STOP_EP: {
            if (slot >= 1 && slot <= HV_XHCI_SLOTS) {
                int dci = (trb[3] >> 16) & 0x1f;
                if (dci >= 1 && dci < HV_XHCI_EPS) {
                    x->slots[slot].ep_running[dci] = 0;
                    set_ep_state(x, slot, dci, 3);
                    uint64_t deq = x->slots[slot].ep[dci].dequeue | (uint64_t)x->slots[slot].ep[dci].ccs;
                    wr(x, out_ctx(x, slot) + 32ull * dci + 8, &deq, 8);
                }
            }
            break;
        }
        case TRB_SET_TR_DEQUEUE: {
            if (slot >= 1 && slot <= HV_XHCI_SLOTS) {
                int dci = (trb[3] >> 16) & 0x1f;
                if (dci >= 1 && dci < HV_XHCI_EPS) {
                    uint64_t p = (uint64_t)trb[1] << 32 | trb[0];
                    x->slots[slot].ep[dci].dequeue = p & ~0xfull;
                    x->slots[slot].ep[dci].ccs = (int)(p & 1);
                    wr(x, out_ctx(x, slot) + 32ull * dci + 8, &p, 8);
                }
            }
            break;
        }
        case TRB_RESET_DEVICE: {
            if (slot >= 1 && slot <= HV_XHCI_SLOTS) {
                for (int dci = 2; dci < HV_XHCI_EPS; dci++) x->slots[slot].ep_running[dci] = 0;
                uint32_t dw3 = (uint32_t)1 << 27;
                wr(x, out_ctx(x, slot) + 12, &dw3, 4);
                x->slots[slot].configured = 0;
                x->slots[slot].protocol = 1;
                x->slots[slot].idle = 0;
            }
            break;
        }
        case TRB_NOOP_CMD:
            break;
        default:
            cc = CC_TRB_ERROR;
            break;
        }
        cmd_completion(x, at, cc, slot);
    }
}

/* ---- ports ---- */

static void ports_connect_init(HvXhci *x) {
    for (int p = 0; p < HV_XHCI_PORTS; p++) {
        x->portsc[p] = PORT_PP | PORT_CCS | PORT_CSC | ((uint32_t)SPEED_FULL << PORT_SPEED_SHIFT) | (7u << PORT_PLS_SHIFT);
    }
}

static void port_write(HvXhci *x, int p, uint32_t v) {
    if (xlog()) fprintf(stderr, "xhci: port %d write %08x (was %08x)\n", p, v, x->portsc[p]);
    uint32_t s = x->portsc[p];
    s &= ~(v & PORT_CHANGES);
    if (v & PORT_PED) s &= ~PORT_PED;
    if (v & PORT_PR) {
        s = (s & ~(0xfu << PORT_PLS_SHIFT)) | PORT_PED | PORT_PRC;
        x->portsc[p] = s;
        port_event(x, p);
        return;
    }
    if ((v & PORT_LWS) && (s & PORT_CCS)) {
        uint32_t pls = (v >> PORT_PLS_SHIFT) & 0xf;
        s = (s & ~(0xfu << PORT_PLS_SHIFT)) | (pls << PORT_PLS_SHIFT);
        if (pls == 0 || pls == 15) {
            s = (s & ~(0xfu << PORT_PLS_SHIFT)) | PORT_PLC;
            x->portsc[p] = s;
            port_event(x, p);
            return;
        }
    }
    s = (s & ~PORT_PP) | (v & PORT_PP);
    x->portsc[p] = s;
}

static void reset(HvXhci *x) {
    x->usbcmd = 0;
    x->usbsts = 1; /* HCHalted */
    x->dnctrl = x->config = 0;
    x->dcbaap = 0;
    memset(&x->cmd, 0, sizeof x->cmd);
    x->cmd_running = 0;
    memset(x->intr, 0, sizeof x->intr);
    for (int i = 0; i < HV_XHCI_INTRS; i++) x->intr[i].pcs = 1;
    memset(x->slots, 0, sizeof x->slots);
    x->kbd_slot = 0;
    x->tab_slot = 0;
    ports_connect_init(x);
    update_irq(x);
}

static uint32_t current_mfindex(const HvXhci *x) {
    if (!(x->usbcmd & 1)) return 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return (uint32_t)((ns / 125000ull) & 0x3fffu);
}

static uint64_t bar_read(HvPciDev *d, int bar, uint64_t off, unsigned size) {
    HvXhci *x = d->opaque;
    if (bar != 0) return 0;
    uint64_t v = 0;
    if (off < CAPLENGTH) {
        static const uint32_t cap[8] = {
            CAPLENGTH | 0x0100u << 16,
            HV_XHCI_SLOTS | (uint32_t)HV_XHCI_INTRS << 8 | (uint32_t)HV_XHCI_PORTS << 24,
            1u << 4,
            0,
            1u | (uint32_t)(XECP / 4) << 16,
            DB,
            RT,
            0,
        };
        uint32_t i = (uint32_t)off / 4;
        v = cap[i];
        if (size == 8 && i + 1 < 8) v |= (uint64_t)cap[i + 1] << 32;
        v >>= 8 * (off & 3);
    } else if (off >= XECP && off < XECP + 16) {
        static const uint32_t ext[4] = {2u | 0x0200u << 16, 0x20425355u, 1u | (uint32_t)HV_XHCI_PORTS << 8, 0};
        uint32_t i = (uint32_t)(off - XECP) / 4;
        v = ext[i];
        if (size == 8 && i + 1 < 4) v |= (uint64_t)ext[i + 1] << 32;
    } else if (off >= OP && off < PORTREGS) {
        switch (off - OP) {
        case 0x00: v = x->usbcmd; break;
        case 0x04: v = x->usbsts; break;
        case 0x08: v = 1; break; /* 4 KB pages */
        case 0x14: v = x->dnctrl; break;
        case 0x18: v = x->cmd_running ? 8 : 0; break;
        case 0x1c: v = 0; break;
        case 0x30: v = size == 8 ? x->dcbaap : (uint32_t)x->dcbaap; break;
        case 0x34: v = x->dcbaap >> 32; break;
        case 0x38: v = x->config; break;
        }
    } else if (off >= PORTREGS && off < PORTREGS + 16u * HV_XHCI_PORTS) {
        int p = (int)(off - PORTREGS) / 16;
        v = (off - PORTREGS) % 16 == 0 ? x->portsc[p] : 0;
    } else if (off >= RT && off < RT + 0x20 + 32u * HV_XHCI_INTRS) {
        if (off - RT < 0x20) return off == RT ? current_mfindex(x) : 0;
        struct HvXhciIntr *it = &x->intr[(off - RT - 0x20) / 32];
        switch ((off - RT - 0x20) % 32) {
        case 0x00: v = it->iman; break;
        case 0x04: v = it->imod; break;
        case 0x08: v = it->erstsz; break;
        case 0x10: v = size == 8 ? it->erstba : (uint32_t)it->erstba; break;
        case 0x14: v = it->erstba >> 32; break;
        case 0x18: v = size == 8 ? it->erdp : (uint32_t)it->erdp; break;
        case 0x1c: v = it->erdp >> 32; break;
        }
    }
    return v;
}

static void set64(uint64_t *f, uint64_t off_in_reg, uint64_t v, unsigned size) {
    if (size == 8) *f = v;
    else if (off_in_reg) *f = (*f & 0xffffffffull) | v << 32;
    else *f = (*f & ~0xffffffffull) | (uint32_t)v;
}

static void bar_write(HvPciDev *d, int bar, uint64_t off, uint64_t v, unsigned size) {
    HvXhci *x = d->opaque;
    if (bar != 0) return;
    if (xlog() && off < DB) fprintf(stderr, "xhci: write +%llx = %llx (%u)\n", (unsigned long long)off, (unsigned long long)v, size);
    if (off >= OP && off < PORTREGS) {
        uint64_t r = off - OP;
        switch (r) {
        case 0x00:
            if (v & 2) { /* HCRST */
                reset(x);
                return;
            }
            if ((v & 1) && !(x->usbcmd & 1)) {
                x->usbcmd = (uint32_t)v;
                x->usbsts &= ~1u;
                for (int p = 0; p < HV_XHCI_PORTS; p++) {
                    if (x->portsc[p] & PORT_CSC) port_event(x, p);
                }
            } else if (!(v & 1)) {
                x->usbcmd = (uint32_t)v;
                x->usbsts |= 1u;
            } else {
                x->usbcmd = (uint32_t)v;
            }
            update_irq(x);
            break;
        case 0x04:
            x->usbsts &= ~((uint32_t)v & (1u << 2 | 1u << 3 | 1u << 4 | 1u << 10));
            break;
        case 0x14: x->dnctrl = (uint32_t)v; break;
        case 0x18:
        case 0x1c: {
            if (r == 0x18 && size == 4) {
                x->cmd.dequeue = (x->cmd.dequeue & ~0xffffffffull) | ((uint32_t)v & ~0x3fu);
                x->cmd.ccs = (int)(v & 1);
            } else if (r == 0x1c) {
                x->cmd.dequeue = (x->cmd.dequeue & 0xffffffffull) | v << 32;
            } else {
                x->cmd.dequeue = v & ~0x3full;
                x->cmd.ccs = (int)(v & 1);
            }
            break;
        }
        case 0x30:
        case 0x34: set64(&x->dcbaap, r - 0x30, v, size); x->dcbaap &= ~0x3full; break;
        case 0x38: x->config = (uint32_t)v; break;
        }
    } else if (off >= PORTREGS && off < PORTREGS + 16u * HV_XHCI_PORTS) {
        if ((off - PORTREGS) % 16 == 0) port_write(x, (int)(off - PORTREGS) / 16, (uint32_t)v);
    } else if (off >= RT + 0x20 && off < RT + 0x20 + 32u * HV_XHCI_INTRS) {
        struct HvXhciIntr *it = &x->intr[(off - RT - 0x20) / 32];
        uint64_t r = (off - RT - 0x20) % 32;
        switch (r) {
        case 0x00:
            if (v & 1) it->iman &= ~1u;
            it->iman = (it->iman & 1) | ((uint32_t)v & 2);
            update_irq(x);
            break;
        case 0x04: it->imod = (uint32_t)v; break;
        case 0x08: it->erstsz = (uint32_t)v & 0xffff; break;
        case 0x10:
        case 0x14:
            set64(&it->erstba, r - 0x10, v, size);
            if (r == 0x14 || size == 8) {
                it->erstba &= ~0x3full;
                it->seg_index = 0;
                it->pcs = 1;
                load_segment(x, it);
            }
            break;
        case 0x18:
        case 0x1c:
            set64(&it->erdp, r - 0x18, v, size);
            if (r == 0x18 && (v & 8)) it->erdp &= ~8ull;
            break;
        }
    } else if (off >= DB && off < DB + 4u * (HV_XHCI_SLOTS + 1)) {
        int slot = (int)(off - DB) / 4;
        if (slot == 0) {
            x->cmd_running = 1;
            run_commands(x);
        } else if (slot >= 1 && slot <= HV_XHCI_SLOTS && x->slots[slot].used) {
            int dci = (int)(v & 0xff);
            if (dci < 1 || dci >= HV_XHCI_EPS) return;
            uint32_t state = rd32(x, out_ctx(x, slot) + 32ull * dci) & 7;
            if (state == 2) return;
            if (state != 1) set_ep_state(x, slot, dci, 1);
            x->slots[slot].ep_running[dci] = 1;
            if (dci == 1) run_ep0(x, slot);
            else if (dci == HID_DCI_IN) run_ep_in(x);
        }
    }
}

void hv_xhci_key(HvXhci *x, uint8_t usage, int down) {
    if (!x || !usage) return;
    pthread_mutex_lock(&x->mu);
    uint8_t *s = x->state;
    if (usage >= 0xe0 && usage <= 0xe7) {
        uint8_t bit = (uint8_t)(1u << (usage - 0xe0));
        s[0] = down ? s[0] | bit : s[0] & ~bit;
    } else {
        int i;
        for (i = 2; i < 8 && s[i] != usage; i++) {}
        if (down && i == 8) {
            for (i = 2; i < 8 && s[i]; i++) {}
            if (i < 8) s[i] = usage;
        } else if (!down && i < 8) {
            memmove(s + i, s + i + 1, (size_t)(7 - i));
            s[7] = 0;
        }
    }
    if (x->rtail - x->rhead < HV_XHCI_REPORTS)
        memcpy(x->reports[x->rtail++ % HV_XHCI_REPORTS], s, 8);
    pthread_mutex_unlock(&x->mu);
    hv_machine_wake(x->m);
}

void hv_xhci_tablet(HvXhci *x, int abs_x, int abs_y, uint8_t buttons, int8_t wheel) {
    if (!x) return;
    if (abs_x < 0) abs_x = 0;
    if (abs_x > 32767) abs_x = 32767;
    if (abs_y < 0) abs_y = 0;
    if (abs_y > 32767) abs_y = 32767;

    pthread_mutex_lock(&x->mu);
    uint8_t rep[6];
    rep[0] = buttons & 0x07;
    rep[1] = (uint8_t)(abs_x & 0xff);
    rep[2] = (uint8_t)((abs_x >> 8) & 0xff);
    rep[3] = (uint8_t)(abs_y & 0xff);
    rep[4] = (uint8_t)((abs_y >> 8) & 0xff);
    rep[5] = (uint8_t)wheel;
    memcpy(x->tab_state, rep, 6);

    /* Coalesce pure motion updates if queue has unconsumed entries and buttons/wheel match */
    if (!wheel && x->ttail > x->thead) {
        uint8_t *prev = x->tab_reports[(x->ttail - 1) % HV_XHCI_REPORTS];
        if (prev[0] == rep[0] && prev[5] == 0) {
            memcpy(prev, rep, 6);
            pthread_mutex_unlock(&x->mu);
            hv_machine_wake(x->m);
            return;
        }
    }
    if (x->ttail - x->thead < HV_XHCI_REPORTS)
        memcpy(x->tab_reports[x->ttail++ % HV_XHCI_REPORTS], rep, 6);
    pthread_mutex_unlock(&x->mu);
    hv_machine_wake(x->m);
}

static const uint8_t linux_to_hid[256] = {
    0x00, 0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a, 0x2b,
    0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0x28, 0xe0, 0x04, 0x16,
    0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34, 0x35, 0xe1, 0x31, 0x1d, 0x1b, 0x06, 0x19,
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5, 0x55, 0xe2, 0x2c, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
    0x3f, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5f, 0x60, 0x61, 0x56, 0x5c, 0x5d, 0x5e, 0x57, 0x59,
    0x5a, 0x5b, 0x62, 0x63, 0x00, 0x00, 0x00, 0x44, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x58, 0xe4, 0x54, 0x00, 0xe6, 0x00, 0x4a, 0x52, 0x4b, 0x50, 0x4f, 0x4d, 0x51, 0x4e, 0x49, 0x00,
    0x00, 0x7f, 0x81, 0x80, 0x66, 0x67, 0x00, 0x48, 0x00, 0x85, 0x00, 0x00, 0x00, 0xe3, 0xe7, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70,
    0x71, 0x72, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

void hv_xhci_key_linux(HvXhci *x, uint16_t code, int down) {
    if (code < 256) hv_xhci_key(x, linux_to_hid[code], down);
}

void hv_xhci_poll(HvXhci *x) {
    if (!x || (x->rhead == x->rtail && x->thead == x->ttail)) return;
    run_ep_in(x);
}

HvXhci *hv_xhci_create(HvMachine *m, HvPcie *bus) {
    HvXhci *x = calloc(1, sizeof *x);
    x->m = m;
    pthread_mutex_init(&x->mu, NULL);
    hv_pci_init(&x->pci, 0x1b36, 0x000d, 0x0c033000u, 0x1af4, 0x1100);
    x->pci.opaque = x;
    x->pci.bar_read = bar_read;
    x->pci.bar_write = bar_write;
    hv_pci_add_bar(&x->pci, 0, BAR_SIZE, 1);
    hv_pci_add_pcie_cap(&x->pci, 0x80);
    hv_pci_add_msix(&x->pci, 0x40, 0, 0x3000, 0x3800, HV_XHCI_INTRS);
    x->pci.config[0x60] = 0x30; /* SBRN: USB 3.0 */
    x->pci.config[0x61] = 0x20; /* FLADJ */
    if (hv_pcie_attach(bus, &x->pci) < 0) {
        free(x);
        return NULL;
    }
    reset(x);
    return x;
}
