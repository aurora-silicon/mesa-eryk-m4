/* SPDX-License-Identifier: MIT */
#pragma once
#include "compiler/nir/nir.h"
#include "util/shader_stats.h"
struct bridge_agx_cf_binding {
   /* Base coefficient register */
   uint8_t cf_base;

   /* Slot being bound */
   gl_varying_slot slot : 8;

   /* First component bound.
    *
    * Must be 2 (Z) or 3 (W) if slot == VARYING_SLOT_POS.
    */
   unsigned offset : 2;

   /* Number of components bound */
   unsigned count : 3;

   /* Is smooth shading enabled? If false, flat shading is used */
   bool smooth : 1;

   /* Perspective correct interpolation */
   bool perspective : 1;

   uint8_t pad;
};

/* Conservative bound, * 4 due to offsets (TODO: maybe worth eliminating
 * coefficient register aliasing?)
 */
#define BRIDGE_MAX_CF_BINDINGS (VARYING_SLOT_MAX * 4)

struct bridge_agx_varyings_fs {
   /* Number of coefficient registers used */
   unsigned nr_cf;

   /* Number of coefficient register bindings */
   unsigned nr_bindings;

   /* Whether gl_FragCoord.z is read */
   bool reads_z;

   /* Coefficient register bindings */
   struct bridge_agx_cf_binding bindings[BRIDGE_MAX_CF_BINDINGS];
};

union bridge_agx_varyings {
   struct bridge_agx_varyings_fs fs;
};

struct bridge_agx_interp_info {
   /* Bit masks indexed by I/O location of flat and linear varyings */
   uint64_t flat;
   uint64_t linear;
};
static_assert(sizeof(struct bridge_agx_interp_info) == 16, "packed");

struct bridge_agx_rodata {
   /* Offset in the binary */
   uint32_t offset;

   /* Base uniform to map constants */
   uint16_t base_uniform;

   /* Number of 16-bit constants to map contiguously there */
   uint16_t size_16;
};

struct bridge_agx_shader_info {
   mesa_shader_stage stage;
   uint32_t binary_size;

   union bridge_agx_varyings varyings;

   /* Number of uniforms */
   uint16_t push_count;

   /* Local memory allocation in bytes */
   uint16_t local_size;

   /* Local imageblock allocation in bytes per thread */
   uint16_t imageblock_stride;

   /* Scratch memory allocation in bytes for main/preamble respectively */
   unsigned scratch_size, preamble_scratch_size;

   /* Size in bytes of the main sahder */
   unsigned main_size;

   /* Does the shader have a preamble? If so, it is at offset preamble_offset.
    * The main shader is at offset main_offset. The preamble is executed first.
    */
   bool has_preamble;
   unsigned preamble_offset, main_offset;

   /* Does the shader read the tilebuffer? */
   bool reads_tib;

   /* Does the shader require early fragment tests? */
   bool early_fragment_tests;

   /* Does the shader potentially draw to a nonzero viewport? */
   bool nonzero_viewport;

   /* Does the shader write layer and/or viewport index? Written together */
   bool writes_layer_viewport;

   /* Does the shader control the sample mask? */
   bool writes_sample_mask;

   /* Depth layout, never equal to NONE */
   enum gl_frag_depth_layout depth_layout;

   /* Based only the compiled shader, should tag writes be disabled? This is set
    * based on what is outputted. Note if rasterizer discard is used, that needs
    * to disable tag writes regardless of this flag.
    */
   bool tag_write_disable;

   /* Shader is incompatible with triangle merging */
   bool disable_tri_merging;

   /* Reads draw ID system value */
   bool uses_draw_id;

   /* Reads base vertex/instance */
   bool uses_base_param;

   /* Uses txf and hence needs a txf sampler mapped */
   bool uses_txf;

   /* Potentially uses the sampler heap (conservative) */
   bool uses_sampler_heap;

   /* Number of texture/sampler state registers pushed by the preamble. */
   uint8_t texture_state_count, sampler_state_count;

   /* Number of 16-bit registers used by the main shader and preamble
    * respectively.
    */
   uint16_t nr_gprs, nr_preamble_gprs;

   /* Separate AGX3 staging allocation, in 16-bit halves. */
   uint16_t agx3_staging_halfs;
   bool agx3_staging_known;

   /* Output mask set during driver lowering */
   uint64_t outputs;

   /* Workgroup size */
   uint16_t workgroup_size[3];

   /* Exact AGX3 direct-launch ShaderInfo producers. Resource-info queries
    * come from NIR. Every backend lowering which emits a ray traversal/query
    * opcode must set uses_ray_traversal as well; ray_queries alone is not a
    * future-proof proxy for full RT pipelines. ShaderInfo ID32 is independent
    * from traversal use and must have its own committed-path producer. */
   bool uses_resource_info_query;
   bool uses_ray_traversal;
   bool ray_traversal_depth_nonzero;

   /* There may be constants in the binary. The driver must map these to uniform
    * registers as specified hre.
    */
   struct bridge_agx_rodata rodata;

   struct agx2_stats stats;

   /* Main of this part begins with G17P FLTNORM carrying f68. The linker
    * clears it if another part precedes this one in the executable. */
   bool agx3_fltnorm_start;
   bool entry_emitted, straight_line;
   uint32_t entry_offset;
};


struct neo_compile_request {
   uint32_t chip, encoding, needs_g13x_coherency, reserved_preamble;
   bool soft_fault, has_scratch, promote_constants, promote_textures;
   bool no_preamble, no_stop, ignore_tib_dependencies, inside_sample_loop;
   uint8_t cf_base;
};
struct neo_compile_result { struct bridge_agx_shader_info info; void *binary; };
bool agx_neo_compile_bridge(nir_shader *, const struct neo_compile_request *, struct neo_compile_result *);
