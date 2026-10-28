/* SPDX-License-Identifier: GPL-2.0-only */
#include "unlock_engine.h"
#include "vbios_parser.h"
#include "logger.h"
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
    /* Read Capabilities Pointer at configuration space byte offset 0x34. Count = 1 UINT8. */
    if (EFI_ERROR(gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint8, 0x34, 1, &cap_ptr))) {
        return 0;
    }

    UINTN iterations = 0;
    while (cap_ptr != 0 && iterations++ < 48) {
        UINT8 cap_id = 0, next_ptr = 0;
        gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint8, cap_ptr, 1, &cap_id);
        gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint8, cap_ptr + 1, 1, &next_ptr);
        if (cap_id == 0x10) { /* PCI Express Capability ID */
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
    /* Device Capabilities at pcie_cap + 0x04. Count = 1 UINT32. */
    gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint32, pcie_cap + 0x04, 1, &devcap);
    if (!(devcap & (1u << 28))) {
        LogWrite("[FLR] Error: PCIe FLR capability not supported (devcap=%#x)\n", devcap);
        return EFI_UNSUPPORTED;
    }

    /* Bit 15 in Device Control register (offset pcie_cap + 0x08) triggers FLR */
    UINT16 devctl = 0;
    gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, pcie_cap + 0x08, 1, &devctl);
    devctl |= (1u << 15);
    gpu->pci_io->Pci.Write(gpu->pci_io, EfiPciIoWidthUint16, pcie_cap + 0x08, 1, &devctl);

    LogWrite("[FLR] Issued FLR trigger; waiting for transaction quiescence...\n");

    /* PCI-SIG specification requires at least 100ms wait after FLR */
    gBS->Stall(120000); /* 120 ms */

    /* Wait for Transactions Pending (bit 5) in Device Status register (pcie_cap + 0x0a) to clear */
    UINT16 devsta = 0;
    for (UINTN w = 0; w < 100; w++) {
        gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, pcie_cap + 0x0a, 1, &devsta);
        if (!(devsta & (1u << 5))) break;
        gBS->Stall(5000);
    }

    /* Re-enable Memory Space (0x2) and Bus Master (0x4) at Command Register (offset 0x04) */
    UINT16 cmd = 0;
    gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, 0x04, 1, &cmd);
    cmd |= 0x0006;
    gpu->pci_io->Pci.Write(gpu->pci_io, EfiPciIoWidthUint16, 0x04, 1, &cmd);

    UINT16 cmd_verify = 0;
    gpu->pci_io->Pci.Read(gpu->pci_io, EfiPciIoWidthUint16, 0x04, 1, &cmd_verify);
    LogWrite("[FLR] Complete. PCI Command restored to %#x (Decode=%s)\n",
             cmd_verify, (cmd_verify & 6) == 6 ? "OK" : "FAILED");

    return (cmd_verify & 6) == 6 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS ExecuteGpuRamUnlock(DETECTED_GPU *gpu)
{
    if (!gpu || !gpu->vbios_loaded || !gpu->vbios_data) {
        LogWrite("[Unlock] Error: VBIOS data not present in RAM\n");
        return EFI_NOT_READY;
    }

    /* 1. Dynamic in-RAM verification of VBIOS FWSEC headers */
    DYNAMIC_FWSEC fwsec = { 0 };
    int parse_rc = ParseVbiosFwsec((const u8 *)gpu->vbios_data, gpu->vbios_size, &fwsec);
    LogWrite("[Unlock] Stage 1 (FWSEC parse): rc=%d, img_off=%#x, img_size=%#x, sig_off=%#x\n",
             parse_rc, fwsec.image_off, fwsec.image_size, fwsec.sig_off);
    if (parse_rc != 0) {
        LogWrite("[Unlock] FWSEC parse error! Aborting.\n");
        return EFI_VOLUME_CORRUPTED;
    }

    /* 2. Verify GPU BAR0 responsiveness (Chip ID at BAR0 + 0) */
    UINT32 chip_id = ReadBar0(gpu, 0x0);
    LogWrite("[Unlock] Stage 2 (BAR0 check): chip_id=%#x at base %#llx\n",
             chip_id, (unsigned long long)gpu->bar0_base);
    if (chip_id == 0xFFFFFFFFU || chip_id == 0) {
        LogWrite("[Unlock] Error: GPU BAR0 not responding!\n");
        return EFI_DEVICE_ERROR;
    }

    /* Read cold selectors before write */
    UINT32 ss0_cold = ReadBar0(gpu, REG_SS0);
    UINT32 ss1_cold = ReadBar0(gpu, REG_SS1);
    LogWrite("[Unlock] Cold selectors before write: SS0=%#x, SS1=%#x\n", ss0_cold, ss1_cold);

    /* 3. Locate PCIe Capability structure */
    UINT8 pcie_cap = FindPcieCapOffset(gpu);
    LogWrite("[Unlock] Stage 3 (PCIe cap): offset=%#x\n", pcie_cap);
    if (pcie_cap == 0) {
        LogWrite("[Unlock] Error: PCIe capability not found\n");
        return EFI_UNSUPPORTED;
    }

    /* 4. Write unlocked speed selectors directly into GPU shadow registers in RAM */
    LogWrite("[Unlock] Stage 4: Writing SS0=%#x to %#x, SS1=%#x to %#x...\n",
             VAL_SS0_UNLOCKED, REG_SS0, VAL_SS1_UNLOCKED, REG_SS1);
    WriteBar0(gpu, REG_SS0, VAL_SS0_UNLOCKED);
    WriteBar0(gpu, REG_SS1, VAL_SS1_UNLOCKED);

    /* 5. Issue Function Level Reset (FLR) to latch selectors and halt GSP cleanly */
    LogWrite("[Unlock] Stage 5: Triggering PCIe FLR...\n");
    EFI_STATUS flr_status = TriggerPcieFlr(gpu, pcie_cap);
    if (EFI_ERROR(flr_status)) {
        LogWrite("[Unlock] FLR failed: status=%#llx\n", (unsigned long long)flr_status);
        return flr_status;
    }

    /* 6. Verify readback of latched selectors in BAR0 MMIO */
    UINT32 ss0_readback = ReadBar0(gpu, REG_SS0);
    UINT32 ss1_readback = ReadBar0(gpu, REG_SS1);
    LogWrite("[Unlock] Stage 6 (Readback): SS0=%#x, SS1=%#x\n", ss0_readback, ss1_readback);

    if (ss0_readback == VAL_SS0_UNLOCKED && (ss1_readback & 0xF) == VAL_SS1_UNLOCKED) {
        gpu->unlocked_success = TRUE;
        LogWrite("[Unlock] SUCCESS: SS0/SS1 correctly latched in RAM!\n");
        return EFI_SUCCESS;
    }

    LogWrite("[Unlock] FAILED: Selectors did not latch (expected SS0=%#x, SS1=%#x)\n",
             VAL_SS0_UNLOCKED, VAL_SS1_UNLOCKED);
    return EFI_DEVICE_ERROR;
}
