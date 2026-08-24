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
   assert(caps->max_feature_level == D12_CORE_FEATURE_LEVEL_12_0);
   /* The three requirements 12_0 adds over 11_1. */
   assert(caps->resource_binding_tier >= 2);
   assert(caps->tiled_resources_tier >= 2);
   assert(caps->typed_uav_load_additional_formats);
   assert(caps->max_shader_model == D12_CORE_SHADER_MODEL_6_2);
   assert(caps->node_count == 1);
   assert(caps->tiled_resources_tier == 2);
   /* UMA: one heap holds every resource category. */
   assert(caps->resource_heap_tier == 2);
   /* A tile-based deferred renderer implements render passes natively. */
   assert(caps->render_passes_tier >= 1);
   /* PPP_MULTISAMPLECTL is one pattern per render: tier 1, never tier 2. */
   assert(caps->programmable_sample_positions_tier == 1);
   /* UMA: the CPU can already write memory the GPU reads as a default heap. */
   assert(caps->gpu_upload_heap);
   assert(caps->gpu_virtual_address_bits == 39);
   assert(caps->uma);
   assert(!caps->cache_coherent_uma);
   assert(caps->tile_based_deferred_renderer);
   assert(caps->typed_uav_load_additional_formats);
   assert(caps->wave_ops);
   assert(caps->wave_lane_count_min == 32);
   assert(caps->wave_lane_count_max == 32);
   assert(caps->total_lane_count == 32);
   assert(caps->predication);
   assert(!caps->hardware_copy);
   assert(caps->copy_queue_timestamp_queries);
   assert(caps->texture_copy_between_dimensions);
   assert(caps->output_merger_logic_op);
   assert(caps->write_buffer_immediate_support_flags ==
          (D12_CORE_COMMAND_LIST_SUPPORT_DIRECT |
           D12_CORE_COMMAND_LIST_SUPPORT_COMPUTE |
           D12_CORE_COMMAND_LIST_SUPPORT_COPY));
   assert(d12_core_supports_feature_level(D12_CORE_FEATURE_LEVEL_11_0));
   assert(d12_core_supports_feature_level(D12_CORE_FEATURE_LEVEL_11_1));
   assert(d12_core_supports_feature_level(D12_CORE_FEATURE_LEVEL_12_0));
   /* 12_1 needs conservative rasterization and ROVs, which do not exist. */
   assert(!d12_core_supports_feature_level(D12_CORE_FEATURE_LEVEL_12_1));
   /* 10_1 is below the D3D12 floor. */
   assert(!d12_core_supports_feature_level(UINT32_C(0x0000a100)));
   /* 12_2 is above what this driver reports. */
   assert(!d12_core_supports_feature_level(UINT32_C(0x0000c200)));
   assert(d12_core_choose_feature_level(requests, 3) ==
          D12_CORE_FEATURE_LEVEL_11_0);
   assert(d12_core_choose_feature_level(NULL, 3) == 0);
   assert(d12_core_choose_feature_level(requests, 0) == 0);

   puts("d12_core_caps_tests: PASS");
   return 0;
}
