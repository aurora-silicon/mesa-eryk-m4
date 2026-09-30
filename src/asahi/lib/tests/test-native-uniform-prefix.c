/* SPDX-License-Identifier: MIT */
#include "agx_compile.h"
#include "agx_abi.h"
#include "agx_linker.h"
#include "nir_builder.h"

#include <stdio.h>
#include <stdlib.h>

static bool
check_deferred_compile(bool conditional)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "deferred uniform compilation");
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.shared_size = 4;
   nir_def *ptr = nir_load_preamble(&b, 1, 64, .base = 4);
   nir_def *read = nir_load_global(&b, 1, 32, ptr, .align_mul = 4,
      .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER);
   nir_def *replacement = nir_load_preamble(&b, 1, 32, .base = 8);
   if (conditional) {
      nir_def *flag = nir_load_preamble(&b, 1, 32, .base = 10);
      nir_push_if(&b, nir_ine_imm(&b, flag, 0));
      nir_push_else(&b, NULL);
      nir_def *fallback = nir_load_global(&b, 1, 32, ptr, .align_mul = 4,
         .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER);
      nir_pop_if(&b, NULL);
      replacement = nir_if_phi(&b, replacement, fallback);
   }
   nir_def *value = nir_load_prefetched_agx(&b, read, replacement);
   nir_store_shared(&b, nir_fmul_imm(&b, value, 2.0), nir_imm_int(&b, 0),
                    .align_mul = 4);
   agx_preprocess_nir(b.shader);
   struct agx_shader_key key = {0};
   key.dev.encoding = AGX_ISA_ENCODING_AGX3B;
   key.no_preamble = true;
   key.reserved_preamble = 12;
   struct agx_shader_part part = {0};
   bool ok = agx_compile_shader_nir(b.shader, &key, &part);
   unsigned memory_reads = 0, deferred = 0;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_op op = nir_instr_as_intrinsic(instr)->intrinsic;
         memory_reads += op == nir_intrinsic_load_agx ||
                         op == nir_intrinsic_load_constant_agx;
         deferred += op == nir_intrinsic_load_prefetched_agx;
      }
   }
   /* A direct input removes the original global read. A runtime-qualified
    * input must still retain its fallback, rather than reading the uniform
    * unconditionally when its eligibility word is zero. */
   ok &= deferred == 0 && (conditional ? memory_reads > 0 : memory_reads == 0);
   free(part.binary);
   ralloc_free(b.shader);
   return ok;
}

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
   nir_def *prefetched = nir_load_preamble(&b, 1, 32, .base = 96);
   nir_def *deferred = nir_load_prefetched_agx(&b, other, prefetched);

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
             nir_intrinsic_base(nir_def_as_intrinsic(prefetched)) ==
                96 + prefix &&
             nir_def_as_intrinsic(deferred)->src[0].ssa == other &&
             nir_def_as_intrinsic(deferred)->src[1].ssa == prefetched &&
             nir_def_as_intrinsic(image)->src[0].ssa == offset &&
             nir_def_as_intrinsic(sampler)->src[0].ssa == offset;
   ralloc_free(b.shader);
   return ok;
}

int main(void)
{
   if (!check(MESA_SHADER_VERTEX) || !check(MESA_SHADER_COMPUTE) ||
       !check(MESA_SHADER_FRAGMENT) || !check_deferred_compile(false) ||
       !check_deferred_compile(true)) {
      fprintf(stderr, "native descriptor uniform relocation failed\n");
      return 1;
   }
   puts("PASS: native hardware/software vertex and fragment descriptor uniform relocation");
   return 0;
}
