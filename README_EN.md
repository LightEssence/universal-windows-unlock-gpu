# Universal Multi-GPU Windows Unlock Engine (Ampere & Ada Lovelace)

[English](README_EN.md) | [Русский](README.md)

> **100% Volatile In-RAM Unlock — Zero Physical SPI Flash Writes.**  
> Safe, volatile in-RAM unlock of full hardware compute performance (FP32 / FP16 / BF16 Tensor Cores) for Windows 10/11 via a standalone UEFI bootloader.  
> *Built on and extending the research and codebase of [JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada).*

---

## ⚠️ Important Disclaimer & Validation Notice

> [!IMPORTANT]  
> **Physical Hardware Validation Testbed:**  
> This bootloader has been physically tested, validated, and verified **exclusively** on the following hardware platform:
> * **Motherboard:** Huananzhi Dual-Socket X99 (LGA2011-v3 dual-socket workstation)
> * **Processors:** 2 × Intel Xeon E5-2696 v3 (Haswell-EP, 36 cores / 72 threads total)
> * **Graphics Card:** Gigabyte GeForce RTX 4070 Ti WINDFORCE OC (AD104, PCI Device `0x2782`, Subsystem `0x1458:0x40e8`)
>
> This project is distributed **"AS IS"** strictly for research and educational purposes. The authors assume no liability for incompatibility, unexpected behavior, or hardware instability on other motherboards, BIOS revisions, or silicon steppings. Use on any other hardware configuration is entirely at your own risk.

> [!NOTE]  
> **Guaranteed EEPROM Safety (Zero SPI Flash Writes):**  
> This software **NEVER flashes, modifies, or writes to the physical SPI Flash EEPROM** of your graphics cards. All microcode permission adjustments and hardware speed selector latching (`SS0 = 0x88888888`, `SS1 = 0x8`) occur strictly within **Host RAM** and volatile Falcon/GSP MMIO BAR0 registers. Rebooting without the USB drive instantly and completely restores all GPUs to their 100% factory stock state.

---

## 📊 Performance Benchmark Results (RTX 4070 Ti)

Compute throughput tests performed under Windows 11 using PyTorch 2.11.0+cu130 (GEMM matrix multiplication benchmark up to $16384 \times 16384$):

| Precision | Matrix Dimension | Execution Time (ms) | Peak Throughput (TFLOPS) | Validation Status |
| :--- | :--- | :--- | :--- | :--- |
| **BFloat16 (BF16)** | $16384 \times 16384$ | 61.352 ms | **143.37 TFLOPS** | 🟢 Full Hardware Unlock Verified |
| **Float16 (FP16)** | $16384 \times 16384$ | 62.943 ms | **139.76 TFLOPS** | 🟢 Full Hardware Unlock Verified |
| **Float32 (FP32)** | $8192 \times 8192$ | 40.038 ms | **27.46 TFLOPS** | 🟢 100% Stock Compute Performance |

*Note: BF16 and FP16 verify continuous, sustained operation of 4th-Gen Ada Lovelace Tensor Cores with zero thermal throttling or numerical calculation errors.*

---

## 🎯 Key Architectural Features

1. **Complete Motherboard Independence:**
   * Dynamic automated search for Windows Boot Manager (`bootmgfw.efi`) across all accessible ESP/FAT partitions without hardcoded disk GUIDs or partition paths.
   * Universal display detachment using standard UEFI firmware protocols (`DisconnectController`), avoiding fragile ACPI table hacks.
   * Explicit PCI Command Register synchronization (IO Space + Memory Space decode enabled, Bus Master disabled prior to transaction claim) ensuring seamless GSP/Falcon transaction admission.

2. **Multi-GPU & Heterogeneous Generation Support:**
   * Full PCIe hierarchy enumeration across all segments, root bridges, and CPU sockets (essential for multi-socket platforms like Dual Xeon / EPYC / Threadripper).
   * Simultaneous support for **Ampere (RTX 30 Series)** and **Ada Lovelace (RTX 40 Series)** cards within the same system.
   * Intelligent role separation: Primary display adapters (active GOP) are released smoothly, while headless compute accelerators are processed without desktop display overhead.

3. **Strict Hardware ID VBIOS Addressing:**
   * Completely eliminates ambiguous filename collisions (no generic `vbios.rom`).
   * Files are mapped strictly by their PCI Subsystem ID:  
     `\EFI\VBIOS\<VENDOR>_<DEVICE>_<SUBVENDOR>_<SUBDEVICE>.rom`  
     *(e.g., `\EFI\VBIOS\10de_2782_1458_40e8.rom`)*.
   * If multiple identical cards are detected, the VBIOS image is staged in RAM once and shared across all matching controllers.

4. **Direct Page Allocation (`Zero-Allocation-Failure`):**
   * Heavy firmware payloads (e.g., `gsp_ga10x.bin`, 84.3 MB) are loaded via direct physical memory page allocation (`gBS->AllocatePages`), preventing out-of-memory aborts caused by UEFI heap fragmentation.

---

## 🧩 Supported GPU Generations

| Family | Architecture | Silicon Chips | Example Models |
|---|---|---|---|
| **GeForce RTX 30 Series** | **Ampere** | `GA102, GA103, GA104, GA106, GA107` | RTX 3050, 3060 (12GB/8GB), 3060 Ti, 3070, 3080, 3090 |
| **GeForce RTX 40 Series** | **Ada Lovelace** | `AD102, AD103, AD104, AD106, AD107` | RTX 4060, 4060 Ti, 4070, 4070 Ti, 4080, 4090 |

---

## 🚀 Quick Start Guide

