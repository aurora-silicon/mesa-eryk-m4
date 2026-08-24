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
   /*
    * Feature level 12_0. Its three requirements over 11_1 are Resource
    * Binding Tier 2, Tiled Resources Tier 2 and typed UAV loads for the
    * additional format set; all three are implemented and asserted below and
    * in the host suite. 12_1 additionally requires conservative
    * rasterization and rasterizer-ordered views, neither of which exists
    * here.
    *
    * One inherited requirement is not met: Direct3D 11-class hardware is
    * expected to rasterize 8x MSAA and this driver offers at most 4x. That
    * is a limit of the driver and of the parts it supports rather than of
    * Apple hardware in general -- the descriptor's sample-count field is two
    * bits and an M5 reports 8x -- but AGX2 is what this driver targets,
    * Honeykrisp advertises 1/2/4 there, and AIL models no wider footprint.
    * The gap was equally real while this said 11_0, so it is not a
    * consequence of the level claimed. CheckFeatureSupport reports zero
    * quality levels for 8x, which is the discoverable, truthful answer an
    * application is required to consult.
    */
   .max_feature_level = D12_CORE_FEATURE_LEVEL_12_0,
   .max_shader_model = D12_CORE_SHADER_MODEL_6_2,
   .node_count = 1,
   /*
    * Resource Binding Tier 2. Tier 1's per-stage ceilings are 128 SRVs and
    * 16 samplers; this driver binds an unbounded descriptor range through a
    * rooted heap pointer rather than a per-register root slot, so a shader
    * addresses the whole shader-visible heap, and a sampler heap carries the
    * full 2,048 logical descriptors. Tier 2 also allows a bound descriptor
    * table to contain descriptors the shader does not access: every slot is
    * initialised to the resident null region at heap creation, so reading one
    * returns a null descriptor rather than faulting.
    *
    * One narrow deviation: AGX resolves the logical sampler heap into at most
    * 1,024 distinct native sampler states (Honeykrisp programs the same
    * bound). Exact deduplication means 2,048 logical samplers work whenever
    * 1,024 or fewer are distinct; beyond that the command list fails with a
    * named error rather than sampling through the wrong state. Raising that
    * bound needs hardware confirmation that the firmware accepts a larger
    * sampler heap, which has not been done.
    */
   .resource_binding_tier = 2,
   /*
    * Tiled Resources Tier 2. Over Tier 1 this requires three things, all
    * implemented and covered by host tests:
    *   - a mip level that is an integer multiple of the standard tile shape
    *     is never packed. AGX packs from the first level below one hardware
    *     tile and the standard tile is two hardware tiles per axis, so such a
    *     level is always twice the packing threshold
    *     (d12_sparse_standard_tile_level_is_nonpacked);
    *   - shader instructions for LOD clamp and for mapped status, which are
    *     the descriptor's minimum-LOD field and the hardware sparse feedback
    *     lane the DXIL translator publishes as CheckAccessFullyMapped; and
    *   - reads of NULL-mapped tiles return zero and writes to them are
    *     discarded, which the read-only sparse alias and the separate write
    *     addressing provide.
    * Tier 3 additionally requires tiled Texture3D, which is not implemented.
    */
   .tiled_resources_tier = 2,
   /*
    * Resource Heap Tier 2: one heap may hold buffers, non-render-target
    * textures and render-target/depth-stencil textures together.
    *
    * Tier 1 exists because a discrete GPU may place the three categories in
    * differently configured memory. AGX is UMA with one allocation kind, so
    * the category never affected placement here; the driver only ever
    * enforced the DENY flags an application itself asked for, which is
    * required at both tiers. Reporting Tier 1 made applications split
    * allocations into separate heaps for no reason.
    */
   .resource_heap_tier = 2,
   /*
    * Render passes, Tier 1.
    *
    * This is the feature a tile-based deferred renderer is structurally
    * better at than the immediate-mode hardware D3D12 was designed around.
    * An OMSetRenderTargets binding says nothing about the edges of the work,
    * so every attachment has to be seeded from memory on entry and written
    * back on exit; the per-attachment access types say directly when neither
    * is needed, and DISCARD then removes a whole memory round trip for the
    * render target rather than merely hinting at one.
    *
    * Tier 2 additionally promises that UAV writes inside a pass cost nothing
    * extra. They plausibly do not here -- a fragment shader writes a UAV the
    * same way inside a pass as outside one -- but that is a performance
    * claim, and it has not been measured on hardware.
    */
   .render_passes_tier = 1,
   /*
    * GPU upload heaps exist so a discrete GPU with a resizable BAR can let
    * the CPU write straight into device memory instead of staging through
    * system memory and copying. AGX is UMA: there is one pool, every
    * allocation is already both CPU-writable and GPU-addressable, and the
    * staging copy this heap type removes never had anything to do.
    */
   .gpu_upload_heap = true,
   /*
    * Programmable sample positions, tier 1.
    *
    * AGX already takes the sample pattern as data: PPP_MULTISAMPLECTL holds
    * one byte per sample, X in the low nibble and Y in the high one, in
    * sixteenths of a pixel from the top left. The driver was writing a fixed
    * default into it. Tier 1 is one pattern for the whole render target,
    * which is exactly the scope of that one register; tier 2 would need a
    * per-pixel pattern, which has nowhere to live.
    */
   .programmable_sample_positions_tier = 1,
   /*
    * Mesh shaders, tier 1 -- the only tier there is.
    *
    * The renderer already compiles mesh and amplification pipelines: the
    * mesh shader runs as compute, an amplification shader hands off through
    * DispatchMesh, and a generated ms_rast program feeds the hardware vertex
    * stage from the mesh output buffer. That path was built and then never
    * advertised, so nothing could reach it.
    */
   .mesh_shader_tier = 1,
   /* DXIL i64 becomes 64-bit NIR and the AGX backend lowers what it does not
    * execute natively. */
   .int64_shader_ops = true,
   /* The FRAGMENT_STENCIL state is emitted per face from independent
    * reference values and read/write masks. */
   .independent_front_and_back_stencil = true,
   /* AGX_PRIMITIVE_TRIANGLE_FAN is a native primitive, strip cut included. */
   .triangle_fan = true,
   .dynamic_index_buffer_strip_cut = true,
   /* AGX's sampler has a pixel-coordinates bit; the D3D12 flag drives it
    * directly rather than through a shader fallback. */
   .non_normalized_coordinate_samplers = true,
   .gpu_virtual_address_bits = 39,
   .uma = true,
   .cache_coherent_uma = false,
   .isolated_mmu = true,
   .tile_based_deferred_renderer = true,
   .native_16bit_shader_ops = true,
   .typed_uav_load_additional_formats = true,
   .wave_ops = true,
   .wave_lane_count_min = 32,
   .wave_lane_count_max = 32,
   /* The static policy cannot know the adapter's active core count. Report
    * one guaranteed hardware wave instead of inventing an adapter total. */
   .total_lane_count = 32,
   .predication = true,
   .hardware_copy = false,
   .copy_queue_timestamp_queries = true,
   .texture_copy_between_dimensions = true,
   .output_merger_logic_op = true,
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
