/* SPDX-License-Identifier: MIT */
#include "agx_compile.h"
#include "agx_nir.h"
#include "nir_builder.h"
#include <stdio.h>

int main(void)
{
   uint32_t random = 0x12345678;
   for (unsigned i = 0; i < 1088; ++i) {
      random = random * 1664525u + 1013904223u;
      uint16_t x = i < 32 ? (i < 16 ? 1u << i : 0) : random;
      uint16_t y = i < 32 ? (i >= 16 ? 1u << (i - 16) : 0) : random >> 16;
      if (i == 32) x = y = 0xffff;
      uint32_t expected = 0;
      for (unsigned bit = 0; bit < 16; ++bit) {
         expected |= ((x >> bit) & 1u) << (2 * bit);
         expected |= ((y >> bit) & 1u) << (2 * bit + 1);
      }
      nir_builder b = nir_builder_init_simple_shader(
         MESA_SHADER_COMPUTE, &agx_nir_options, "interleave oracle");
      nir_def *v = nir_interleave(&b, nir_imm_intN_t(&b, x, 16),
                                      nir_imm_intN_t(&b, y, 16));
      nir_intrinsic_instr *store = nir_store_global(&b, v,
         nir_imm_int64(&b, 0x2000000000), .align_mul = 4);
      assert(agx_nir_lower_interleave(b.shader));
      while (nir_opt_constant_folding(b.shader)) {}
      assert(nir_src_is_const(store->src[0]));
      assert(nir_src_as_uint(store->src[0]) == expected);
      ralloc_free(b.shader);
   }
   puts("AGX3 interleave: 1088 lowered-NIR comparisons PASS");
   return 0;
}
