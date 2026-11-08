#ifndef HENGEVM_XHCI_H
#define HENGEVM_XHCI_H

#include <pthread.h>

#include "pcie.h"

/* xHCI 1.0 USB host controller (MSI-X + INTx) with:
   - Port 1: USB HID Boot Keyboard
   - Port 2: USB HID Absolute Tablet (0..32767 X/Y, L/R/M buttons, scroll wheel)
   Zero guest drivers required in EDK2 UEFI or Windows 11 ARM64. */
#define HV_XHCI_SLOTS 8
#define HV_XHCI_PORTS 2
#define HV_XHCI_INTRS 4
#define HV_XHCI_EPS 32 /* device context index 1..31 */
#define HV_XHCI_REPORTS 256

typedef struct HvXhciRing {
    uint64_t dequeue; /* guest PA of the next TRB */
    int ccs;          /* consumer cycle state */
} HvXhciRing;

typedef struct HvXhciSlot {
    int used;
    int port;         /* 1 = keyboard, 2 = tablet */
    int configured;
    uint8_t protocol; /* 0 = boot, 1 = report */
    uint8_t idle;
    HvXhciRing ep[HV_XHCI_EPS];
    int ep_running[HV_XHCI_EPS];
} HvXhciSlot;

typedef struct HvXhci {
    HvPciDev pci;
    HvMachine *m;
    /* operational */
    uint32_t usbcmd, usbsts, dnctrl, config;
    uint64_t dcbaap;
    HvXhciRing cmd;
    int cmd_running;
    uint32_t portsc[HV_XHCI_PORTS];
    /* interrupters and their event rings */
    struct HvXhciIntr {
        uint32_t iman, imod, erstsz;
        uint64_t erstba, erdp;
        uint64_t seg_base;
        uint32_t seg_size, seg_index, index;
        int pcs;
    } intr[HV_XHCI_INTRS];

    HvXhciSlot slots[HV_XHCI_SLOTS + 1];
    int kbd_slot;
    int tab_slot;

    pthread_mutex_t mu;
    /* Port 1 keyboard reports (8 bytes each) */
    uint8_t reports[HV_XHCI_REPORTS][8];
    uint32_t rhead, rtail;
    uint8_t state[8];
    /* Port 2 absolute tablet reports (6 bytes: buttons, x_lo, x_hi, y_lo, y_hi, wheel) */
    uint8_t tab_reports[HV_XHCI_REPORTS][6];
    uint32_t thead, ttail;
    uint8_t tab_state[6];

    uint64_t delivered;
} HvXhci;

HvXhci *hv_xhci_create(HvMachine *m, HvPcie *bus);
/* Any thread: HID usage page 7 key down/up (e.g. 4 = A; 0xe0-0xe7 modifiers). */
void hv_xhci_key(HvXhci *x, uint8_t usage, int down);
/* Any thread: Linux input key code down/up. */
void hv_xhci_key_linux(HvXhci *x, uint16_t code, int down);
/* Any thread: USB HID Absolute Tablet event (x, y in 0..32767; buttons bit0=L, bit1=R, bit2=M; wheel -127..127). */
void hv_xhci_tablet(HvXhci *x, int abs_x, int abs_y, uint8_t buttons, int8_t wheel);
/* CPU thread: hand waiting keyboard & tablet reports to the guest. */
void hv_xhci_poll(HvXhci *x);

#endif
