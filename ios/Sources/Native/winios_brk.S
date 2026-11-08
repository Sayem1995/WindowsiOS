/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * BreakpointJIT trap sequences for iOS 26/27 TXM dual-mapped JIT setup.
 * Adapted from Henge (Leviidev/Henge, GPL-2.0-or-later).
 *
 * Protocol (StikDebug universal.js / LLDB launcher):
 *   brk #0x69   -> debugger writes 0xE0000069 into x0 and advances pc += 4
 *   brk #0xf00d -> x16 = 1 (CMD_PREPARE_REGION: x0=addr/0, x1=len -> x0=rx_addr)
 *                  x16 = 0 (CMD_DETACH: debugger detaches cleanly, RX stays live)
 */

#if defined(__aarch64__) || defined(__arm64__)

.section __TEXT,__text,regular,pure_instructions

.globl _winios_brk_get_jit_mapping
.p2align 2
_winios_brk_get_jit_mapping:
    mov     x16, #1              /* CMD_PREPARE_REGION */
    brk     #0xf00d
    ret

.globl _winios_brk_jit_detach
.p2align 2
_winios_brk_jit_detach:
    mov     x16, #0              /* CMD_DETACH */
    brk     #0xf00d
    ret

.globl _winios_brk_probe
.p2align 2
_winios_brk_probe:
    brk     #0x69
    ret

#endif /* aarch64 */
