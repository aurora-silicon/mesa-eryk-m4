/* SPDX-License-Identifier: MIT */
#include "agx_compile.h"
#include "agx_linker.h"
#include "agx_nir.h"
#include "nir_builder.h"
#include "util/format/u_formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct agx_shader_part
compile(unsigned mode)
{
   struct agx_fs_epilog_key key = {
      .link = {.size_32 = 3, .loc_written = 1},
      .nr_samples = 1,
      .preserve_zero_mask = true,
      .lower_srgb = true,
      .native_spills = true,
   };
   memset(key.remap, -1, sizeof(key.remap));
   key.remap[0] = 0;
   key.rt_formats[0] = PIPE_FORMAT_R16G16B16A16_FLOAT;
   key.blend.rt[0].colormask = 0xf;
   key.blend.rt[0].mode = mode;

   nir_builder b = nir_builder_init_simple_shader(0, &agx_nir_options, NULL);
   agx_nir_fs_epilog(&b, &key);
   agx_nir_reserve_native_fragment_prefix(b.shader);
   NIR_PASS(_, b.shader, agx_nir_lower_g17p_1x_tile_samples);
   agx_preprocess_nir(b.shader);
   struct agx_shader_key backend = {
      .dev.encoding = AGX_ISA_ENCODING_AGX3B,
      .secondary = true,
      .no_preamble = true,
   };
   struct agx_shader_part part = {0};
   if (!agx_compile_shader_nir(b.shader, &backend, &part)) {
      fprintf(stderr, "blend epilog compilation failed\n");
      exit(1);
   }
   ralloc_free(b.shader);
   return part;
}

int
main(void)
{
   const enum pipe_blendfactor factors[] = {
      PIPE_BLENDFACTOR_ZERO, PIPE_BLENDFACTOR_ONE,
      PIPE_BLENDFACTOR_SRC_COLOR, PIPE_BLENDFACTOR_INV_DST_ALPHA,
      PIPE_BLENDFACTOR_SRC1_COLOR, PIPE_BLENDFACTOR_INV_SRC1_ALPHA,
   };
   const enum pipe_blend_func funcs[] = {
      PIPE_BLEND_ADD, PIPE_BLEND_MIN, PIPE_BLEND_MAX,
   };

   for (unsigned r = 0; r < ARRAY_SIZE(funcs); ++r) {
      for (unsigned a = 0; a < ARRAY_SIZE(funcs); ++a) {
         for (unsigned f = 0; f < ARRAY_SIZE(factors); ++f) {
            struct agx_blend_standard raw = {
               .rgb_func = funcs[r],
               .alpha_func = funcs[a],
               .rgb_src_factor = factors[f],
               .rgb_dst_factor = factors[ARRAY_SIZE(factors) - 1 - f],
               .alpha_src_factor = factors[f],
               .alpha_dst_factor = factors[ARRAY_SIZE(factors) - 1 - f],
            };
            unsigned old;
            memcpy(&old, &raw, sizeof(old));
            unsigned canonical = agx_pack_blend_standard(
               funcs[r], raw.rgb_src_factor, raw.rgb_dst_factor,
               funcs[a], raw.alpha_src_factor, raw.alpha_dst_factor);
            struct agx_blend_standard unpacked =
               agx_unpack_blend_standard(canonical);
            if ((r == 0 && unpacked.rgb_src_factor != factors[f]) ||
                (a == 0 && unpacked.alpha_src_factor != factors[f]) ||
                (r != 0 && unpacked.rgb_src_factor != PIPE_BLENDFACTOR_ONE) ||
                (a != 0 && unpacked.alpha_src_factor != PIPE_BLENDFACTOR_ONE) ||
                (r == 0 && unpacked.rgb_dst_factor != raw.rgb_dst_factor) ||
                (a == 0 && unpacked.alpha_dst_factor != raw.alpha_dst_factor) ||
                (r != 0 && unpacked.rgb_dst_factor != PIPE_BLENDFACTOR_ONE) ||
                (a != 0 && unpacked.alpha_dst_factor != PIPE_BLENDFACTOR_ONE)) {
               fprintf(stderr, "blend key canonicalization changed a live factor\n");
               return 1;
            }
            struct agx_shader_part before = compile(old);
            struct agx_shader_part after = compile(canonical);
            bool equal = before.info.binary_size == after.info.binary_size &&
               memcmp(before.binary, after.binary, before.info.binary_size) == 0;
            free(before.binary);
            free(after.binary);
            if (!equal) {
               fprintf(stderr, "MIN/MAX key changed shader binary: %u/%u/%u\n",
                       r, a, f);
               return 1;
            }
         }
      }
   }
   return 0;
}
