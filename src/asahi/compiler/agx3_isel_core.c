/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_isel_core.h"

#include "agx3_binder.h"

#define KV(f, v) {(uint16_t)(f), (uint64_t)(v)}

static bool
cond_ok(enum agx3_isel_icond cond)
{
   return cond == AGX3_ISEL_ICOND_ULT || cond == AGX3_ISEL_ICOND_UGT ||
          cond == AGX3_ISEL_ICOND_EQ || cond == AGX3_ISEL_ICOND_SLT ||
          cond == AGX3_ISEL_ICOND_SGT;
}

/* A 32-bit value spans two halves, so its index is 2-aligned and the pair
 * must fit the 256-half register file. A 16-bit one is a single half. */
static bool
reg32_ok(uint16_t half)
{
   return (half & 1) == 0 && half + 2 <= 256;
}

static bool
reg16_ok(uint16_t half)
{
   return half < 256;
}

/*
 * One entry per witnessed operand shape. `base` is the field vector of an
 * Apple-compiled instruction with the operands removed; `true_field` and
 * `false_field` name where this shape puts its two selected operands, which
 * differs by operand kind: an immediate goes to f64/f81, a kind-2 register to
 * f65/f82, and the 32-bit both-registers form's kind-3 registers to f66/f83.
 */
struct agx3_isel_form {
   uint8_t ep;
   uint8_t true_field;
   uint8_t false_field;
   uint8_t nbase;
   struct agx3_field_kv base[16];
};

/* 32-bit result: ep27. */
static const struct agx3_isel_form form_32_reg_reg = {
   27, 66, 83, 12,
   {KV(10, 2), KV(11, 2), KV(42, 2), KV(43, 2), KV(44, 1), KV(57, 2),
    KV(58, 2), KV(59, 1), KV(72, 3), KV(73, 2), KV(89, 3), KV(90, 2)}};
static const struct agx3_isel_form form_32_imm_imm = {
   27, 64, 81, 12,
   {KV(10, 2), KV(11, 2), KV(42, 2), KV(43, 2), KV(46, 1), KV(57, 2),
    KV(58, 2), KV(61, 1), KV(72, 1), KV(73, 2), KV(89, 1), KV(90, 2)}};
/*
 * The two 32-bit mixed shapes carry f12 = 1, taken verbatim from their
 * witnessed instructions. Its meaning is NOT established: across the probes
 * it correlates with the select's result feeding a later ALU op rather than
 * being stored directly, in both the mixed and the register/register shapes,
 * so it is not shape-specific and is not a wait (those are f98..f103). It is
 * carried unchanged because dropping it would stop reproducing Apple's bytes;
 * if a shader ever miscompiles around a mixed select, re-probe this field
 * first. Everything else in these two baselines is explained above.
 */
static const struct agx3_isel_form form_32_reg_imm = {
   27, 65, 81, 14,
   {KV(10, 2), KV(11, 2), KV(12, 1), KV(42, 2), KV(43, 2), KV(46, 1),
    KV(57, 2), KV(58, 2), KV(61, 1), KV(72, 2), KV(73, 2), KV(76, 1),
    KV(89, 1), KV(90, 2)}};
static const struct agx3_isel_form form_32_imm_reg = {
   27, 64, 82, 14,
   {KV(10, 2), KV(11, 2), KV(12, 1), KV(42, 2), KV(43, 2), KV(46, 1),
    KV(57, 2), KV(58, 2), KV(61, 1), KV(72, 1), KV(73, 2), KV(89, 2),
    KV(90, 2), KV(93, 1)}};

/* 16-bit result: ep25. */
static const struct agx3_isel_form form_16_imm_imm = {
   25, 64, 81, 12,
   {KV(10, 2), KV(11, 1), KV(42, 2), KV(43, 2), KV(46, 1), KV(57, 2),
    KV(58, 2), KV(61, 1), KV(72, 1), KV(73, 1), KV(89, 1), KV(90, 1)}};
static const struct agx3_isel_form form_16_reg_imm = {
   25, 65, 81, 13,
   {KV(10, 2), KV(11, 1), KV(42, 2), KV(43, 2), KV(46, 1), KV(57, 2),
    KV(58, 2), KV(61, 1), KV(72, 2), KV(73, 1), KV(76, 1), KV(89, 1),
    KV(90, 1)}};
