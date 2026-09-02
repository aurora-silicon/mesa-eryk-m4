/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_fltnorm_core.h"

#include <string.h>

#include "agx3_binder.h"

#define KV(f, v) {(uint16_t)(f), (uint64_t)(v)}

static bool
b_only(enum agx_isa_encoding enc)
{
   return enc == AGX_ISA_ENCODING_AGX3B;
}

static bool
enc_ok(enum agx_isa_encoding enc)
{
   return enc == AGX_ISA_ENCODING_AGX3B || enc == AGX_ISA_ENCODING_AGX3C;
}

static unsigned
nslots(enum agx_isa_encoding enc)
{
   return enc == AGX_ISA_ENCODING_AGX3C ? 8 : 6;
}

static uint16_t
wait_limit(enum agx_isa_encoding enc)
{
   return (uint16_t)((1u << nslots(enc)) - 1);
}

/* Per-source field triple: the class selector, the value field for that class,
 * and the two liveness bits a register source picks between. */
struct src_fields {
   uint8_t f_kind, f_imm, f_reg, f_uniform, f_width, f_live, f_kill;
};

static bool
append_src(struct agx3_field_kv *ov, unsigned *n,
           const struct agx3_fltnorm_src *s, const struct src_fields *f)
{
   uint8_t value_field;
   switch (s->kind) {
   case AGX3_FLTNORM_SRC_IMMEDIATE:
      /* AGX minifloats are 8 bits. */
      if (s->value > 0xff)
         return false;
      value_field = f->f_imm;
      break;
   case AGX3_FLTNORM_SRC_REGISTER:
      if ((s->value & 1) || s->value >= 256)
         return false;
      value_field = f->f_reg;
      break;
   case AGX3_FLTNORM_SRC_UNIFORM:
      if (s->value >= 512)
         return false;
      value_field = f->f_uniform;
      break;
   default:
      return false;
   }

   ov[(*n)++] = (struct agx3_field_kv)KV(f->f_kind, s->kind);
   ov[(*n)++] = (struct agx3_field_kv)KV(f->f_width, 2); /* 32-bit */
   ov[(*n)++] = (struct agx3_field_kv)KV(value_field, s->value);

   /* Only a register source carries liveness; the composer refuses the bits
    * on the immediate and uniform alternatives. */
   if (s->kind == AGX3_FLTNORM_SRC_REGISTER) {
      ov[(*n)++] =
         (struct agx3_field_kv)KV(s->kill ? f->f_kill : f->f_live, 1);
   }
   return true;
}

