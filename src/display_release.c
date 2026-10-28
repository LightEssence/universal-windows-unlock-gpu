/* SPDX-License-Identifier: GPL-2.0-only */
#include "display_release.h"
#include "uefi_base.h"

EFI_STATUS ReleaseGpuDisplay(DETECTED_GPU *gpu)
{
    if (!gpu || !gpu->handle || !gpu->pci_io) {
        return EFI_INVALID_PARAMETER;
    }

    /* If this GPU is not holding GOP display console, no need to touch display drivers! */
    if (!gpu->is_display_adapter) {
        return EFI_SUCCESS;
    }

    /* Standard UEFI DisconnectController - completely board-agnostic */
    EFI_STATUS status = gBS->DisconnectController(gpu->handle, NULL, NULL);
    if (EFI_ERROR(status) && status != EFI_NOT_FOUND) {
        /* If already disconnected or not bound, ignore */
        return status;
    }

    /* Restore PCI Command register so MMIO / DMA continue functioning */
    return RestoreGpuPciDecode(gpu);
}

EFI_STATUS RestoreGpuPciDecode(DETECTED_GPU *gpu)
{
    if (!gpu || !gpu->pci_io || !gpu->pci_io->Pci.Read || !gpu->pci_io->Pci.Write) {
        return EFI_INVALID_PARAMETER;
    }

    UINT16 cmd = 0;
    EFI_STATUS status = gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, 0, 2, &cmd);
    if (EFI_ERROR(status)) return status;

    /* Bit 1: Memory Space Enable (0x2), Bit 2: Bus Master Enable (0x4) */
    cmd |= 0x0006;
    return gpu->pci_io->Pci.Write(gpu->pci_io, EfiPciIoWidthUint16, 0, 2, &cmd);
}

