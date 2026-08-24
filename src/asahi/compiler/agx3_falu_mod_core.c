/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_falu_mod_core.h"

#include "agx3_binder.h"

#define KV(f, v) {(uint16_t)(f), (uint64_t)(v)}

/* Long-form baselines: the encoding point that carries every modifier, per
 * mnemonic and encoding. Decoded from Apple's witnessed instructions. */
static const struct agx3_field_kv add_b[] = {KV(9, 2),  KV(10, 2), KV(25, 2),
                                             KV(26, 2), KV(29, 1), KV(42, 2),
                                             KV(43, 2), KV(46, 1), KV(57, 1)};
static const struct agx3_field_kv add_c[] = {
   KV(9, 2),  KV(10, 2), KV(25, 2), KV(26, 2), KV(29, 1), KV(42, 2),
   KV(43, 2), KV(46, 1), KV(50, 3), KV(51, 48), KV(59, 1)};
static const struct agx3_field_kv mul_b[] = {
   KV(9, 2),  KV(10, 2), KV(25, 2), KV(26, 2), KV(29, 1), KV(42, 2),
   KV(43, 2), KV(46, 1), KV(60, 2), KV(74, 2), KV(85, 1)};
static const struct agx3_field_kv mul_c[] = {
   KV(9, 2),  KV(10, 2), KV(25, 2), KV(26, 2), KV(29, 1),  KV(42, 2), KV(43, 2),
   KV(46, 1), KV(60, 2), KV(74, 2), KV(78, 3), KV(79, 62), KV(87, 1)};
static const struct agx3_field_kv fma_b[] = {
   KV(9, 2),  KV(10, 2), KV(25, 2), KV(26, 2), KV(29, 1),  KV(42, 2), KV(43, 2),
   KV(46, 1), KV(59, 2), KV(60, 2), KV(63, 1), KV(77, 2), KV(91, 2), KV(102, 1)};
static const struct agx3_field_kv fma_c[] = {
   KV(9, 2),  KV(10, 2), KV(25, 2), KV(26, 2), KV(29, 1),  KV(42, 2),
   KV(43, 2), KV(46, 1), KV(59, 2), KV(60, 2), KV(63, 1),  KV(77, 2),
   KV(91, 2), KV(95, 3), KV(96, 55), KV(104, 1)};

static bool
b(enum agx_isa_encoding e)
{
   return e == AGX_ISA_ENCODING_AGX3B;
}

static unsigned
common_mods(uint16_t dest, uint16_t src0, uint16_t src1,
            const struct agx3_falu_mods *m, struct agx3_field_kv *ov)
{
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(2, dest);
   ov[n++] = (struct agx3_field_kv)KV(35, src0);
   ov[n++] = (struct agx3_field_kv)KV(18, src1);
   ov[n++] = (struct agx3_field_kv)KV(47, m->src0_abs ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(48, m->src0_neg ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(30, m->src1_abs ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(31, m->src1_neg ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(14, m->saturate ? 1 : 0);
   return n;
}

static bool
regs_ok(uint16_t a, uint16_t b_, uint16_t c)
{
   return (a & 1) == 0 && a < 256 && (b_ & 1) == 0 && b_ < 256 &&
          (c & 1) == 0 && c < 256;
}

bool
agx3_falu_add_mod(enum agx_isa_encoding enc, uint16_t dest, uint16_t src0,
                  uint16_t src1, const struct agx3_falu_mods *m,
                  uint8_t out[AGX3_FALU_MOD_MAX_LENGTH], uint8_t *len)
{
   if (m == NULL || !regs_ok(dest, src0, src1) || m->addend_abs ||
       m->addend_neg)
      return false;
   struct agx3_field_kv ov[8];
   unsigned n = common_mods(dest, src0, src1, m, ov);
   return agx3_bind_compose(enc, "f32add", 3, b(enc) ? add_b : add_c,
                            b(enc) ? 9 : 11, ov, n, out, len);
}

bool
agx3_falu_mul_mod(enum agx_isa_encoding enc, uint16_t dest, uint16_t src0,
                  uint16_t src1, const struct agx3_falu_mods *m,
                  uint8_t out[AGX3_FALU_MOD_MAX_LENGTH], uint8_t *len)
{
   if (m == NULL || !regs_ok(dest, src0, src1) || m->addend_abs ||
       m->addend_neg)
      return false;
   struct agx3_field_kv ov[8];
   unsigned n = common_mods(dest, src0, src1, m, ov);
   return agx3_bind_compose(enc, "f32mul", 3, b(enc) ? mul_b : mul_c,
                            b(enc) ? 11 : 13, ov, n, out, len);
}

bool
agx3_falu_fma_mod(enum agx_isa_encoding enc, uint16_t dest, uint16_t src0,
                  uint16_t src1, uint16_t addend,
                  const struct agx3_falu_mods *m,
                  uint8_t out[AGX3_FALU_MOD_MAX_LENGTH], uint8_t *len)
{
   if (m == NULL || !regs_ok(dest, src0, src1) || (addend & 1) != 0 ||
       addend >= 256)
      return false;
   struct agx3_field_kv ov[11];
   unsigned n = common_mods(dest, src0, src1, m, ov);
   ov[n++] = (struct agx3_field_kv)KV(52, addend);
   ov[n++] = (struct agx3_field_kv)KV(64, m->addend_abs ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(65, m->addend_neg ? 1 : 0);
   return agx3_bind_compose(enc, "f32fma", b(enc) ? 5 : 7, b(enc) ? fma_b : fma_c,
                            b(enc) ? 14 : 16, ov, n, out, len);
}
