/* SPDX-License-Identifier: GPL-2.0-only */
#include "vbios_loader.h"
#include "uefi_base.h"

static EFI_GUID LoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID SimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;

static VOID StringToUpper(CONST CHAR16 *src, CHAR16 *dst, UINTN max_chars)
{
    UINTN i = 0;
    while (src[i] && i < max_chars - 1) {
        if (src[i] >= L'a' && src[i] <= L'z') {
            dst[i] = src[i] - (L'a' - L'A');
        } else {
            dst[i] = src[i];
        }
        i++;
    }
    dst[i] = L'\0';
}

static EFI_STATUS ReadRomFile(EFI_FILE_HANDLE root, CONST CHAR16 *path, VOID **out_buf, UINTN *out_size)
{
    EFI_FILE_HANDLE file = NULL;
    EFI_STATUS status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !file) {
        return status;
    }

    UINT8 info_buf[sizeof(EFI_FILE_INFO) + 256];
    UINTN info_size = sizeof(info_buf);
    status = file->GetInfo(file, &FileInfoGuid, &info_size, info_buf);
    if (EFI_ERROR(status)) {
        file->Close(file);
        return status;
    }

    EFI_FILE_INFO *info = (EFI_FILE_INFO *)info_buf;
    UINTN file_size = (UINTN)info->FileSize;

    /* NVIDIA VBIOS sanity: between 256 KiB and 4 MiB */
    if (file_size < 256 * 1024 || file_size > 4 * 1024 * 1024) {
        file->Close(file);
        return EFI_BAD_BUFFER_SIZE;
    }

    VOID *data = NULL;
    status = gBS->AllocatePool(EfiBootServicesData, file_size, &data);
    if (EFI_ERROR(status) || !data) {
        file->Close(file);
        return status;
    }

    UINTN read_bytes = file_size;
    status = file->Read(file, &read_bytes, data);
    file->Close(file);

    if (EFI_ERROR(status) || read_bytes != file_size) {
        gBS->FreePool(data);
        return EFI_DEVICE_ERROR;
    }

    /* Basic Option ROM validation: 0x55, 0xAA header */
    UINT8 *bytes = (UINT8 *)data;
    if (bytes[0] != 0x55 || bytes[1] != 0xAA) {
        gBS->FreePool(data);
        return EFI_COMPROMISED_DATA;
    }

    *out_buf = data;
    *out_size = file_size;
    return EFI_SUCCESS;
}

EFI_STATUS LoadGpuVbiosStrictId(DETECTED_GPU *target_gpu, CONST DETECTED_GPU all_gpus[], UINTN gpu_count)
{
    if (!target_gpu) return EFI_INVALID_PARAMETER;

    /* Check if an identical sibling card has already loaded this exact ROM in RAM */
    for (UINTN j = 0; j < gpu_count; j++) {
        CONST DETECTED_GPU *sibling = &all_gpus[j];
        if (sibling != target_gpu && sibling->vbios_loaded && sibling->vbios_data) {
            if (sibling->vendor_id == target_gpu->vendor_id &&
                sibling->device_id == target_gpu->device_id &&
                sibling->subsys_vendor_id == target_gpu->subsys_vendor_id &&
                sibling->subsys_device_id == target_gpu->subsys_device_id) {
                /* Identical hardware IDs share the same RAM buffer cleanly */
                target_gpu->vbios_data = sibling->vbios_data;
                target_gpu->vbios_size = sibling->vbios_size;
                target_gpu->vbios_loaded = TRUE;
                return EFI_SUCCESS;
            }
        }
    }

    /* Locate boot file system (the partition we booted from) */
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_STATUS status = gBS->HandleProtocol(gImageHandle, &LoadedImageProtocolGuid, (VOID **)&loaded_image);
    if (EFI_ERROR(status) || !loaded_image || !loaded_image->DeviceHandle) {
        return EFI_NOT_FOUND;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    status = gBS->HandleProtocol(loaded_image->DeviceHandle, &SimpleFileSystemProtocolGuid, (VOID **)&fs);
    if (EFI_ERROR(status) || !fs) {
        return EFI_NOT_FOUND;
    }

    EFI_FILE_HANDLE root = NULL;
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || !root) {
        return EFI_NOT_FOUND;
    }

    /* Try exact strict ID path (lowercase) */
    VOID *data = NULL;
    UINTN size = 0;
    status = ReadRomFile(root, target_gpu->rom_filename, &data, &size);

    /* If not found, try uppercase variation (e.g. \EFI\VBIOS\10DE_2782_1458_40E8.ROM) */
    if (EFI_ERROR(status)) {
        CHAR16 upper_path[64];
        StringToUpper(target_gpu->rom_filename, upper_path, ARRAY_SIZE(upper_path));
        status = ReadRomFile(root, upper_path, &data, &size);
    }

    root->Close(root);

    if (!EFI_ERROR(status)) {
        target_gpu->vbios_data = data;
        target_gpu->vbios_size = size;
        target_gpu->vbios_loaded = TRUE;
    }

    return status;
}

VOID FreeGpuVbios(DETECTED_GPU *gpu)
{
    if (gpu && gpu->vbios_loaded && gpu->vbios_data) {
        /* Note: Shared sibling references will be cleaned once pool is freed */
        gBS->FreePool(gpu->vbios_data);
        gpu->vbios_data = NULL;
        gpu->vbios_size = 0;
        gpu->vbios_loaded = FALSE;
    }
}
