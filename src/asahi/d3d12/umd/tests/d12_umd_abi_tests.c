/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_umd_abi.h"
#include "../d12_umd_wddm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct agxw_adapter_identity
valid_identity(void)
{
   return (struct agxw_adapter_identity){
      .magic = AGXW_ADAPTER_IDENTITY_MAGIC,
      .version = AGXW_ADAPTER_IDENTITY_VERSION,
      .struct_size = sizeof(struct agxw_adapter_identity),
      .chip_id = UINT32_C(0x6020),
      .gpu_generation = 14,
      .gpu_variant = 'S',
      .gpu_revision = UINT32_C(0x10),
      .contract_version = AGXW_CONTRACT_VERSION,
      .features = UINT64_C(0x12345678),
      .params_blob_size = AGXW_PARAMS_GLOBAL_SIZE,
   };
}

int
main(void)
{
   struct agxw_adapter_identity input = valid_identity();
   struct agxw_adapter_identity output;

   memset(&output, 0, sizeof(output));
   assert(d12_umd_validate_identity(&input, sizeof(input), &output) ==
          D12_UMD_IDENTITY_OK);
   assert(memcmp(&input, &output, sizeof(input)) == 0);
   assert(d12_umd_validate_identity(NULL, sizeof(input), &output) ==
          D12_UMD_IDENTITY_NULL);
   assert(d12_umd_validate_identity(&input, sizeof(input) - 1, &output) ==
          D12_UMD_IDENTITY_SHORT);

   input.magic ^= 1;
   assert(d12_umd_validate_identity(&input, sizeof(input), &output) ==
          D12_UMD_IDENTITY_FOREIGN);
   input = valid_identity();
   input.version++;
   assert(d12_umd_validate_identity(&input, sizeof(input), &output) ==
          D12_UMD_IDENTITY_VERSION);
   input = valid_identity();
   input.struct_size--;
   assert(d12_umd_validate_identity(&input, sizeof(input), &output) ==
          D12_UMD_IDENTITY_SIZE);
   input = valid_identity();
   input.contract_version++;
   assert(d12_umd_validate_identity(&input, sizeof(input), &output) ==
          D12_UMD_IDENTITY_CONTRACT);
   input = valid_identity();
   input.params_blob_size--;
   assert(d12_umd_validate_identity(&input, sizeof(input), &output) ==
          D12_UMD_IDENTITY_PARAMS_SIZE);
   input = valid_identity();
   input.reserved = 1;
   assert(d12_umd_validate_identity(&input, sizeof(input), &output) ==
          D12_UMD_IDENTITY_RESERVED);

   assert(d12_umd_supported_version_count() ==
          (D12_UMD_TARGETS_WDDM23 ? 2u :
             (D12_UMD_TARGETS_WDDM21 ? 4u : 1u)));
   assert(d12_umd_supported_versions() != NULL);
#if D12_UMD_TARGETS_WDDM23
   assert(d12_umd_supported_versions()[0] ==
          D12_UMD_DDI_SUPPORTED_0032);
   assert(d12_umd_supported_versions()[1] ==
          D12_UMD_DDI_SUPPORTED_0003);
#elif D12_UMD_TARGETS_WDDM21
   assert(d12_umd_supported_versions()[0] ==
          D12_UMD_DDI_SUPPORTED_0013);
   assert(d12_umd_supported_versions()[1] ==
          D12_UMD_DDI_SUPPORTED_0011);
   assert(d12_umd_supported_versions()[2] ==
          D12_UMD_DDI_SUPPORTED_0010);
   assert(d12_umd_supported_versions()[3] ==
          D12_UMD_DDI_SUPPORTED_0003);
#else
   assert(d12_umd_supported_versions()[0] ==
          D12_UMD_DDI_SUPPORTED_0003);
#endif
   assert(d12_umd_version_supported(D12_UMD_DDI_INTERFACE_0003,
                                    D12_UMD_DDI_BUILD_0003));
   assert(d12_umd_version_supported(D12_UMD_DDI_INTERFACE_R1,
                                    D12_UMD_DDI_BUILD_0010) ==
          (bool)D12_UMD_TARGETS_WDDM21_EXACT);
   assert(d12_umd_version_supported(D12_UMD_DDI_INTERFACE_R1,
                                    D12_UMD_DDI_BUILD_0011) ==
          (bool)D12_UMD_TARGETS_WDDM21_EXACT);
   assert(d12_umd_version_supported(D12_UMD_DDI_INTERFACE_R1,
                                    D12_UMD_DDI_BUILD_0013) ==
          (bool)D12_UMD_TARGETS_WDDM21_EXACT);
   assert(!d12_umd_version_supported(D12_UMD_DDI_INTERFACE_R1, 2));
   assert(d12_umd_version_supported(D12_UMD_DDI_INTERFACE_R3,
                                    D12_UMD_DDI_BUILD_0032) ==
          (bool)D12_UMD_TARGETS_WDDM23);

   puts("d12_umd_abi_tests: PASS");
   return 0;
}
