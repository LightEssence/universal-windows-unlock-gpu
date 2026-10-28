/* SPDX-License-Identifier: GPL-2.0-only */
#include "unlock_engine.h"
#include "vbios_parser.h"
#include "uefi_base.h"

#define REG_SS0 0x82381cU
#define REG_SS1 0x823820U
#define VAL_SS0_UNLOCKED 0x88888888U
#define VAL_SS1_UNLOCKED 0x00000008U

static UINT32 ReadBar0(DETECTED_GPU *gpu, UINT32 offset)
{
    UINT32 val = 0xFFFFFFFFU;
    if (!gpu || !gpu->pci_io || !gpu->pci_io->Mem.Read) return val;
    gpu->pci_io->Mem.Read(gpu->pci_io, EfiPciIoWidthUint32, 0, offset, 1, &val);
    return val;
}

static VOID WriteBar0(DETECTED_GPU *gpu, UINT32 offset, UINT32 val)
{
    if (!gpu || !gpu->pci_io || !gpu->pci_io->Mem.Write) return;
    gpu->pci_io->Mem.Write(gpu->pci_io, EfiPciIoWidthUint32, 0, offset, 1, &val);
}

static UINT8 FindPcieCapOffset(DETECTED_GPU *gpu)
{
    UINT8 cap_ptr = 0;
    if (EFI_ERROR(gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint8, 0, 0x34, &cap_ptr))) {
        return 0;
    }

    UINTN iterations = 0;
    while (cap_ptr != 0 && iterations++ < 48) {
        UINT8 cap_id = 0, next_ptr = 0;
        gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint8, 0, cap_ptr, &cap_id);
        gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint8, 0, cap_ptr + 1, &next_ptr);
        if (cap_id == 0x10) { /* PCI Express Capability */
            return cap_ptr;
        }
        cap_ptr = next_ptr;
    }
    return 0;
}

static EFI_STATUS TriggerPcieFlr(DETECTED_GPU *gpu, UINT8 pcie_cap)
{
    if (pcie_cap == 0) return EFI_UNSUPPORTED;

    UINT32 devcap = 0;
    gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint32, 0, pcie_cap + 0x04, &devcap);
    if (!(devcap & (1u << 28))) {
        /* FLR bit in DEVCAP is not supported */
        return EFI_UNSUPPORTED;
    }

    /* Bit 15 in Device Control register (offset 0x08) triggers FLR */
    UINT16 devctl = 0;
    gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, 0, pcie_cap + 0x08, &devctl);
    devctl |= (1u << 15);
    gpu->pci_io->Pci.Write(gpu->pci_io, EfiPciIoWidthUint16, 0, pcie_cap + 0x08, &devctl);

    /* PCI-SIG specification requires at least 100ms wait after FLR */
    gBS->Stall(120000); /* 120 ms */

    /* Re-enable Memory Space (0x2) and Bus Master (0x4) */
    UINT16 cmd = 0;
    gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, 0, 0x04, &cmd);
    cmd |= 0x0006;
    gpu->pci_io->Pci.Write(gpu->pci_io, EfiPciIoWidthUint16, 0, 0x04, &cmd);

    return EFI_SUCCESS;
}

EFI_STATUS ExecuteGpuRamUnlock(DETECTED_GPU *gpu)
{
    if (!gpu || !gpu->vbios_loaded || !gpu->vbios_data) {
        return EFI_NOT_READY;
    }

    /* 1. Dynamic in-RAM verification of VBIOS FWSEC headers */
    DYNAMIC_FWSEC fwsec = { 0 };
    int parse_rc = ParseVbiosFwsec((const u8 *)gpu->vbios_data, gpu->vbios_size, &fwsec);
    if (parse_rc != 0) {
        return EFI_VOLUME_CORRUPTED;
    }

    /* 2. Verify GPU BAR0 responsiveness (Chip ID at BAR0 + 0) */
    UINT32 chip_id = ReadBar0(gpu, 0x0);
    if (chip_id == 0xFFFFFFFFU || chip_id == 0) {
        return EFI_DEVICE_ERROR;
    }

    /* 3. Locate PCIe Capability structure */
    UINT8 pcie_cap = FindPcieCapOffset(gpu);
    if (pcie_cap == 0) {
        return EFI_UNSUPPORTED;
    }

    /* 4. Write unlocked speed selectors directly into GPU shadow registers in RAM */
    WriteBar0(gpu, REG_SS0, VAL_SS0_UNLOCKED);
    WriteBar0(gpu, REG_SS1, VAL_SS1_UNLOCKED);

    /* 5. Issue Function Level Reset (FLR) to latch selectors and halt GSP cleanly */
    EFI_STATUS flr_status = TriggerPcieFlr(gpu, pcie_cap);
    if (EFI_ERROR(flr_status)) {
        return flr_status;
    }

    /* 6. Verify readback of latched selectors in BAR0 MMIO */
    UINT32 ss0_readback = ReadBar0(gpu, REG_SS0);
    UINT32 ss1_readback = ReadBar0(gpu, REG_SS1);

    if (ss0_readback == VAL_SS0_UNLOCKED && (ss1_readback & 0xF) == VAL_SS1_UNLOCKED) {
        gpu->unlocked_success = TRUE;
        return EFI_SUCCESS;
    }

    return EFI_DEVICE_ERROR;
}

