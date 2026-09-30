/* SPDX-License-Identifier: MIT */
#include "hk_shader.h"
#include "../compiler_neo/neo_bridge.h"
#include <string.h>
bool hk_compile_neo_fragment(nir_shader *nir, const struct agx_shader_key *key,
                             struct agx_shader_part *out)
{
   struct neo_compile_request r = {
      .chip = AGX_CHIP_G15S, .encoding = key->dev.encoding,
      .needs_g13x_coherency = key->dev.needs_g13x_coherency,
      .soft_fault = key->dev.soft_fault, .reserved_preamble = key->reserved_preamble,
      .has_scratch = key->has_scratch, .promote_constants = key->promote_constants,
      .promote_textures = key->promote_textures, .no_preamble = key->no_preamble,
      .no_stop = key->no_stop, .ignore_tib_dependencies = key->fs.ignore_tib_dependencies,
      .inside_sample_loop = key->fs.inside_sample_loop, .cf_base = key->fs.cf_base,
   };
   struct neo_compile_result compiled = {0};
   if (!agx_neo_compile_bridge(nir, &r, &compiled))
      return false;
   memset(out, 0, sizeof(*out));
   out->info.stage = compiled.info.stage;
   out->info.binary_size = compiled.info.binary_size;
   out->info.push_count = compiled.info.push_count;
   out->info.local_size = compiled.info.local_size;
   out->info.imageblock_stride = compiled.info.imageblock_stride;
   out->info.scratch_size = compiled.info.scratch_size;
   out->info.preamble_scratch_size = compiled.info.preamble_scratch_size;
   out->info.main_size = compiled.info.main_size;
   out->info.has_preamble = compiled.info.has_preamble;
   out->info.preamble_offset = compiled.info.preamble_offset;
   out->info.main_offset = compiled.info.main_offset;
   out->info.reads_tib = compiled.info.reads_tib;
   out->info.early_fragment_tests = compiled.info.early_fragment_tests;
   out->info.nonzero_viewport = compiled.info.nonzero_viewport;
   out->info.writes_layer_viewport = compiled.info.writes_layer_viewport;
   out->info.writes_sample_mask = compiled.info.writes_sample_mask;
   out->info.depth_layout = compiled.info.depth_layout;
   out->info.tag_write_disable = compiled.info.tag_write_disable;
   out->info.disable_tri_merging = compiled.info.disable_tri_merging;
   out->info.uses_draw_id = compiled.info.uses_draw_id;
   out->info.uses_base_param = compiled.info.uses_base_param;
   out->info.uses_txf = compiled.info.uses_txf;
   out->info.uses_sampler_heap = compiled.info.uses_sampler_heap;
   out->info.texture_state_count = compiled.info.texture_state_count;
   out->info.sampler_state_count = compiled.info.sampler_state_count;
   out->info.nr_gprs = compiled.info.nr_gprs;
   out->info.nr_preamble_gprs = compiled.info.nr_preamble_gprs;
   out->info.agx3_staging_halfs = compiled.info.agx3_staging_halfs;
   out->info.agx3_staging_known = compiled.info.agx3_staging_known;
   out->info.outputs = compiled.info.outputs;
   out->info.uses_resource_info_query = compiled.info.uses_resource_info_query;
   out->info.uses_ray_traversal = compiled.info.uses_ray_traversal;
   out->info.ray_traversal_depth_nonzero = compiled.info.ray_traversal_depth_nonzero;
   out->info.agx3_fltnorm_start = compiled.info.agx3_fltnorm_start;
   static_assert(sizeof(out->info.varyings) == sizeof(compiled.info.varyings), "varyings bridge layout");
   memcpy(&out->info.varyings, &compiled.info.varyings, sizeof(out->info.varyings));
   static_assert(sizeof(out->info.workgroup_size) == sizeof(compiled.info.workgroup_size), "workgroup_size bridge layout");
   memcpy(&out->info.workgroup_size, &compiled.info.workgroup_size, sizeof(out->info.workgroup_size));
   static_assert(sizeof(out->info.rodata) == sizeof(compiled.info.rodata), "rodata bridge layout");
   memcpy(&out->info.rodata, &compiled.info.rodata, sizeof(out->info.rodata));
   static_assert(sizeof(out->info.stats) == sizeof(compiled.info.stats), "stats bridge layout");
   memcpy(&out->info.stats, &compiled.info.stats, sizeof(out->info.stats));
   out->info.agx3_colour_link.entry_emitted = compiled.info.entry_emitted;
   out->info.agx3_colour_link.straight_line = compiled.info.straight_line;
   out->info.agx3_colour_link.entry_offset = compiled.info.entry_offset;
   out->binary = compiled.binary;
   return true;
}
