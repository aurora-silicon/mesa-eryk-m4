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

static unsigned
runtime_vectors(void)
{
   unsigned fail = 0;
   {
      const uint8_t expected[] = {0x09, 0x01, 0x1e, 0x03, 0x91, 0x04, 0x02, 0xc0};
      const struct agx3_falu_mods m = {.src0_abs = 0, .src0_neg = 0, .src1_abs = 0, .src1_neg = 0, .addend_abs = 0, .addend_neg = 1, .saturate = 0};
      const struct agx3_falu_runtime rt = {.cache = {0, 0, 0}, .discard = {1, 1, 1}, .dest_cache = 0, .wait_mask = 32};
      uint8_t out[16], len;
      if (!agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 0, 4,
                                      &m, &rt, out, &len) ||
          len != sizeof(expected) || memcmp(out, expected, len)) {
         fprintf(stderr, "native modified FMA vector 0 failed\n");
         fail++;
      }
   }
   {
      const uint8_t expected[] = {0x09, 0x03, 0x06, 0x05, 0x11, 0x06, 0x02, 0xc0};
      const struct agx3_falu_mods m = {.src0_abs = 0, .src0_neg = 0, .src1_abs = 0, .src1_neg = 0, .addend_abs = 0, .addend_neg = 1, .saturate = 0};
      const struct agx3_falu_runtime rt = {.cache = {0, 0, 0}, .discard = {0, 0, 0}, .dest_cache = 0, .wait_mask = 32};
      uint8_t out[16], len;
      if (!agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 4, 2, 6,
                                      &m, &rt, out, &len) ||
          len != sizeof(expected) || memcmp(out, expected, len)) {
         fprintf(stderr, "native modified FMA vector 1 failed\n");
         fail++;
      }
   }
   {
      const uint8_t expected[] = {0x09, 0x01, 0x1e, 0x03, 0x83, 0x04, 0x02, 0x00, 0x00, 0x80, 0x01, 0x00};
      const struct agx3_falu_mods m = {.src0_abs = 0, .src0_neg = 0, .src1_abs = 1, .src1_neg = 0, .addend_abs = 0, .addend_neg = 0, .saturate = 0};
      const struct agx3_falu_runtime rt = {.cache = {0, 0, 0}, .discard = {1, 1, 1}, .dest_cache = 0, .wait_mask = 32};
      uint8_t out[16], len;
      if (!agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 0, 4,
                                      &m, &rt, out, &len) ||
          len != sizeof(expected) || memcmp(out, expected, len)) {
         fprintf(stderr, "native modified FMA vector 2 failed\n");
         fail++;
      }
   }
   {
      const uint8_t expected[] = {0x09, 0x03, 0x1e, 0x01, 0x81, 0x04, 0x02, 0xc8};
      const struct agx3_falu_mods m = {.src0_abs = 0, .src0_neg = 1, .src1_abs = 0, .src1_neg = 0, .addend_abs = 0, .addend_neg = 0, .saturate = 0};
      const struct agx3_falu_runtime rt = {.cache = {0, 0, 0}, .discard = {1, 1, 1}, .dest_cache = 0, .wait_mask = 32};
      uint8_t out[16], len;
      if (!agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 0, 2, 4,
                                      &m, &rt, out, &len) ||
          len != sizeof(expected) || memcmp(out, expected, len)) {
         fprintf(stderr, "native modified FMA vector 3 failed\n");
         fail++;
      }
   }
   {
      const uint8_t expected[] = {0x09, 0x01, 0x1e, 0x03, 0x89, 0x04, 0x02, 0xc0};
      const struct agx3_falu_mods m = {.src0_abs = 0, .src0_neg = 0, .src1_abs = 0, .src1_neg = 0, .addend_abs = 1, .addend_neg = 0, .saturate = 0};
      const struct agx3_falu_runtime rt = {.cache = {0, 0, 0}, .discard = {1, 1, 1}, .dest_cache = 0, .wait_mask = 32};
      uint8_t out[16], len;
      if (!agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 0, 4,
                                      &m, &rt, out, &len) ||
          len != sizeof(expected) || memcmp(out, expected, len)) {
         fprintf(stderr, "native modified FMA vector 4 failed\n");
         fail++;
      }
   }
   {
      const uint8_t expected[] = {0x09, 0x01, 0x1e, 0x03, 0x82, 0x04, 0x02, 0x00, 0x00, 0x82};
      const struct agx3_falu_mods m = {.src0_abs = 0, .src0_neg = 0, .src1_abs = 0, .src1_neg = 0, .addend_abs = 0, .addend_neg = 0, .saturate = 1};
      const struct agx3_falu_runtime rt = {.cache = {0, 0, 0}, .discard = {1, 1, 1}, .dest_cache = 0, .wait_mask = 32};
      uint8_t out[16], len;
      if (!agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 0, 4,
                                      &m, &rt, out, &len) ||
          len != sizeof(expected) || memcmp(out, expected, len)) {
         fprintf(stderr, "native modified FMA vector 5 failed\n");
         fail++;
      }
   }
   /* The witnessed abs source forces the long form. Independently inspect
    * all six physical wait bits and source hints, not the encoder's recipe. */
   for (unsigned waits = 0; waits < 64; ++waits) {
      const struct agx3_falu_mods m = {.src1_abs = true};
      struct agx3_falu_runtime rt = {.wait_mask = waits};
      uint8_t out[16], len;
      const unsigned wait_bits[] = {61, 62, 63, 77, 78, 79};
      if (!agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 0, 4,
                                    &m, &rt, out, &len) || len != 12) {
         fail++;
         continue;
      }
      for (unsigned i = 0; i < 6; ++i)
         fail += ((out[wait_bits[i] / 8] >> (wait_bits[i] % 8)) & 1) !=
                 ((waits >> i) & 1);
      const unsigned cache_bits[] = {31, 15, 47};
      const unsigned discard_bits[] = {20, 19, 39};
      for (unsigned hints = 0; hints < 27; ++hints) {
         unsigned v = hints;
         for (unsigned i = 0; i < 3; ++i, v /= 3) {
            rt.cache[i] = (v % 3) == 1;
            rt.discard[i] = (v % 3) == 2;
         }
         if (!agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 0, 4,
                                       &m, &rt, out, &len) || len != 12) {
            fail++;
            continue;
         }
         for (unsigned i = 0; i < 3; ++i) {
            fail += ((out[cache_bits[i] / 8] >> (cache_bits[i] % 8)) & 1)
                    != rt.cache[i];
            fail += ((out[discard_bits[i] / 8] >> (discard_bits[i] % 8)) & 1)
                    != rt.discard[i];
         }
      }
   }
   struct agx3_falu_mods m = {.addend_neg = true};
   struct agx3_falu_runtime rt = {.wait_mask = 64};
   uint8_t out[16], len = 0xa5;
   memset(out, 0xa5, sizeof(out));
   fail += agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 4, 6,
                                    &m, &rt, out, &len);
   rt.wait_mask = 0;
   rt.cache[0] = rt.discard[0] = true;
   fail += agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 4, 6,
                                    &m, &rt, out, &len);
   rt.cache[0] = false;
   fail += agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3B, 0, 2, 4, 255,
                                    &m, &rt, out, &len);
   fail += agx3_falu_fma_mod_runtime(AGX_ISA_ENCODING_AGX3C, 0, 2, 4, 6,
                                    &m, &rt, out, &len);
   fail += len != 0xa5;
   for (unsigned i = 0; i < sizeof(out); ++i)
      fail += out[i] != 0xa5;
   printf("native modified FMA: six Apple byte vectors, 64 wait masks, "
          "27 independent lifetime combinations: %u failures\n", fail);
   return fail;
}

int
main(void)
{
   unsigned pass = 0, fail = runtime_vectors();
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
