/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_copy_core.h"

#include "agx3_binder.h"

#define KV(f, v) {(uint16_t)(f), (uint64_t)(v)}
#define AGX3_COPY_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Baselines decoded from Apple's corpus vectors:
 *
 *   agx3b  copy.ep0  f9=2 f10=2 f23=3 f24=2, f2=dest, f17=uniform  (4 B)
 *   agx3c  copy.ep0  the same plus the encoding's f28=3 f29=35 pair (4 B)
 *
 * f9/f10 are the destination's kind (2 = register) and width (2 = 32-bit);
 * f23/f24 are source slot A's kind (3 = uniform) and width.
 */
static const struct agx3_field_kv agx3b_copy_baseline[] = {
   KV(9, 2),
   KV(10, 2),
   KV(23, 3),
   KV(24, 2),
};

static const struct agx3_field_kv agx3c_copy_baseline[] = {
   KV(9, 2),
   KV(10, 2),
   KV(23, 3),
   KV(24, 2),
   KV(28, 3),
   KV(29, 35),
};

/*
 * The 16-bit register-to-register baseline, decoded from Apple's corpus
 * (two instances: r0 <- r5 and r1 <- r6). f27 is one of the liveness hint
 * bits -- other corpus instances set f30 instead -- and is carried verbatim.
 */
bool
agx3_copy_encode_reg16(enum agx_isa_encoding encoding, uint16_t dest_half,
                       uint16_t src_half, uint16_t wait_mask,
                       uint8_t out[AGX3_COPY_MAX_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL)
      return false;

   /* Only the AGX3b form is grounded here. */
   if (encoding != AGX_ISA_ENCODING_AGX3B)
      return false;

   /* A single half either way; the destination field is the narrower one. */
   if (dest_half >= 128 || src_half >= 256 || wait_mask >= (1u << 6))
      return false;

   /*
    * Two witnessed shapes: the no-wait corpus copies carry f27, and the one
    * that waits carries f30 instead and leaves f27 clear. Follow each rather
    * than setting both, which matches neither.
    */
   const struct agx3_field_kv baseline_plain[] = {
      KV(9, 2), KV(10, 1), KV(11, 1), KV(23, 2), KV(24, 1), KV(27, 1),
   };
   const struct agx3_field_kv baseline_waited[] = {
      KV(9, 2), KV(10, 1), KV(11, 1), KV(23, 2), KV(24, 1),
   };
   const struct agx3_field_kv *baseline =
      wait_mask ? baseline_waited : baseline_plain;
   const unsigned nbaseline = wait_mask
                                 ? AGX3_COPY_ARRAY_SIZE(baseline_waited)
                                 : AGX3_COPY_ARRAY_SIZE(baseline_plain);

   struct agx3_field_kv overrides[8] = {
      KV(2, dest_half),
      KV(16, src_half),
   };
   unsigned n = 2;
   for (unsigned i = 0; i < 6; ++i) {
      if (wait_mask & (1u << i))
         overrides[n++] = (struct agx3_field_kv)KV(30 + i, 1);
   }

   return agx3_bind_compose(encoding, "copy", 0, baseline, nbaseline,
                            overrides, n, out, len);
}

bool
agx3_copy_encode_reg_to_uniform(enum agx_isa_encoding encoding,
                                uint16_t uniform_half, uint16_t src_half,
                                bool kill, uint8_t out[AGX3_COPY_MAX_LENGTH],
                                uint8_t *len)
{
   if (out == NULL || len == NULL)
      return false;

   /* Only AGX3b is witnessed. */
   if (encoding != AGX_ISA_ENCODING_AGX3B)
      return false;

   if (uniform_half >= 512 || src_half >= 256)
      return false;

   const struct agx3_field_kv baseline[] = {
      KV(9, 3), KV(10, 2), KV(23, 2), KV(24, 2),
   };
   const struct agx3_field_kv overrides[] = {
      KV(3, uniform_half),
      KV(16, src_half),
      KV(27, kill ? 1 : 0),
   };

   return agx3_bind_compose(encoding, "copy", 0, baseline,
                            AGX3_COPY_ARRAY_SIZE(baseline), overrides,
                            AGX3_COPY_ARRAY_SIZE(overrides), out, len);
}

bool
agx3_copy_encode_uniform_to_reg32(enum agx_isa_encoding encoding,
                                  uint16_t dest_half, uint16_t uniform_half,
                                  uint8_t out[AGX3_COPY_MAX_LENGTH],
                                  uint8_t *len)
{
   const struct agx3_field_kv *baseline;
   unsigned nbaseline;

   if (out == NULL || len == NULL)
      return false;

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      baseline = agx3b_copy_baseline;
      nbaseline = AGX3_COPY_ARRAY_SIZE(agx3b_copy_baseline);
   } else if (encoding == AGX_ISA_ENCODING_AGX3C) {
      baseline = agx3c_copy_baseline;
      nbaseline = AGX3_COPY_ARRAY_SIZE(agx3c_copy_baseline);
   } else {
      return false;
   }

   /* A 32-bit value occupies two halves in either file, so both indices are
    * 2-aligned; the GPR file is 256 halves and the uniform file 512. */
   if ((dest_half & 1) || dest_half + 2 > 256)
      return false;
   if ((uniform_half & 1) || uniform_half + 2 > 512)
      return false;

   const struct agx3_field_kv overrides[] = {
      KV(2, dest_half),
      KV(17, uniform_half),
   };

   return agx3_bind_compose(encoding, "copy", 0, baseline, nbaseline,
                            overrides,
                            AGX3_COPY_ARRAY_SIZE(overrides), out, len);
}
