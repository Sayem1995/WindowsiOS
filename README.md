# WindowsiOS — Native Windows 11 ARM64 on iOS (iPhone Duo)

`WindowsiOS` runs **Microsoft Windows 11 24H2 ARM64** (Build `26100`) natively on non-jailbroken iOS / iPadOS 26–27 TXM hardware — verified end-to-end on the foldable **iPhone Duo** (`iPhone19,4`, Apple A20 Pro GPU) — without `Hypervisor.framework`.

Built on the open-source ARM64-to-ARM64 binary translation and iOS TXM JIT precedent of:
- [`Leviidev/Henge`](https://github.com/Leviidev/Henge) & [`Leviidev/Henge#7`](https://github.com/Leviidev/Henge/pull/7) — ARM64 dynamic binary translator, software MMU, trackpad/touch input, and iOS 26/27 TXM JIT substrate (`GPL-2.0-or-later`)
- [`Leviidev/Husk`](https://github.com/Leviidev/Husk) — ARM64 JIT architecture notes (`GPL-2.0-or-later`)
- [`mxhm11cr/LinuxRunner`](https://github.com/mxhm11cr/LinuxRunner) — ARM64 OS bring-up patterns on iOS

## Key Architectural Highlights

1. **4 KB Software MMU, ASID-Keyed `HvTlbStash` & Direct ARM64-to-ARM64 JIT (`core/`)**
   - Full 4 KB granule (`HV_TLB_PAGE_BITS = 12`) stage-1 software MMU required by the Windows NT ARM64 kernel (`ntoskrnl.exe`), paired with 2 MB / 1 GB large-block Bloom-filtered TLB tracking and Cortex-A76 `MIDR_EL1` (`0x414fd0b1`).
   - **Windows `TCR_EL1.A1 = 1` ASID-Keyed `HvTlbStash`**: Windows ARM64 stores the process ASID in `TTBR1_EL1[63:48]` (`TCR.A1 = 1`) and transitions transiently through `ASID 0` during `KiSwapProcess`. `hv_mmu_ukey` preserves ~266–313 user TLB entries across `64,800+/s` process switches without re-walking 4-level page tables.
   - **Inline Exclusive Atomics (`LDXP`/`STXP` & `LDXR`/`LDAXR XZR/WZR`)**: Fast-path Apple Silicon `LDP`/`CASPAL` for 128-bit Windows `SLIST` (`ExpInterlockedPopEntrySList`) and `LDAR`/`CASAL` for 8/16/32/64-bit exclusives including Clang C++ `ldxr xzr, [rn]; stlxr ws, xt, [rn]` patterns used throughout `msedge.dll`.
   - **Prism (`xtajit64.dll`) `FEAT_LRCPC2` Unaligned Ordered Access Emulation**: Keeps aligned RCpc accesses inline at native speed while routing unaligned `LDAPUR` / `STLUR` / `LDAPR` crossings (`SCTLR_EL1.nAA = 1`) through acquire/release-fenced software emulation so Darwin/XNU (`nAA = 0`) never raises `SIGBUS`.
2. **UEFI + ACPI 6.3 + GICv3 ITS + PCIe NVMe / xHCI / WHQL VirtIO-Net Machine (`devices/`)**
   - EDK2 `ArmVirtQemu` UEFI (`pflash0` code + persistent CFI01 `pflash1` NVRAM) + `fw_cfg` + `ramfb` 120Hz Metal presenter (`devices/fb_metal.m`).
   - Full ACPI 6.3 (`FADT` PSCI, `GTDT`, `MCFG`, `MADT` GICD/GICR/GIC ITS `0x0F`, `IORT` mapping PCIe RIDs to GICv3 ITS#0, and `DSDT` GPEX Root Complex + GED power button).
   - PCIe GPEX Root Complex Integrated Endpoints (`0x0092`): NVMe 1.4 (`WIN11IOS01` with deferred `HV_K_FLUSH=1` + periodic/app-lifecycle `hv_disk_sync()`), USB 3.0 xHCI (`Event Data TRB` `ED=1` + `EDTLA` + `CC_STALL` halt recovery for Port 1 HID Keyboard `0627:0002` + Port 2 Absolute Touch/Trackpad Tablet `0627:0001`), WHQL ARM64 `virtio-net` (`NetKVM` `SUBSYS_11001AF4` over `libslirp` NAT for live HTTPS browsing in Microsoft Edge + `http://10.0.2.4/` host share), and `virtio-snd` (CoreAudio).
3. **Featherweight Windows 11 Profile, 384 MiB TXM JIT, 4.5 GB Ledger-Exempt RAM & Surface Laptop Ultra UI (`ios/`, `scripts/`, `tools/`)**
   - **Featherweight Guest Optimization (`scripts/optimize_windows_lite.py`, `tools/opt_hive_arm64.c`)**: Trims guest RAM usage at idle Desktop down to **`1.46 GB / 4.5 GB` (`~31.7%`)** and **`89%` WFI idle** across 4 A20 Pro performance-core vCPUs (`QOS_CLASS_USER_INITIATED`).
   - **TXM Dual-Mapped JIT + Ledger-Exempt Guest RAM**: Reserves a zero-fill hole in `__DATA`, blesses **`384 MiB` RX/RW TXM JIT**, and allocates **`4,608 MiB` (`4.5 GB`) of ledger-exempt Guest RAM** while `CS_DEBUGGED` is active before detaching LLDB (`scripts/launch_on_duo.sh`), leaving ~2–3 GB of free iOS Jetsam headroom.
   - **Midnight-Sapphire Surface Laptop Ultra / Surface Book UI**: Uses iOS 27 `UIHingeInteraction` (`UIHinge.Status.partiallyOpen` + live fold angle) on the foldable **iPhone Duo** to switch seamlessly between **Folded Clamshell Laptop Mode** (Midnight-Sapphire CNC aluminum chassis, razor-thin top display lid with Surface Studio camera dot, recessed 6-row matte-black chiclet keyboard with `F1–F10`/`F4 Edge`/Copilot/stacked `Up`/`Down` arrows, and flush matte-glass multi-touch touchpad) and **Unfolded Open Studio Mode** (edge-to-edge Metal display with an auto-hiding frosted-sapphire command pill).

## Building & Deploying

```bash
# 1. Build core test suite, macOS runner (build/winvm), sparse image packer (build/hvsparse), and iOS static library (build/ios/libwinvm.a)
./scripts/build_ios_deps.sh
make -j8 all ios-lib test-core

# 2. Provision & optimize Featherweight Windows 11 24H2 ARM64 NVMe image
/opt/homebrew/bin/python3 scripts/provision_windows_arm64.py
/opt/homebrew/bin/python3 scripts/optimize_windows_lite.py

# 3. Build & install WindowsiOS.app for physical iOS device
xcodegen generate --spec ios/project.yml
xcodebuild -project ios/WindowsiOS.xcodeproj -scheme WindowsiOS -configuration Release \
  -sdk iphoneos -derivedDataPath build/DerivedData build
xcrun devicectl device install app --device 00008160-000E05D036C0000A \
  build/DerivedData/Build/Products/Release-iphoneos/WindowsiOS.app

# 4. Pack sparse image, transfer to Documents/Machines/Win11, and launch with 384 MiB TXM JIT + 4608 MiB RAM
./build/hvsparse pack work/windows11.img work/windows11.hvsparse
./scripts/launch_on_duo.sh
```

## License

Licensed under **GPL-2.0-or-later** (`LICENSE`), preserving upstream copyright and license terms from [`Leviidev/Henge`](https://github.com/Leviidev/Henge) and [`Leviidev/Husk`](https://github.com/Leviidev/Husk).
