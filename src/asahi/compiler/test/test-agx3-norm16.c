/* SPDX-License-Identifier: MIT */
#include "agx_compile.h"
#include "agx_nir.h"
#include "nir_builder.h"
#include <math.h>
#include <stdio.h>

int main(void)
{
   unsigned checks = 0;
   for (unsigned sign = 0; sign < 2; ++sign) {
      enum pipe_format format = sign ? PIPE_FORMAT_R16_SNORM : PIPE_FORMAT_R16_UNORM;
      for (int i = -32; i <= 48; ++i) {
         float input = i / 32.f;
         float clamped = fminf(1.f, fmaxf(sign ? -1.f : 0.f, input));
         uint16_t expected = (int32_t)nearbyintf(clamped * (sign ? 32767.f : 65535.f));
         nir_builder b = nir_builder_init_simple_shader(
            MESA_SHADER_FRAGMENT, &agx_nir_options, "normalized tile store oracle");
         nir_store_local_pixel_agx(&b, nir_imm_vec4(&b, input, input, input, input),
                                   nir_imm_intN_t(&b, 5, 16), nir_undef(&b, 2, 16),
                                   .base = 20, .format = format, .write_mask = 10);
         assert(agx_nir_lower_agx3_tile_formats(b.shader));
         while (nir_opt_constant_folding(b.shader)) {}
         unsigned stores = 0;
         nir_foreach_function_impl(impl, b.shader) {
            nir_foreach_block(block, impl) {
               nir_foreach_instr(instr, block) {
                  if (instr->type != nir_instr_type_intrinsic) continue;
                  nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
                  if (intr->intrinsic != nir_intrinsic_store_local_pixel_agx) continue;
                  ++stores;
                  assert(nir_intrinsic_format(intr) == PIPE_FORMAT_R16_UINT);
                  assert(nir_intrinsic_base(intr) == 20);
                  assert(nir_intrinsic_write_mask(intr) == 10);
                  assert(nir_src_as_uint(intr->src[1]) == 5);
                  assert(intr->src[0].ssa->bit_size == 16);
                  for (unsigned c = 0; c < 4; ++c) {
                     assert(nir_src_as_const_value(intr->src[0])[c].u16 == expected);
                     checks++;
                  }
               }
            }
         }
         assert(stores == 1);
         ralloc_free(b.shader);
      }
      const uint16_t raw[] = {0, 1, 32767, 32768, 65534, 65535};
      for (unsigned i = 0; i < ARRAY_SIZE(raw); ++i) {
         nir_builder b = nir_builder_init_simple_shader(
            MESA_SHADER_FRAGMENT, &agx_nir_options, "normalized tile load oracle");
         nir_def *value = nir_load_local_pixel_agx(&b, 1, 32,
            nir_imm_intN_t(&b, 5, 16), .base = 20, .format = format);
         nir_intrinsic_instr *sink = nir_store_global(&b, value,
            nir_imm_int64(&b, 0x2000000000), .align_mul = 4);
         assert(agx_nir_lower_agx3_tile_formats(b.shader));
         unsigned loads = 0;
         nir_foreach_function_impl(impl, b.shader) {
            nir_foreach_block(block, impl) {
               nir_foreach_instr_safe(instr, block) {
                  if (instr->type != nir_instr_type_intrinsic) continue;
                  nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
                  if (intr->intrinsic != nir_intrinsic_load_local_pixel_agx) continue;
                  ++loads;
                  assert(intr->def.bit_size == 16);
                  assert(nir_intrinsic_base(intr) == 20);
                  assert(nir_intrinsic_format(intr) == PIPE_FORMAT_R16_UINT);
                  b.cursor = nir_before_instr(instr);
                  nir_def_replace(&intr->def, nir_imm_intN_t(&b, raw[i], 16));
               }
            }
         }
         assert(loads == 1);
         while (nir_opt_constant_folding(b.shader)) {}
         float expected = sign ? fmaxf(-1.f, (int16_t)raw[i] / 32767.f) : raw[i] / 65535.f;
         assert(fabsf(nir_src_as_float(sink->src[0]) - expected) < 0.0000002f);
         checks++;
         ralloc_free(b.shader);
      }
   }
   printf("AGX3 normalized16: %u lowered-NIR comparisons PASS\n", checks);
   return 0;
}
