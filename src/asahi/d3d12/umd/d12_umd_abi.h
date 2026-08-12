/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Portable policy at the WDDM UMD boundary.  This file intentionally has no
 * dependency on Windows headers so malformed KMD replies and version
 * negotiation can be unit-tested on the build host.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agxw_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/* D3D12DDI_SUPPORTED_0003 from the pinned WDK 10.0.26100.0 header. */
#define D12_UMD_DDI_SUPPORTED_0003 UINT64_C(0x000c000200080000)
#define D12_UMD_DDI_SUPPORTED_0010 UINT64_C(0x000c000a00000000)
#define D12_UMD_DDI_SUPPORTED_0011 UINT64_C(0x000c000a00010000)

#define D12_UMD_DDI_INTERFACE_0003 UINT32_C(0x000c0002)
#define D12_UMD_DDI_INTERFACE_R1 UINT32_C(0x000c000a)
#define D12_UMD_DDI_BUILD_0003 UINT32_C(8)
#define D12_UMD_DDI_BUILD_0010 UINT32_C(0)
#define D12_UMD_DDI_BUILD_0011 UINT32_C(1)

enum d12_umd_identity_result {
   D12_UMD_IDENTITY_OK = 0,
   D12_UMD_IDENTITY_NULL,
   D12_UMD_IDENTITY_SHORT,
   D12_UMD_IDENTITY_FOREIGN,
   D12_UMD_IDENTITY_VERSION,
   D12_UMD_IDENTITY_SIZE,
   D12_UMD_IDENTITY_CONTRACT,
   D12_UMD_IDENTITY_PARAMS_SIZE,
   D12_UMD_IDENTITY_RESERVED,
};

enum d12_umd_identity_result
d12_umd_validate_identity(const void *blob, size_t blob_size,
                          struct agxw_adapter_identity *identity);

size_t d12_umd_supported_version_count(void);

const uint64_t *d12_umd_supported_versions(void);
bool d12_umd_version_supported(uint32_t interface_version,
                               uint32_t build_version);

#ifdef __cplusplus
}
#endif
