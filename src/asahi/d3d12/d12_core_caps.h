/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral capability policy for the native AGX D3D12 renderer.
 *
 * Keep this header free of COM, WDK and Mesa types.  Both the public D3D12
 * COM frontend and the WDDM DDI frontend translate this single policy into
 * their respective ABI structures.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D12_CORE_FEATURE_LEVEL_11_0 UINT32_C(0x0000b000)
#define D12_CORE_SHADER_MODEL_6_0 UINT32_C(0x00000060)
#define D12_CORE_COMMAND_LIST_SUPPORT_DIRECT UINT32_C(0x1)
#define D12_CORE_COMMAND_LIST_SUPPORT_COMPUTE UINT32_C(0x4)
#define D12_CORE_COMMAND_LIST_SUPPORT_COPY UINT32_C(0x8)

struct d12_core_caps {
   uint32_t max_feature_level;
   uint32_t max_shader_model;
   uint32_t node_count;
   uint32_t resource_binding_tier;
   uint32_t tiled_resources_tier;
   uint32_t resource_heap_tier;
   bool uma;
   bool cache_coherent_uma;
   bool isolated_mmu;
   bool tile_based_deferred_renderer;
   bool native_16bit_shader_ops;
   bool copy_queue_timestamp_queries;
   uint32_t write_buffer_immediate_support_flags;
};

const struct d12_core_caps *d12_core_get_caps(void);

bool d12_core_supports_feature_level(uint32_t minimum_feature_level);

uint32_t d12_core_choose_feature_level(const uint32_t *requested,
                                       uint32_t requested_count);

#ifdef __cplusplus
}
#endif
