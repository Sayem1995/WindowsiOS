/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HV_ACPI_H
#define HV_ACPI_H

#include <stdint.h>
#include "fw_cfg.h"

/* Populate HvFwCfg with ACPI 6.4 tables (DSDT, FACP, APIC/MADT with GICv3 + GICv2m,
 * PPTT, GTDT, MCFG, SPCR, DBG2, IORT, XSDT), RSDP, BIOS table-loader script, and
 * SMBIOS 3.0 tables for EDK2 ArmVirtQemu and Windows 11 ARM64. */
void hv_acpi_build_and_register(HvFwCfg *fw, int ncpus, uint64_t ram_size, int spi_msi_base, int spi_msi_count);

#endif
