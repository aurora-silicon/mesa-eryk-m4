/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_compile.h"
#include "agx_linker.h"
#include "nir_builder.h"
#include "util/format/u_formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned
count_intrinsic(nir_shader *s, nir_intrinsic_op op)
{
   unsigned count = 0;
   nir_foreach_function_impl(impl, s) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_intrinsic &&
                nir_instr_as_intrinsic(instr)->intrinsic == op)
               count++;
         }
      }
   }
   return count;
}

/* Gallium may link a half-precision fragment output into a normalized
 * render target. Native tile stores need a physical normalized pack in
 * either precision, including the XR30 compositor output. */
static bool
check_normalized_precision(unsigned bits, enum pipe_format format)
{
   struct agx_fs_epilog_key key = {
      .link = {
         .size_32 = bits == 32 ? BITFIELD_BIT(0) : 0,
         .loc_written = BITFIELD_BIT(0),
      },
      .nr_samples = 1,
   };
   memset(key.remap, -1, sizeof(key.remap));
   key.remap[0] = 0;
   key.rt_formats[0] = format;
   key.blend.rt[0].colormask = 0xf;
   key.blend.rt[0].mode =
      agx_pack_blend_standard(PIPE_BLEND_ADD, PIPE_BLENDFACTOR_ONE,
                             PIPE_BLENDFACTOR_ZERO, PIPE_BLEND_ADD,
                             PIPE_BLENDFACTOR_ONE, PIPE_BLENDFACTOR_ZERO);
   nir_builder b = nir_builder_init_simple_shader(
      0, &agx_nir_options, "native normalized epilog precision");
   agx_nir_fs_epilog(&b, &key);
   agx_preprocess_nir(b.shader);
   struct agx_shader_key backend_key = {
      .dev.encoding = AGX_ISA_ENCODING_AGX3B,
      .secondary = true,
      .no_preamble = true,
   };
   struct agx_shader_part part = {0};
   const bool compiled = agx_compile_shader_nir(b.shader, &backend_key, &part);
   const bool ok = compiled && part.binary && part.info.binary_size > 0;
   if (!ok)
      fprintf(stderr, "normalized %u-bit epilog format %u failed\n", bits, format);
   free(part.binary);
   ralloc_free(b.shader);
   return ok;
}

int
main(void)
{
   const unsigned mode =
      agx_pack_blend_advanced(PIPE_ADVANCED_BLEND_MULTIPLY,
                              PIPE_BLEND_OVERLAP_CONJOINT, false, false, false);
   const struct agx_blend_advanced unpacked =
      agx_unpack_blend_advanced(mode);
   if (unpacked.op != PIPE_ADVANCED_BLEND_MULTIPLY ||
       unpacked.overlap != PIPE_BLEND_OVERLAP_CONJOINT ||
       unpacked.src_premultiplied || unpacked.dst_premultiplied ||
       unpacked.clamp_results) {
      fprintf(stderr, "advanced blend state did not round-trip\n");
      return 1;
   }

   struct agx_fs_epilog_key key = {
      .link = {
         .size_32 = BITFIELD_BIT(0),
         .loc_written = BITFIELD_BIT(0),
      },
      .nr_samples = 1,
   };
   memset(key.remap, -1, sizeof(key.remap));
   key.remap[0] = 0;
   key.rt_formats[0] = PIPE_FORMAT_R8G8B8A8_UNORM;
   key.blend.rt[0].advanced_blend = true;
   key.blend.rt[0].colormask = 0xf;
   key.blend.rt[0].mode = mode;

   nir_builder b =
      nir_builder_init_simple_shader(0, &agx_nir_options,
                                     "G17P conjoint multiply epilog");
   agx_nir_fs_epilog(&b, &key);

   const unsigned loads =
      count_intrinsic(b.shader, nir_intrinsic_load_local_pixel_agx);
   const unsigned stores =
      count_intrinsic(b.shader, nir_intrinsic_store_local_pixel_agx);
   if (loads != 1 || stores != 1 ||
       !b.shader->info.fs.uses_fbfetch_output) {
      fprintf(stderr,
              "advanced blend did not retain one tile load/store (%u/%u)\n",
              loads, stores);
      ralloc_free(b.shader);
      return 1;
   }

   agx_preprocess_nir(b.shader);
   struct agx_shader_key backend_key = {
      .dev.encoding = AGX_ISA_ENCODING_AGX3B,
      .secondary = true,
      .no_preamble = true,
   };
   struct agx_shader_part part = {0};
   const bool compiled =
      agx_compile_shader_nir(b.shader, &backend_key, &part);
   const bool ok = compiled && part.binary && part.info.binary_size > 0;
   if (!ok)
      fprintf(stderr, "G17P advanced blend epilog failed to compile\n");

   free(part.binary);
   ralloc_free(b.shader);
   bool normalized_ok = true;
   const enum pipe_format formats[] = {
      PIPE_FORMAT_R8G8B8A8_UNORM,
      PIPE_FORMAT_R8G8B8A8_SNORM,
      PIPE_FORMAT_R10G10B10A2_UNORM,
   };
   for (unsigned bits = 16; bits <= 32; bits += 16)
      for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
         normalized_ok &= check_normalized_precision(bits, formats[i]);
   return ok && normalized_ok ? 0 : 1;
}
