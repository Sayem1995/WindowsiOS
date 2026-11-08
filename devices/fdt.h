/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HV_FDT_H
#define HV_FDT_H

#include <stddef.h>
#include <stdint.h>

/* Build a QEMU-compatible ARM64 'virt' Flattened Device Tree (DTB) for EDK2 ArmVirtQemu.
 * Returns malloc'd buffer and sets *out_size, or NULL on error. */
uint8_t *hv_fdt_build_virt(int ncpus, uint64_t ram_base, uint64_t ram_size, size_t *out_size);

#endif
