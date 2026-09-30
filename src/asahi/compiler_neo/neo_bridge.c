/* SPDX-License-Identifier: MIT */
#include "agx_compile.h"
#include "neo_bridge.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
bool agx_neo_compile_bridge(nir_shader *nir, const struct neo_compile_request *r,
                           struct neo_compile_result *out)
{
   if (nir->info.stage != MESA_SHADER_FRAGMENT || r->chip != AGX_CHIP_G15S ||
       r->encoding != AGX_ISA_ENCODING_AGX3B || !r->no_preamble)
      return false;
   struct agx_shader_key key = {
      .dev = {.chip = r->chip, .encoding = r->encoding,
              .needs_g13x_coherency = r->needs_g13x_coherency, .soft_fault = r->soft_fault},
      .reserved_preamble = r->reserved_preamble, .has_scratch = r->has_scratch,
      .promote_constants = r->promote_constants, .promote_textures = r->promote_textures,
      .no_preamble = true, .no_stop = r->no_stop,
      .fs = {.ignore_tib_dependencies = r->ignore_tib_dependencies,
             .inside_sample_loop = r->inside_sample_loop, .cf_base = r->cf_base},
   };
   struct agx_shader_part compiled = {0};
   nir->options = &agx_nir_options;
   if (!agx_compile_shader_nir(nir, &key, &compiled))
      return false;
   /* Neo's scratch and CProg launch contracts are not wired into M3 here. */
   if (compiled.info.has_preamble || compiled.info.scratch_size ||
       compiled.info.preamble_scratch_size || compiled.info.agx3_spectpr_scratch_size) {
      fprintf(stderr, "NEO_M3_REJECT unsupported scratch/preamble launch requirements\n");
      free(compiled.binary);
      return false;
   }
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
   out->info.entry_emitted = compiled.info.agx3_colour_link.entry_emitted;
   out->info.straight_line = compiled.info.agx3_colour_link.straight_line;
   out->info.entry_offset = compiled.info.agx3_colour_link.entry_offset;
   out->binary = compiled.binary;
   fprintf(stderr, "NEO_M3_COMPILE instructions=%u regs=%u scratch=%u bytes=%u\n",
           out->info.stats.instrs, out->info.nr_gprs, out->info.scratch_size, out->info.binary_size);
   return true;
}
