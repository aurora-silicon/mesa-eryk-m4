/* SPDX-License-Identifier: MIT */
#include "agx_compile.h"
#include "agx_abi.h"
#include "agx_linker.h"
#include "nir_builder.h"

#include <stdio.h>

/* Explicit descriptor loads and retained texture-query handles must refer to
 * the same uniform after reserving the native program's prefix. Firefox's
 * vertex blur shader combines both forms for descriptor set 2.
 */
static bool
check(mesa_shader_stage stage)
{
   nir_builder b = nir_builder_init_simple_shader(
      stage, &agx_nir_options, "native descriptor uniform relocation");
   nir_def *offset = nir_imm_int(&b, 72);
   nir_def *pointer = nir_load_preamble(&b, 1, 64, .base = 60);
   nir_def *image = nir_bindless_image_agx(&b, offset, .desc_set = 60);
   nir_def *sampler = nir_bindless_sampler_agx(&b, offset, nir_imm_int(&b, 3), .desc_set = 60);
   nir_def *other = nir_load_preamble(&b, 1, 32, .base = 12);

   if (stage != MESA_SHADER_FRAGMENT)
      agx_nir_reserve_native_vertex_prefix(b.shader);
   else
      agx_nir_reserve_native_fragment_prefix(b.shader);

   unsigned prefix = stage != MESA_SHADER_FRAGMENT ? AGX_ABI_VUNI_NATIVE_PREFIX
                                                : AGX_ABI_FUNI_NATIVE_PREFIX;
   bool ok = nir_intrinsic_base(nir_def_as_intrinsic(pointer)) ==
                60 + prefix &&
             nir_intrinsic_desc_set(nir_def_as_intrinsic(image)) ==
                60 + prefix &&
             nir_intrinsic_desc_set(nir_def_as_intrinsic(sampler)) ==
                60 + prefix &&
             nir_intrinsic_base(nir_def_as_intrinsic(other)) ==
                12 + prefix &&
             nir_def_as_intrinsic(image)->src[0].ssa == offset &&
             nir_def_as_intrinsic(sampler)->src[0].ssa == offset;
   ralloc_free(b.shader);
   return ok;
}

int main(void)
{
   if (!check(MESA_SHADER_VERTEX) || !check(MESA_SHADER_COMPUTE) ||
       !check(MESA_SHADER_FRAGMENT)) {
      fprintf(stderr, "native descriptor uniform relocation failed\n");
      return 1;
   }
   puts("PASS: native hardware/software vertex and fragment descriptor uniform relocation");
   return 0;
}
