/* SPDX-License-Identifier: GPL-2.0-only */
#include "asset_loader.h"
#include "logger.h"
#include "port.h"
#include "profile.h"
#include "compat/compat.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

static EFI_GUID LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID FsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID InfoGuid = EFI_FILE_INFO_ID;

static EFI_STATUS ReadAssetFile(EFI_FILE_HANDLE root, const CHAR16 *dir_name, const CHAR16 *file_name, UINTN expected, VOID **data)
{
    *data = NULL;
    EFI_FILE_HANDLE f = NULL;
    CHAR16 path[128];
    UINTN pos = 0;

    for (UINTN i = 0; dir_name[i] != 0 && pos < 120; i++) path[pos++] = dir_name[i];
    for (UINTN i = 0; file_name[i] != 0 && pos < 127; i++) path[pos++] = file_name[i];
    path[pos] = 0;

    EFI_STATUS s = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s) || !f) return s;

    UINT64 info_words[128];
    UINTN info_bytes = sizeof(info_words);
    s = f->GetInfo(f, &InfoGuid, &info_bytes, info_words);
    EFI_FILE_INFO *info = (EFI_FILE_INFO *)info_words;
    if (EFI_ERROR(s) || info->FileSize != expected || (info->Attribute & EFI_FILE_DIRECTORY)) {
        f->Close(f);
        return EFI_COMPROMISED_DATA;
    }

    VOID *p = GuAlloc(expected);
    if (!p) {
        f->Close(f);
        return EFI_OUT_OF_RESOURCES;
    }

    for (UINTN at = 0; at < expected;) {
        UINTN n = expected - at;
        if (n > 1048576) n = 1048576;
        UINTN wanted = n;
        s = f->Read(f, &n, (UINT8 *)p + at);
        if (EFI_ERROR(s) || n != wanted) {
            f->Close(f);
            GuFree(p);
            return EFI_DEVICE_ERROR;
        }
        at += n;
    }

    f->Close(f);
    *data = p;
    return EFI_SUCCESS;
}

EFI_STATUS LoadFirmwareAssets(EFI_HANDLE image_handle)
{
    static const CHAR16 *dirs[] = {
        L"\\AD104-ASSETS\\",
        L"\\EFI\\ASSETS\\"
    };
    static const CHAR16 *files[] = {
        L"booter_load_ad102.bin",
        L"v67-ad102-610.43.03-four-plm.bin",
        L"gsp_ga10x.bin",
        L"gsp_boot_ad102.bin",
        L"gsp_boot_desc_ad102.bin"
    };
    static const char *full_names[] = {
        "gpu-unlock/610.43.03/booter_load_ad102.bin",
        "gpu-unlock/610.43.03/v67-ad102-610.43.03-four-plm.bin",
        "gpu-unlock/610.43.03/gsp_ga10x.bin",
        "gpu-unlock/610.43.03/gsp_boot_ad102.bin",
        "gpu-unlock/610.43.03/gsp_boot_desc_ad102.bin"
    };
    static const UINTN sizes[] = { 57672, 64000, 84277400, 36864, 84 };

    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_STATUS s = gBS->HandleProtocol(image_handle, &LoadedGuid, (VOID **)&loaded);
    if (EFI_ERROR(s) || !loaded || !loaded->DeviceHandle) return s;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    s = gBS->HandleProtocol(loaded->DeviceHandle, &FsGuid, (VOID **)&fs);
    if (EFI_ERROR(s) || !fs) return s;

    EFI_FILE_HANDLE root = NULL;
    s = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(s) || !root) return s;

    /* Try each asset directory until all files are successfully loaded */
    BOOLEAN loaded_all = FALSE;
    for (UINTN d = 0; d < 2 && !loaded_all; d++) {
        BOOLEAN dir_ok = TRUE;
        for (UINTN i = 0; i < 5; i++) {
            VOID *data = NULL;
            s = ReadAssetFile(root, dirs[d], files[i], sizes[i], &data);
            if (EFI_ERROR(s)) {
                dir_ok = FALSE;
                break;
            }
            GuAssets[i].name = full_names[i];
            GuAssets[i].firmware.size = sizes[i];
            GuAssets[i].firmware.data = data;
        }
        if (dir_ok) {
            loaded_all = TRUE;
            LogWrite("[Assets] Successfully loaded all 5 V67 assets from %ls\n", dirs[d]);
        }
    }

    root->Close(root);

    if (!loaded_all) {
        LogWrite("[Assets] Error: Could not locate V67 firmware assets on boot media!\n");
        return EFI_NOT_FOUND;
    }

    /* Verify cryptographic SHA-256 signatures of all loaded payloads */
    if (!GuAd104AssetsValid()) {
        LogWrite("[Assets] Error: Asset SHA-256 hash validation failed!\n");
        return EFI_CRC_ERROR;
    }

    LogWrite("[Assets] Verified 5 V67 assets SHA-256 pinned in RAM (84.4 MB total)\n");
    return EFI_SUCCESS;
}
