/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * The f32 modifier encoders must reproduce the composer's bytes -- the same
 * recipe (baseline + operands + modifiers) the RE proved byte-exact against
 * Apple -- across the abs/neg/saturate combinations of add, mul and fma.
 */

#include "agx3_falu_mod_core.h"
#include "agx3_falu_mod_core_vectors.c"

#include <stdio.h>
#include <string.h>

int
main(void)
{
   unsigned pass = 0, fail = 0;
   for (size_t i = 0; i < mod_vec_count; ++i) {
      const struct mod_vec *v = &mod_vecs[i];
      const enum agx_isa_encoding e =
         v->enc == 0 ? AGX_ISA_ENCODING_AGX3B : AGX_ISA_ENCODING_AGX3C;
      const struct agx3_falu_mods m = {
         .src0_abs = v->s0a, .src0_neg = v->s0n, .src1_abs = v->s1a,
         .src1_neg = v->s1n, .addend_abs = v->ada, .addend_neg = v->adn,
         .saturate = v->sat,
      };
      uint8_t out[AGX3_FALU_MOD_MAX_LENGTH];
      uint8_t len = 0;
      bool ok;
      if (!strcmp(v->mnem, "f32add"))
         ok = agx3_falu_add_mod(e, v->dest, v->s0, v->s1, &m, out, &len);
      else if (!strcmp(v->mnem, "f32mul"))
         ok = agx3_falu_mul_mod(e, v->dest, v->s0, v->s1, &m, out, &len);
      else
         ok = agx3_falu_fma_mod(e, v->dest, v->s0, v->s1, v->ad, &m, out, &len);

      if (!ok || len != v->nbytes || memcmp(out, v->bytes, v->nbytes) != 0) {
         if (fail < 10)
            fprintf(stderr, "FAIL %s enc=%d\n", v->mnem, v->enc);
         fail++;
      } else {
         pass++;
      }
   }
   if (fail) {
      fprintf(stderr, "agx3_falu_mod_core: %u of %zu failed\n", fail,
              mod_vec_count);
      return 1;
   }
   printf("agx3_falu_mod_core: all %u modifier forms reproduced\n", pass);
   return 0;
}