### Step 1. Prepare USB Flash Drive
Format any USB drive using the **FAT32** filesystem.

### Step 2. Dump VBIOS in Windows (1 Click)
1. Plug the FAT32 USB drive into your PC.
2. Run `dump_vbios.bat` as Administrator.
3. The script automatically identifies all connected NVIDIA GPUs and dumps their ROMs into `EFI\VBIOS\` using exact hardware IDs (e.g., `10de_2782_1458_40e8.rom`).

### Step 3. Deploy the Bootloader
Run the deployment script:
```bat
copy_to_usb.bat
```
*(or via PowerShell: `powershell -ExecutionPolicy Bypass -File .\copy_to_usb.ps1 -DriveLetter <Drive_Letter>`)*

This copies:
* `\EFI\BOOT\BOOTX64.EFI` — Standalone EFI bootloader.
* `\EFI\ASSETS\` — Unified microcode assets for Ampere and Ada Lovelace.
* `\EFI\VBIOS\` — GPU VBIOS dumps.

### Step 4. Boot via UEFI
1. Restart your PC and enter the UEFI Boot Menu (`F11`, `F12`, or `F8`).
2. Select your USB drive (`UEFI: <Your USB Drive>`).
3. The bootloader scans the PCIe bus, reports all supported GPUs, and provides two choices:
   * **[U] Unlock All Ready GPUs & Boot Windows** — Executes volatile in-RAM unlock and chains to Windows.
   * **[S] Boot Stock Windows** — Boots Windows immediately without modifications.  
   *(Option `[U]` is automatically executed after a 10-second countdown)*.

---

## 📁 Repository Structure

```
universal-windows-unlock-gpu/
├── dist/
│   └── EFI/
│       └── BOOT/
│           └── BOOTX64.EFI    # Compiled standalone EFI application (~116 KB, clean PE32+)
├── UNIVERSAL-ASSETS/          # Microcode assets for GSP & Falcon (Ampere + Ada)
│   ├── booter_load_ad102.bin
│   ├── booter_load_ga102.bin
│   ├── gsp_boot_ad102.bin
│   ├── gsp_boot_desc_ad102.bin
│   ├── gsp_boot_desc_ga102.bin
│   ├── gsp_boot_ga102.bin
│   ├── gsp_ga10x.bin
│   ├── v67-ad102-610.43.03-four-plm.bin
│   ├── v67-ga102-610.43.03.bin
│   └── NVIDIA-LICENSE
├── include/                   # UEFI and EDK2 headers
├── src/                       # Bootloader source code (C99 / UEFI)
│   ├── app.c                  # UefiMain entry point and interactive UI
│   ├── asset_loader.c         # Direct-page firmware staging into RAM
│   ├── checks.c               # Protocol and display validation
│   ├── display_release.c      # GOP disconnect and PCI Command register setup
│   ├── pci_scan.c             # Multi-root PCIe scanning
│   ├── port.c                 # Kernel abstraction layer: DMA, MMIO, FLR, BAR0
│   ├── profile.c              # Architectural profiling (Ampere / Ada Lovelace)
│   ├── unlock_engine.c        # In-RAM Falcon/GSP transaction pipeline
│   ├── vbios_loader.c         # Strict HW-ID ROM locator & parser
│   └── vbios_parser.c         # On-the-fly FWSEC parameter extraction
├── vendor/                    # Core unlock engine (unlock_610, edk2 types)
├── tools/
│   └── package_usb.py         # USB packaging automation script
├── build.py                   # Automated cross-compilation (w64devkit GCC/LD)
├── copy_to_usb.bat            # 1-click USB deployment batch file
├── copy_to_usb.ps1            # PowerShell deployment script
├── dump_vbios.bat             # 1-click Windows VBIOS dumping utility
└── dump_vbios.ps1             # PowerShell GPU auto-discovery & dumping logic
```

---

## 🛠 Building from Source

The project builds cleanly using the bundled `w64devkit` toolchain (GCC 16.2.0):

```powershell
python build.py
```

The script compiles all C units with `-O2`, links the PE32+ executable, and zeroes out the PE Import Directory to eliminate dynamic loading dependencies in the UEFI ImageLoader environment.

---

## 📜 Credits & References

This project builds upon the foundational reverse-engineering and open-source contributions of:

1. **[JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada)** — **Base Upstream Repository**  
   Special thanks for the pioneering breakthrough in unlocking the Ada Lovelace architecture, reverse-engineering the V67 Falcon transaction structure, and providing the first working UEFI implementation. This repository extends those concepts into a generalized multi-GPU, multi-architecture engine.

2. **[NVIDIA Open GPU Kernel Modules](https://github.com/NVIDIA/open-gpu-kernel-modules)**  
   Official NVIDIA open-source Linux kernel drivers, documentation of GSP initialization protocols, and Falcon microcontroller layout.

3. **The `gpu-unlock` Project & V67 Falcon Research Community**  
   Groundwork on Falcon firmware layout, SEC2 authentication bypass structures, V67 payload injection, and MMIO speed selector register mechanics.

4. **[Tianocore EDK II](https://github.com/tianocore/edk2)**  
   Unified Extensible Firmware Interface specifications, protocol definitions for `EFI_PCI_IO_PROTOCOL` and `EFI_GRAPHICS_OUTPUT_PROTOCOL`.

---

## ⚖️ License

* The EFI bootloader and supporting scripts are licensed under **GPL-2.0-only** in accordance with derivative work requirements from Linux kernel and NVIDIA open drivers.
* Microcode firmware binaries (`*.bin`) remain the intellectual property of NVIDIA Corporation and are governed by `NVIDIA-LICENSE`.

