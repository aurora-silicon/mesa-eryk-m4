/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_abi.h"
#include "d12_umd_wddm.h"

#include <string.h>

static const uint64_t d12_umd_versions[] = {
#if D12_UMD_TARGETS_WDDM31
   D12_UMD_DDI_SUPPORTED_0099,
#elif D12_UMD_TARGETS_WDDM24
   D12_UMD_DDI_SUPPORTED_0042,
#elif D12_UMD_TARGETS_WDDM23_EXACT
   D12_UMD_DDI_SUPPORTED_0032,
#elif D12_UMD_TARGETS_WDDM21_EXACT
   D12_UMD_DDI_SUPPORTED_0013,
   D12_UMD_DDI_SUPPORTED_0011,
   D12_UMD_DDI_SUPPORTED_0010,
#endif
   D12_UMD_DDI_SUPPORTED_0003,
};

enum d12_umd_identity_result
d12_umd_validate_identity(const void *blob, size_t blob_size,
                          struct agxw_adapter_identity *identity)
{
   struct agxw_adapter_identity parsed;

   if (!blob || !identity)
      return D12_UMD_IDENTITY_NULL;
   if (blob_size < sizeof(parsed))
      return D12_UMD_IDENTITY_SHORT;

   memcpy(&parsed, blob, sizeof(parsed));
   if (parsed.magic != AGXW_ADAPTER_IDENTITY_MAGIC)
      return D12_UMD_IDENTITY_FOREIGN;
   if (parsed.version != AGXW_ADAPTER_IDENTITY_VERSION)
      return D12_UMD_IDENTITY_VERSION;
   if (parsed.struct_size != sizeof(parsed))
      return D12_UMD_IDENTITY_SIZE;
   if (parsed.contract_version != AGXW_CONTRACT_VERSION)
      return D12_UMD_IDENTITY_CONTRACT;
   if (parsed.params_blob_size != AGXW_PARAMS_GLOBAL_SIZE)
      return D12_UMD_IDENTITY_PARAMS_SIZE;
   if (parsed.submit_private_max_version < AGXW_SUBMIT_PRIVATE_VERSION_V4)
      return D12_UMD_IDENTITY_SUBMIT_VERSION;

   *identity = parsed;
   return D12_UMD_IDENTITY_OK;
}

size_t
d12_umd_supported_version_count(void)
{
   return sizeof(d12_umd_versions) / sizeof(d12_umd_versions[0]);
}

const uint64_t *
d12_umd_supported_versions(void)
{
   return d12_umd_versions;
}

bool
d12_umd_version_supported(uint32_t interface_version,
                          uint32_t build_version)
{
   return (interface_version == D12_UMD_DDI_INTERFACE_0003 &&
           build_version == D12_UMD_DDI_BUILD_0003)
#if D12_UMD_TARGETS_WDDM31
          ||
          (interface_version == D12_UMD_DDI_INTERFACE_R8 &&
           build_version == D12_UMD_DDI_BUILD_0099)
#elif D12_UMD_TARGETS_WDDM24
          ||
          (interface_version == D12_UMD_DDI_INTERFACE_R4 &&
           build_version == D12_UMD_DDI_BUILD_0042)
#elif D12_UMD_TARGETS_WDDM23_EXACT
          ||
          (interface_version == D12_UMD_DDI_INTERFACE_R3 &&
           build_version == D12_UMD_DDI_BUILD_0032)
#elif D12_UMD_TARGETS_WDDM21_EXACT
          ||
          (interface_version == D12_UMD_DDI_INTERFACE_R1 &&
           (build_version == D12_UMD_DDI_BUILD_0010 ||
            build_version == D12_UMD_DDI_BUILD_0011 ||
            build_version == D12_UMD_DDI_BUILD_0013))
#endif
      ;
}
