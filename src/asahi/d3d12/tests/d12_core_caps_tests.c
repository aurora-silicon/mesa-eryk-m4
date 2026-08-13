/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_core_caps.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
   const struct d12_core_caps *caps = d12_core_get_caps();
   const uint32_t requests[] = {
      UINT32_C(0x0000c200),
      D12_CORE_FEATURE_LEVEL_11_0,
      UINT32_C(0x0000a100),
   };

   assert(caps != NULL);
   assert(caps->max_feature_level == D12_CORE_FEATURE_LEVEL_11_0);
   assert(caps->max_shader_model == D12_CORE_SHADER_MODEL_6_0);
   assert(caps->node_count == 1);
   assert(caps->tiled_resources_tier == 1);
   assert(caps->uma);
   assert(caps->tile_based_deferred_renderer);
   assert(d12_core_supports_feature_level(D12_CORE_FEATURE_LEVEL_11_0));
   assert(!d12_core_supports_feature_level(UINT32_C(0x0000a100)));
   assert(!d12_core_supports_feature_level(UINT32_C(0x0000c000)));
   assert(d12_core_choose_feature_level(requests, 3) ==
          D12_CORE_FEATURE_LEVEL_11_0);
   assert(d12_core_choose_feature_level(NULL, 3) == 0);
   assert(d12_core_choose_feature_level(requests, 0) == 0);

   puts("d12_core_caps_tests: PASS");
   return 0;
}
