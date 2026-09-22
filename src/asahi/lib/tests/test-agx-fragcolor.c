/* SPDX-License-Identifier: MIT */
#include "agx_compile.h"
#include "agx_linker.h"
#include "nir_builder.h"
#include <stdio.h>

enum variation { IDENTICAL, DIFFERENT, PARTIAL, MISSING, DUAL, COVERAGE, SPLIT };

static bool
check(enum variation variation)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "colour broadcast");
   nir_def *colour = nir_load_preamble(&b, 4, 32, .base = 0);
   nir_def *other = nir_load_preamble(&b, 4, 32, .base = 8);
   nir_def *zero = nir_imm_int(&b, 0);
   nir_def *mask = nir_imm_intN_t(&b, 1, 16);
   nir_def *condition = nir_ine_imm(&b, nir_channel(&b, colour, 0), 0);
   nir_if *nif = NULL;
   for (unsigned rt = 0; rt < (variation == MISSING ? 7 : 8); ++rt) {
      if (rt == 4 && variation == COVERAGE)
         nir_demote_samples(&b, mask);
      if (rt == 4 && variation == SPLIT)
         nif = nir_push_if(&b, condition);
      nir_store_output(&b, rt == 4 && variation == DIFFERENT ? other : colour,
                       zero, .write_mask = rt == 4 && variation == PARTIAL ? 7 : 15,
                       .io_semantics = {
                          .location = FRAG_RESULT_DATA0 + rt,
                          .dual_source_blend_index = variation == DUAL && rt == 0,
                       }, .src_type = nir_type_float32);
      b.shader->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DATA0 + rt);
   }
   if (nif)
      nir_pop_if(&b, nif);
   const bool changed = agx_nir_coalesce_fragcolor(b.shader);
   bool ok = changed == (variation == IDENTICAL);
   if (changed) {
      struct agx_fs_epilog_link_info link = {0};
      agx_nir_lower_fs_output_to_epilog_g17p(b.shader, &link);
      ok &= link.broadcast_rt0 && link.loc_written == 1 && link.size_32 == 1;
   }
   nir_validate_shader(b.shader, "after colour broadcast");
   ralloc_free(b.shader);
   if (!ok)
      fprintf(stderr, "colour broadcast variation %u failed\n", variation);
   return ok;
}

int
main(void)
{
   bool ok = true;
   for (unsigned v = IDENTICAL; v <= SPLIT; ++v)
      ok &= check(v);
   return ok ? 0 : 1;
}