bool
agx3_fltnorm_encode(enum agx_isa_encoding enc,
                    const struct agx3_fltnorm_desc *d,
                    uint8_t out[AGX3_FLTNORM_MAX_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL || d == NULL)
      return false;
   memset(out, 0, AGX3_FLTNORM_MAX_LENGTH);
   *len = 0;

   if (!enc_ok(enc) || d->dest >= 256)
      return false;
   if ((d->wait_mask & ~wait_limit(enc)) != 0)
      return false;
   if (d->src[0].kind == AGX3_FLTNORM_SRC_NONE)
      return false;
   if (d->two_lanes != (d->src[1].kind != AGX3_FLTNORM_SRC_NONE))
      return false;

   static const struct src_fields s0 = {
      .f_kind = 23, .f_imm = 15, .f_reg = 16, .f_uniform = 17,
      .f_width = 24, .f_live = 25, .f_kill = 27,
   };
   static const struct src_fields s1 = {
      .f_kind = 40, .f_imm = 32, .f_reg = 33, .f_uniform = 34,
      .f_width = 41, .f_live = 42, .f_kill = 44,
   };

   /* AGX3c carries the same operand map plus this form's sub-opcode pair. */
   struct agx3_field_kv base[4];
   unsigned nbase = 0;
   base[nbase++] = (struct agx3_field_kv)KV(9, 2);
   base[nbase++] = (struct agx3_field_kv)KV(10, 1);
   if (enc == AGX_ISA_ENCODING_AGX3C) {
      base[nbase++] = (struct agx3_field_kv)KV(48, 3);
      base[nbase++] = (struct agx3_field_kv)KV(49, 72);
   }

   struct agx3_field_kv ov[20];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(2, d->dest);
   ov[n++] = (struct agx3_field_kv)KV(11, d->dest_hint ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(58, d->snorm ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(59, d->two_lanes ? 1 : 0);

   if (!append_src(ov, &n, &d->src[0], &s0))
      return false;

   if (d->two_lanes) {
      if (!append_src(ov, &n, &d->src[1], &s1))
         return false;
   } else {
      /* The one-lane alternative: no second source, and the second operand's
       * width selector reads 1 rather than 2 (probe fn_r). */
      ov[n++] = (struct agx3_field_kv)KV(40, 0);
      ov[n++] = (struct agx3_field_kv)KV(41, 1);
   }

   for (unsigned i = 0; i < nslots(enc); ++i)
      if (d->wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(50 + i, 1);

   if (agx3_bind_compose(enc, "fltnorm", 0, base, nbase, ov, n, out, len))
      return true;

   memset(out, 0, AGX3_FLTNORM_MAX_LENGTH);
   *len = 0;
   return false;
}

bool
agx3_persp_mul_encode(enum agx_isa_encoding enc,
                      const struct agx3_persp_mul_desc *d,
                      uint8_t out[AGX3_FLTNORM_MAX_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL || d == NULL)
      return false;
   memset(out, 0, AGX3_FLTNORM_MAX_LENGTH);
   *len = 0;

   if (!b_only(enc))
      return false;
   if ((d->dest & 1) || d->dest >= 256 || (d->reciprocal & 1) ||
       d->reciprocal >= 256 || (d->raw & 1) || d->raw >= 256)
      return false;
   if ((d->wait_mask & ~(uint16_t)0x3f) != 0)
      return false;
   /* f70 is seven bits wide. */
   if (d->vary_index > 0x7f)
      return false;
   if (d->stage_dest ? (d->slot < 1 || d->slot > 6) : (d->slot != 0))
      return false;

   /* The form's root predicates -- the composer refuses ep4 outright without
    * them -- plus the two operand classes. */
   const struct agx3_field_kv base[] = {
      KV(10, 2), /* 32-bit destination */
      KV(25, 2), KV(26, 2), /* the interpolated operand: register, 32-bit */
      KV(42, 2), KV(43, 2), /* the reciprocal operand: register, 32-bit */
      KV(59, 6), KV(60, 2), KV(73, 6), KV(74, 2), KV(88, 1),
   };

   struct agx3_field_kv ov[16];
   unsigned n = 0;
   if (d->stage_dest) {
      ov[n++] = (struct agx3_field_kv)KV(9, 5);
      ov[n++] = (struct agx3_field_kv)KV(5, d->dest);
      ov[n++] = (struct agx3_field_kv)KV(12, d->slot);
   } else {
      ov[n++] = (struct agx3_field_kv)KV(9, 2);
      ov[n++] = (struct agx3_field_kv)KV(2, d->dest);
      ov[n++] = (struct agx3_field_kv)KV(11, d->dest_hint ? 1 : 0);
   }
   ov[n++] = (struct agx3_field_kv)KV(18, d->raw);
   if (d->raw_liveness != AGX3_OPERAND_UNMARKED) {
      ov[n++] = (struct agx3_field_kv)KV(
         d->raw_liveness == AGX3_OPERAND_KILL ? 29 : 27, 1);
   }
   ov[n++] = (struct agx3_field_kv)KV(35, d->reciprocal);
   if (d->reciprocal_liveness != AGX3_OPERAND_UNMARKED) {
      ov[n++] = (struct agx3_field_kv)KV(
         d->reciprocal_liveness == AGX3_OPERAND_KILL ? 46 : 44, 1);
   }
   ov[n++] = (struct agx3_field_kv)KV(70, d->vary_index);

   for (unsigned i = 0; i < 6; ++i)
      if (d->wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(80 + i, 1);

   if (agx3_bind_compose(enc, "f32mul", 4, base, 10, ov, n, out, len))
      return true;

   memset(out, 0, AGX3_FLTNORM_MAX_LENGTH);
   *len = 0;
   return false;
}

bool
agx3_normflt_encode(enum agx_isa_encoding enc,
                    const struct agx3_normflt_desc *d,
                    uint8_t out[AGX3_FLTNORM_MAX_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL || d == NULL)
      return false;
   memset(out, 0, AGX3_FLTNORM_MAX_LENGTH);
   *len = 0;

   if (!enc_ok(enc) || (d->dest & 1) || d->dest >= 256 || d->src >= 256)
      return false;
   if ((d->wait_mask & ~wait_limit(enc)) != 0)
      return false;

   /* f25 = 2 register source, f26 = 1 its 16-bit width, f29 = 1 in every
    * witness. AGX3c adds this form's sub-opcode pair. */
   struct agx3_field_kv base[7];
   unsigned nbase = 0;
   base[nbase++] = (struct agx3_field_kv)KV(9, 2);
   base[nbase++] = (struct agx3_field_kv)KV(10, 2);
   base[nbase++] = (struct agx3_field_kv)KV(25, 2);
   base[nbase++] = (struct agx3_field_kv)KV(26, 1);
   base[nbase++] = (struct agx3_field_kv)KV(29, 1);
   if (enc == AGX_ISA_ENCODING_AGX3C) {
      base[nbase++] = (struct agx3_field_kv)KV(30, 3);
      base[nbase++] = (struct agx3_field_kv)KV(31, 115);
   }

   struct agx3_field_kv ov[14];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(2, d->dest);
   ov[n++] = (struct agx3_field_kv)KV(11, d->dest_hint ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(18, d->src);
   ov[n++] = (struct agx3_field_kv)KV(41, d->two_lanes ? 1 : 0);

   for (unsigned i = 0; i < nslots(enc); ++i)
      if (d->wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(32 + i, 1);

   if (agx3_bind_compose(enc, "normflt", 0, base, nbase, ov, n, out, len))
      return true;

   memset(out, 0, AGX3_FLTNORM_MAX_LENGTH);
   *len = 0;
   return false;
}
