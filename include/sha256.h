/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef UNIVERSAL_SHA256_H
#define UNIVERSAL_SHA256_H

#include "uefi_base.h"

VOID ComputeSha256(const VOID *Data, UINTN Size, UINT8 Digest[32]);
BOOLEAN VerifySha256(const VOID *Data, UINTN Size, const char *Hex64);

#endif /* UNIVERSAL_SHA256_H */