static const struct agx3_isel_form form_16_imm_reg = {
   25, 64, 82, 13,
   {KV(10, 2), KV(11, 1), KV(42, 2), KV(43, 2), KV(46, 1), KV(57, 2),
    KV(58, 2), KV(61, 1), KV(72, 1), KV(73, 1), KV(89, 2), KV(90, 1),
    KV(93, 1)}};
static const struct agx3_isel_form form_16_reg_reg = {
   25, 65, 82, 14,
   {KV(10, 2), KV(11, 1), KV(42, 2), KV(43, 2), KV(46, 1), KV(57, 2),
    KV(58, 2), KV(61, 1), KV(72, 2), KV(73, 1), KV(76, 1), KV(89, 2),
    KV(90, 1), KV(93, 1)}};

/*
 * A 16-bit result in an odd half is a different encoding point, not a field:
 * Apple emits ep25 for an even half and ep26 for an odd one, with an
 * otherwise identical field layout. Witnessed by ushort2/ushort4 kernels,
 * whose components must land in adjacent halves, so the compiler had to
 * produce both. (Scalar code never does -- Apple's allocator always picks an
 * even half, which is why the first pass here saw only ep25.)
 */
static const struct agx3_isel_form *
pick_form(bool dest_16, enum agx3_isel_operand t, enum agx3_isel_operand f)
{
   const bool ti = t == AGX3_ISEL_IMMEDIATE, fi = f == AGX3_ISEL_IMMEDIATE;

   if (dest_16) {
      if (ti && fi)
         return &form_16_imm_imm;
      if (!ti && fi)
         return &form_16_reg_imm;
      if (ti && !fi)
         return &form_16_imm_reg;
      return &form_16_reg_reg;
   }

   if (ti && fi)
      return &form_32_imm_imm;
   if (!ti && fi)
      return &form_32_reg_imm;
   if (ti && !fi)
      return &form_32_imm_reg;
   return &form_32_reg_reg;
}

static bool
operand_ok(enum agx3_isel_operand kind, uint16_t value, bool narrow)
{
   if (kind == AGX3_ISEL_IMMEDIATE)
      return value <= 0xff;
   if (kind == AGX3_ISEL_REGISTER)
      return narrow ? reg16_ok(value) : reg32_ok(value);
   return false;
}

bool
agx3_isel_encode(enum agx_isa_encoding encoding,
                 const struct agx3_isel_desc *d,
                 uint8_t out[AGX3_ISEL_MAX_LENGTH], uint8_t *len)
{
   if (d == NULL || out == NULL || len == NULL)
      return false;

   /* The probes are AGX3b. AGX3c spells this `iseli32`/`iseli16` with its own
    * forms, which no probe here covers. */
   if (encoding != AGX_ISA_ENCODING_AGX3B)
      return false;

   /* The compared operands are always a pair of 32-bit registers. */
   if (!cond_ok(d->cond) || !reg32_ok(d->cmp0) || !reg32_ok(d->cmp1))
      return false;
   if (d->wait_mask >= (1u << 6))
      return false;

   if (d->dest_16 ? !reg16_ok(d->dest) : !reg32_ok(d->dest))
      return false;
   if (!operand_ok(d->true_kind, d->sel_true, d->dest_16) ||
       !operand_ok(d->false_kind, d->sel_false, d->dest_16))
      return false;

   const struct agx3_isel_form *form =
      pick_form(d->dest_16, d->true_kind, d->false_kind);
   const uint8_t ep =
      (d->dest_16 && (d->dest & 1)) ? (uint8_t)(form->ep + 1) : form->ep;

   struct agx3_field_kv ov[10];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(3, d->dest);
   ov[n++] = (struct agx3_field_kv)KV(35, d->cmp0);
   ov[n++] = (struct agx3_field_kv)KV(50, d->cmp1);
   ov[n++] = (struct agx3_field_kv)KV(106, d->cond);
   ov[n++] = (struct agx3_field_kv)KV(form->true_field, d->sel_true);
   ov[n++] = (struct agx3_field_kv)KV(form->false_field, d->sel_false);

   /* Waits ride on f98..f103 for slots 1..6. */
   for (unsigned i = 0; i < 6; ++i) {
      if (d->wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(98 + i, 1);
   }

   return agx3_bind_compose(encoding, "isel", ep, form->base, form->nbase, ov,
                            n, out, len);
}
