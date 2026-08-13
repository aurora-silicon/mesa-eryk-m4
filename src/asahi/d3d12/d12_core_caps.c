/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_core_caps.h"

/*
 * This is capability policy, not a hardware wish list.  Values are raised
 * only when every frontend can honour the corresponding D3D12 contract.
 */
static const struct d12_core_caps d12_agx_caps = {
   .max_feature_level = D12_CORE_FEATURE_LEVEL_11_0,
   .max_shader_model = D12_CORE_SHADER_MODEL_6_2,
   .node_count = 1,
   .resource_binding_tier = 1,
   .tiled_resources_tier = 1,
   .resource_heap_tier = 1,
   .uma = true,
   .cache_coherent_uma = true,
   .isolated_mmu = true,
   .tile_based_deferred_renderer = true,
   .native_16bit_shader_ops = true,
   .predication = true,
   .hardware_copy = false,
   .copy_queue_timestamp_queries = true,
   .write_buffer_immediate_support_flags =
      D12_CORE_COMMAND_LIST_SUPPORT_DIRECT |
      D12_CORE_COMMAND_LIST_SUPPORT_COMPUTE |
      D12_CORE_COMMAND_LIST_SUPPORT_COPY,
};

const struct d12_core_caps *
d12_core_get_caps(void)
{
   return &d12_agx_caps;
}

bool
d12_core_supports_feature_level(uint32_t minimum_feature_level)
{
   return minimum_feature_level >= D12_CORE_FEATURE_LEVEL_11_0 &&
          minimum_feature_level <= d12_agx_caps.max_feature_level;
}

uint32_t
d12_core_choose_feature_level(const uint32_t *requested,
                              uint32_t requested_count)
{
   uint32_t best = 0;

   if (!requested)
      return 0;

   for (uint32_t i = 0; i < requested_count; ++i) {
      uint32_t level = requested[i];
      if (d12_core_supports_feature_level(level) && level > best)
         best = level;
   }

   return best;
}
