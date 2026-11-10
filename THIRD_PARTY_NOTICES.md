# Third-Party Notices & Upstream Attributions

`WindowsiOS` is licensed as a whole under the **GNU General Public License v2.0 or later** (`GPL-2.0-or-later`; see [`LICENSE`](LICENSE)), preserving all upstream copyright notices and copyleft obligations from [`Leviidev/Henge`](https://github.com/Leviidev/Henge) and [`Leviidev/Husk`](https://github.com/Leviidev/Husk).

This project stands on the shoulders of extraordinary open-source systems engineering across the iOS binary-translation, virtualization, and firmware ecosystems. Every upstream project, library, firmware package, and driver that helped make `WindowsiOS` possible is credited below along with its governing license terms.

---

## Summary of Upstream & Ecosystem Components

| Component | Upstream Project / Authors | License | How Used in `WindowsiOS` |
| :--- | :--- | :--- | :--- |
| **Henge** (`HengeVM`) | [`Leviidev/Henge`](https://github.com/Leviidev/Henge) & [`Leviidev/Henge#7`](https://github.com/Leviidev/Henge/pull/7) (Levii & contributors) | `GPL-2.0-or-later` | Foundational ARM64-to-ARM64 dynamic binary translator, stage-1 software MMU foundation, block/IJC/return caches (`core/`, `host/enter.S`, `host/jit_region.c`), `/.henge` WebDAV share server (`host/share.c`), and iOS 26/27 TXM dual-mapped JIT substrate (`ios/Sources/Native/winios_jit.c`, `winios_brk.S`, `henge-jit.js`) |
| **Husk** | [`Leviidev/Husk`](https://github.com/Leviidev/Husk) (Levii & contributors) | `GPL-2.0-or-later` | ARM64 JIT architecture & translation insights, iOS `libslirp` cross-build patches, and on-device JIT helper integration (`scripts/build_ios_deps.sh`) |
| **LinuxRunner** | [`mxhm11cr/LinuxRunner`](https://github.com/mxhm11cr/LinuxRunner) (derived from `ish-app/ish` & `rcarmo/ios-linuxkit`) | `GPL-2.0` / `GPL-3.0` | Architectural inspiration and early ARM64 guest OS bring-up & iOS runtime discipline patterns (no GPL-3.0-only source files linked) |
| **StikDebug / StikJIT** | [`StephenDev0/StikDebug`](https://github.com/StephenDev0/StikDebug) / `StikJIT` (BluDood / StephenDev0 & contributors) + [`jkcoxson/idevice`](https://github.com/jkcoxson/idevice) | `MPL-2.0` / `BSD-3-Clause` / `MIT` | On-device iOS 26/27 TXM debugger & JIT page-blessing protocol (`universal.js` / `ios/Sources/Resources/henge-jit.js`, `BRK #0x69` / `BRK #0xF00D` dual-mapped RX/RW regions, and `CS_DEBUGGED` ledger-exempt memory setup) |
| **libslirp** | [`gitlab.freedesktop.org/slirp/libslirp`](https://gitlab.freedesktop.org/slirp/libslirp) / [`utmapp/libslirp`](https://github.com/utmapp/libslirp) (Danny Gasparovski, Fabrice Bellard, QEMU & libslirp contributors) | `BSD-3-Clause` | User-mode TCP/IP + DHCP + DNS NAT stack (`10.0.2.0/24`) linked via `host/net_slirp.c` (fetched & cross-compiled at build time by `scripts/build_ios_deps.sh`) |
| **GLib / libintl / libffi / PCRE2** | GNOME GLib (`2.83.0`), `proxy-libintl`, `libffi` (`3.5.0`), `PCRE2` | `LGPL-2.1-or-later` / `MIT` / `BSD-3-Clause` | Transitive build-time static libraries required by `libslirp` on `arm64-apple-ios` (`scripts/build_ios_deps.sh`, staged into ignored `ios/deps/`) |
| **TianoCore EDK2 (`ArmVirtQemu`)** | [`tianocore/edk2`](https://github.com/tianocore/edk2) (TianoCore / Intel / Linaro / Arm contributors) | `BSD-2-Clause-Patent` | `AARCH64` UEFI firmware image (`QEMU_EFI.fd` -> `work/code.fd` + `work/vars.fd`). **Not bundled in this repository**; fetched at provisioning time |
| **Red Hat `virtio-win` (`NetKVM`)** | [`virtio-win/kvm-guest-drivers-windows`](https://github.com/virtio-win/kvm-guest-drivers-windows) (Red Hat, Inc. & contributors) | `BSD-3-Clause` / `GPL-2.0` | WHQL-signed Windows 11 ARM64 VirtIO Ethernet miniport driver (`NetKVM` `w11/ARM64`). **Not bundled in this repository**; fetched & injected at provisioning time |

---

## 1. Leviidev/Henge & Leviidev/Henge#7 (`GPL-2.0-or-later`)

- **Repository:** <https://github.com/Leviidev/Henge> and <https://github.com/Leviidev/Henge/pull/7>
- **License:** GNU General Public License v2.0 or later (`GPL-2.0-or-later`)

`WindowsiOS` directly adapts and extends Henge's `HengeVM` ARM64-to-ARM64 dynamic binary translator (`core/`), assembly entry trampoline and dual-mapped JIT allocator (`host/enter.S`, `host/jit_region.c`), `libslirp` + `/.henge/` WebDAV guest-host transport (`host/net_slirp.c`, `host/share.c`), and iOS 26/27 TXM debugger-assisted JIT bridge (`ios/Sources/Native/winios_jit.c`, `ios/Sources/Native/winios_brk.S`, `ios/Sources/Resources/henge-jit.js`).

Significant modifications were made across `core/`, `devices/`, `host/`, `ios/`, `scripts/`, and `tools/` in November 2026 to boot and run Microsoft Windows 11 24H2 ARM64 (`ntoskrnl.exe` + Prism `xtajit64.dll`), including 4 KB stage-1 MMU support with `TCR_EL1.A1 = 1` ASID-keyed `HvTlbStash`, `LDXP`/`STXP` and `LDXR XZR` LSE lowering, unaligned `FEAT_LRCPC2` emulation, UEFI/ACPI 6.3/GICv3 ITS/NVMe/xHCI/Intel ICH6 HD Audio virtual hardware, `hvsparse` block storage, native iOS `SFSafariViewController` OAuth loopback relay, and the foldable iPhone Duo UI. All modified and newly added files in the combined work remain licensed under **GPL-2.0-or-later** (see [`LICENSE`](LICENSE)).

```text
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
```

---

## 2. Leviidev/Husk (`GPL-2.0-or-later`)

- **Repository:** <https://github.com/Leviidev/Husk>
- **License:** GNU General Public License v2.0 or later (`GPL-2.0-or-later`)

`Leviidev/Husk` pioneered practical ARM64 translation, iOS `libslirp` build patches, and on-device StikJIT integration patterns on modern non-jailbroken iOS devices. Portions of the iOS JIT/dependency build flow (`scripts/build_ios_deps.sh`) draw directly from Husk's open-source architecture under `GPL-2.0-or-later`.

---

## 3. mxhm11cr/LinuxRunner (`GPL-2.0` / `GPL-3.0` Inspiration)

- **Repository:** <https://github.com/mxhm11cr/LinuxRunner>
- **Lineage:** Derived from [`ish-app/ish`](https://github.com/ish-app/ish) (Theodore Dubois & contributors) and [`rcarmo/ios-linuxkit`](https://github.com/rcarmo/ios-linuxkit) (Rui Carmo & contributors)

Credited with gratitude for demonstrating early guest OS bring-up workflows, iOS Jetsam footprint discipline, and desktop session ergonomics on iOS hardware. `WindowsiOS` does not link source files from `LinuxRunner`.

---

## 4. StikDebug / StikJIT (`MPL-2.0` / `BSD-3-Clause` / `MIT`)

- **Repositories:** <https://github.com/StephenDev0/StikDebug>, `StikJIT`, and <https://github.com/jkcoxson/idevice>
- **Authors:** StephenDev0 (BluDood), Jackson Coxson (`idevice`), and contributors
- **Licenses:** Mozilla Public License 2.0 (`MPL-2.0`), `BSD-3-Clause` (debugger script conventions), and `MIT` (`idevice`)

On iOS 26 and iOS 27 devices equipped with Apple's Trusted Execution Monitor (**TXM**), hardware enforcement blocks ordinary `MAP_JIT` execution without debugger page-blessing. `WindowsiOS` uses the `BRK #0x69` / `BRK #0xF00D` protocol (`ios/Sources/Native/winios_brk.S`, `ios/Sources/Resources/henge-jit.js`, and `scripts/_lldb_txm_jit.py`) pioneered by `StikDebug` / `StikJIT` and adapted by `Leviidev/Henge` to allocate and bless dual-mapped RX/RW JIT regions (`_M<size>,rx` + `vm_remap`) and pre-allocate ledger-exempt guest RAM while `CS_DEBUGGED` is active prior to debugger detach.

- `StikJIT` framework components (when optionally staged via `scripts/build_ios_deps.sh`) are governed by the **Mozilla Public License Version 2.0** (`MPL-2.0`, compatible with `GPL-2.0-or-later` Larger Works under MPL-2.0 Section 3.3). Full MPL-2.0 text: <https://www.mozilla.org/MPL/2.0/>.
- `idevice` (Copyright (c) Jackson Coxson) is licensed under the **MIT License**.

---

## 5. libslirp (`BSD-3-Clause`) & Cross-Compiled Static Dependencies

- **Repository:** <https://gitlab.freedesktop.org/slirp/libslirp> (mirror: <https://github.com/utmapp/libslirp>)
- **License:** `BSD-3-Clause`

`WindowsiOS` uses `libslirp` (`v4.9.1`) in `host/net_slirp.c` to provide rootless, entitlement-free user-mode TCP/IP, UDP, ICMP, DHCP (`10.0.2.15`), DNS (`10.0.2.3`), WebDAV Unix-socket forwarding (`10.0.2.4:80`), and dynamic IPv4/IPv6 OAuth loopback port forwarding. `libslirp` is fetched from upstream release tarballs during `scripts/build_ios_deps.sh` (or via Homebrew on macOS) and is never committed as binary artifacts to git.

```text
Copyright (c) 1995,1996 Danny Gasparovski. All rights reserved.
Copyright (c) 2004-2008 Fabrice Bellard.
Copyright (c) libslirp contributors.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Build-Time Static Dependencies of `libslirp` on iOS (`scripts/build_ios_deps.sh`)

When building `ios/deps/` locally:
- **GLib `2.83.0` & `proxy-libintl`:** Licensed under `LGPL-2.1-or-later` (<https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>).
- **libffi `3.5.0`:** Copyright (c) 1996-2024 Anthony Green, Red Hat, Inc. and contributors. Licensed under the `MIT` License.
- **PCRE2:** Copyright (c) University of Cambridge / Philip Hazel. Licensed under `BSD-3-Clause`.

---

## 6. TianoCore EDK2 `ArmVirtQemu` (`BSD-2-Clause-Patent`)

- **Repository:** <https://github.com/tianocore/edk2>
- **License:** `BSD-2-Clause-Patent`

`WindowsiOS` boots `AARCH64` UEFI firmware (`ArmVirtQemu` `QEMU_EFI.fd`, mapped into `pflash0` at `0x00000000` with persistent CFI01 NVRAM `pflash1` at `0x04000000`). Firmware binaries (`*.fd`) are **not** stored in this git repository; they are supplied locally in `work/code.fd` and `work/vars.fd` during provisioning.

```text
SPDX-License-Identifier: BSD-2-Clause-Patent

Copyright (c) 2011 - 2024, ARM Limited. All rights reserved.
Copyright (c) 2014 - 2024, Linaro Limited. All rights reserved.
Copyright (c) 2006 - 2024, Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

Subject to the terms and conditions of this license, each copyright holder
and contributor hereby grants to those receiving rights under this license
a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable
(except for failure to satisfy the conditions of this license) patent license
to make, have made, use, offer to sell, sell, import, and otherwise transfer
this software, where such license applies only to those patent claims, already
acquired or hereafter acquired, licensable by such copyright holder or
contributor that are necessarily infringed by:

(a) their Contribution(s) (the licensed copyrights of copyright holders and
    non-copyrightable additions of contributors, in source or binary form)
    alone; or
(b) combination of their Contribution(s) with the work of authorship to which
    such Contribution(s) was added by such copyright holder or contributor, if,
    at the time the Contribution is added, such addition causes such combination
    to be necessarily infringed. The patent license shall not apply to any other
    combinations which include the Contribution.

Except as expressly stated above, no patent license is granted either expressly
or by implication, estoppel, or otherwise.

DISCLAIMER

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## 7. Red Hat `virtio-win` — `NetKVM` ARM64 Driver (`BSD-3-Clause` / `GPL-2.0`)

- **Repository:** <https://github.com/virtio-win/kvm-guest-drivers-windows>
- **Copyright:** Copyright (c) Red Hat, Inc. and contributors
- **License:** `BSD-3-Clause` (source) / WHQL-signed driver package terms (`virtio-win` ISO)

During offline Windows 11 image provisioning (`scripts/provision_windows_arm64.py`), the WHQL-signed Windows 11 ARM64 `NetKVM` driver (`PCI\VEN_1AF4&DEV_1000&SUBSYS_00011AF4` / transitional VirtIO Ethernet) is injected into the guest driver store via `DISM /Add-Driver`. No `virtio-win` binaries are committed to this repository.

---

## 8. Non-Redistributed Proprietary Guest Software

The following proprietary operating system and application packages are **never** included in this repository, are excluded via [`.gitignore`](.gitignore), and are fetched/installed strictly on the user's own machine into `work/`:

- **Microsoft Windows 11 24H2 ARM64** (`Build 26100`) — Property of Microsoft Corporation, governed by the Microsoft Software License Terms. Users supply their own official Microsoft retail ARM64 ESD/ISO (`work/downloads/Win11_24H2_ARM64_en-us.esd`).
- **Google Antigravity (`2.31.0` Windows ARM64)** — Property of Google LLC, governed by its respective terms of service.
