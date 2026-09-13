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

/* Native half copies use the same independent destination cache, source
 * cache/discard and scoreboard fields as other copy operands. In particular,
 * a zero wait mask is not evidence that its source dies at this instruction. */
static bool
copy_register(enum agx_isa_encoding encoding, uint16_t dest_half,
                       uint16_t src_half, bool narrow, bool dest_cache, bool src_cache,
                       bool src_discard, uint16_t wait_mask,
                       uint8_t out[AGX3_COPY_MAX_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL || encoding != AGX_ISA_ENCODING_AGX3B ||
       dest_half >= 256 || src_half >= 256 || wait_mask >= (1u << 6) ||
       (src_cache && src_discard) || (!narrow && ((dest_half | src_half) & 1)))
      return false;

   const struct agx3_field_kv baseline[] = {
      KV(9, 2), KV(10, narrow ? 1 : 2), KV(23, 2), KV(24, narrow ? 1 : 2),
   };
   struct agx3_field_kv overrides[11] = {
      KV(2, dest_half), KV(16, src_half), KV(11, dest_cache),
      KV(25, src_cache), KV(27, src_discard),
   };
   unsigned n = 5;
   for (unsigned i = 0; i < 6; ++i) {
      if (wait_mask & (1u << i))
         overrides[n++] = (struct agx3_field_kv)KV(30 + i, 1);
   }
   /* The compact destination is seven bits. The long encoding preserves
    * the same operand/hint fields and addresses the full half-register file.
    */
   for (unsigned ep = 0; ep < 2; ep++) {
      if (agx3_bind_compose(encoding, "copy", ep, baseline,
                            AGX3_COPY_ARRAY_SIZE(baseline), overrides, n,
                            out, len))
         return true;
   }
   return false;
}

bool
agx3_copy_encode_reg16(enum agx_isa_encoding encoding, uint16_t dest_half,
                       uint16_t src_half, bool dest_cache, bool src_cache,
                       bool src_discard, uint16_t wait_mask,
                       uint8_t out[AGX3_COPY_MAX_LENGTH], uint8_t *len)
{
   return copy_register(encoding, dest_half, src_half, true, dest_cache,
                        src_cache, src_discard, wait_mask, out, len);
}

bool
agx3_copy_encode_reg32(enum agx_isa_encoding encoding, uint16_t dest_half,
                       uint16_t src_half, bool dest_cache, bool src_cache,
                       bool src_discard, uint16_t wait_mask,
                       uint8_t out[AGX3_COPY_MAX_LENGTH], uint8_t *len)
{
   return copy_register(encoding, dest_half, src_half, false, dest_cache,
                        src_cache, src_discard, wait_mask, out, len);
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

   /* As with register copies, high GPR destinations require the long
    * AGX3b form. Preserve both operands when compact composition refuses. */
   for (unsigned ep = 0; ep < (encoding == AGX_ISA_ENCODING_AGX3B ? 2u : 1u); ++ep) {
      if (agx3_bind_compose(encoding, "copy", ep, baseline, nbaseline,
                            overrides, AGX3_COPY_ARRAY_SIZE(overrides), out, len))
         return true;
   }
   return false;
}
