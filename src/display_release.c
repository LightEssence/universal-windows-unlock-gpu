/* SPDX-License-Identifier: GPL-2.0-only */
#include "display_release.h"
#include "logger.h"
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

    LogWrite("[Release] Disconnecting UEFI GOP controller from handle...\n");

    /* Standard UEFI DisconnectController - completely board-agnostic */
    EFI_STATUS status = gBS->DisconnectController(gpu->handle, NULL, NULL);
    if (EFI_ERROR(status) && status != EFI_NOT_FOUND) {
        LogWrite("[Release] Error: DisconnectController failed: %#llx\n", (unsigned long long)status);
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
    /* Offset 4 = PCI Command Register. Count = 1 UINT16. */
    EFI_STATUS status = gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, 4, 1, &cmd);
    if (EFI_ERROR(status)) {
        LogWrite("[Decode] Failed to read PCI Command register (%#llx)\n", (unsigned long long)status);
        return status;
    }

    LogWrite("[Decode] PCI Command before: %#x\n", cmd);

    /* Bit 0: IO Space Enable (0x1), Bit 1: Memory Space Enable (0x2).
     * Bit 2 (Bus Master Enable) MUST BE 0 before Falcon transaction claim.
     * The transaction enables Bus Master itself when DMA buffers are mapped.
     */
    cmd = (cmd & ~0x0004) | 0x0003;
    status = gpu->pci_io->Pci.Write(gpu->pci_io, EfiPciIoWidthUint16, 4, 1, &cmd);
    if (EFI_ERROR(status)) {
        LogWrite("[Decode] Failed to write PCI Command register (%#llx)\n", (unsigned long long)status);
        return status;
    }

    UINT16 readback = 0;
    status = gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, 4, 1, &readback);
    if (EFI_ERROR(status)) {
        LogWrite("[Decode] Failed to read back PCI Command register (%#llx)\n", (unsigned long long)status);
        return status;
    }

    LogWrite("[Decode] PCI Command after: %#x (IO=%s, Memory=%s, Master=%s)\n",
             readback, (readback & 1) ? "ENABLED" : "DISABLED",
             (readback & 2) ? "ENABLED" : "DISABLED",
             (readback & 4) ? "ENABLED" : "DISABLED");

    /* Validate readback: Memory Space Enable (bit 1) must be set, Bus Master (bit 2) must be cleared */
    if ((readback & 0x0002) == 0 || (readback & 0x0004) != 0) {
        LogWrite("[Decode] Error: Contradictory PCI Command readback: %#x (expected Memory=1, Master=0)\n", readback);
        return EFI_DEVICE_ERROR;
    }

    return EFI_SUCCESS;
}

static EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static BOOLEAN CheckFileExistsOnBootVolume(CONST CHAR16 *path)
{
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_STATUS s = gBS->HandleProtocol(gImageHandle, &loaded_guid, (VOID **)&loaded);
    if (EFI_ERROR(s) || !loaded || !loaded->DeviceHandle) return FALSE;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    s = gBS->HandleProtocol(loaded->DeviceHandle, &fs_guid, (VOID **)&fs);
    if (EFI_ERROR(s) || !fs) return FALSE;

    EFI_FILE_HANDLE root = NULL;
    s = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(s) || !root) return FALSE;

    EFI_FILE_HANDLE f = NULL;
    s = root->Open(root, &f, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(s) && f) {
        f->Close(f);
        root->Close(root);
        return TRUE;
    }
    root->Close(root);
    return FALSE;
}

EFI_STATUS ReconnectGpuDisplay(DETECTED_GPU *gpu)
{
    if (!gpu || !gpu->handle) {
        return EFI_INVALID_PARAMETER;
    }

    if (!gpu->is_display_adapter) {
        return EFI_SUCCESS;
    }

    /* Check for override markers on boot media root */
    if (CheckFileExistsOnBootVolume(L"\\no-reconnect.txt") ||
        CheckFileExistsOnBootVolume(L"no-reconnect.txt")) {
        LogWrite("[Display] Reconnect skipped (no-reconnect.txt detected on boot media).\n"
                 "          bootmgfw.efi will self-establish GOP console on start.\n");
        return EFI_SUCCESS;
    }

    BOOLEAN force_recursive = CheckFileExistsOnBootVolume(L"\\tx-reconnect-recursive.txt") ||
                              CheckFileExistsOnBootVolume(L"tx-reconnect-recursive.txt");

    LogWrite("[Display] Preparing UEFI GOP reconnect (recursive=%s)...\n",
             force_recursive ? "TRUE" : "FALSE");

    /* 1. Ensure PCI Command has IO (0x1), Memory (0x2), and Bus Master (0x4) enabled BEFORE GOP connects */
    if (gpu->pci_io && gpu->pci_io->Pci.Read && gpu->pci_io->Pci.Write) {
        UINT16 cmd = 0;
        gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, 4, 1, &cmd);
        cmd |= 0x0007; /* Enable IO, Memory, and Bus Master for vendor GOP driver operation */
        gpu->pci_io->Pci.Write(gpu->pci_io, EfiPciIoWidthUint16, 4, 1, &cmd);
        LogWrite("[Display] PCI Command primed for GOP driver: %#06x\n", cmd);
    }

    /* 2. Wait 200ms for PCIe link and GPU firmware to settle post-FLR before binding GOP driver */
    LogWrite("[Display] Waiting 200ms for post-FLR link and GPU settling...\n");
    gBS->Stall(200000);

    /* 3. Reconnect GOP driver to GPU handle.
     * By default, recursive=FALSE binds the vendor GOP driver directly to the GPU handle,
     * producing EFI_GRAPHICS_OUTPUT_PROTOCOL for Windows Boot Manager without deadlocking
     * the firmware ConSplitter/ConsoleOut tree (as seen on modern ASUS/MSI/Gigabyte boards).
     */
    LogWrite("[Display] Calling gBS->ConnectController(gpu->handle, recursive=%s)...\n",
             force_recursive ? "TRUE" : "FALSE");
    EFI_STATUS status = gBS->ConnectController(gpu->handle, NULL, NULL, force_recursive);
    LogWrite("[Display] ConnectController on GPU handle returned %#llx\n", (unsigned long long)status);

    /* 4. If forced recursive, reconnect system console and reset text mode */
    if (force_recursive) {
        if (gST && gST->ConsoleOutHandle) {
            LogWrite("[Display] Reconnecting ConsoleOutHandle...\n");
            gBS->ConnectController(gST->ConsoleOutHandle, NULL, NULL, TRUE);
        }
        if (gST && gST->ConOut) {
            LogWrite("[Display] Resetting console...\n");
            gST->ConOut->Reset(gST->ConOut, FALSE);
            gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGRAY | EFI_BLACK);
        }
    }

    /* 5. Check if GOP protocol is available in the system for bootmgfw */
    UINTN gop_count = 0;
    EFI_HANDLE *gop_handles = NULL;
    EFI_STATUS g_stat = gBS->LocateHandleBuffer(ByProtocol, &GopGuid, NULL, &gop_count, &gop_handles);
    if (!EFI_ERROR(g_stat)) {
        LogWrite("[Display] Active GOP instances in system: %u\n", (unsigned)gop_count);
        if (gop_handles) gBS->FreePool(gop_handles);
    } else {
        LogWrite("[Display] Note: LocateHandleBuffer for GOP returned %#llx\n", (unsigned long long)g_stat);
    }

    LogWrite("[Display] Display reconnect sequence completed successfully\n");
    return EFI_SUCCESS;
}
