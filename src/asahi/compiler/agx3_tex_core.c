/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_tex_core.h"

#include "agx3_binder.h"

#include <string.h>

#define KV(f, v) {(uint16_t)(f), (uint64_t)(v)}

static bool
one_or_zero(uint16_t value)
{
   return (value & (value - 1)) == 0;
}

static bool
fetch_ok(enum agx_isa_encoding enc, const struct agx3_tex_fetch_desc *d)
{
   if (d == NULL || (enc != AGX_ISA_ENCODING_AGX3B &&
                     enc != AGX_ISA_ENCODING_AGX3C))
      return false;

   if ((d->width != AGX3_TEX_W16 && d->width != AGX3_TEX_W32) ||
       d->dest >= 128 || (d->width == AGX3_TEX_W32 && (d->dest & 1)) ||
       (d->coord & 3) || d->coord > 60 ||
       (enc == AGX_ISA_ENCODING_AGX3B ? (!d->mask || d->mask > 15)
                                    : (d->mask != 1 && d->mask != 15)) ||
       d->texture > 15 ||
       (d->register_lod && (enc != AGX_ISA_ENCODING_AGX3B || d->array || d->multisample)) ||
       (d->multisample && enc != AGX_ISA_ENCODING_AGX3B) ||
       (d->three_d && (enc != AGX_ISA_ENCODING_AGX3B || d->array ||
                       d->multisample || d->register_lod)))
      return false;

   const unsigned tk = d->texture_kind ?: AGX3_TEX_OPERAND_IMMEDIATE;
   const unsigned sk = d->sampler_kind ?: AGX3_TEX_OPERAND_IMMEDIATE;
   if (tk > AGX3_TEX_OPERAND_UNIFORM || sk > AGX3_TEX_OPERAND_UNIFORM ||
       (tk != AGX3_TEX_OPERAND_IMMEDIATE &&
        ((d->texture_value & 1) || d->texture_value > 252)) ||
       (sk == AGX3_TEX_OPERAND_IMMEDIATE ? d->sampler_value > 15
         : ((d->sampler_value & 1) || d->sampler_value > 252)) ||
       (d->texture_kill && tk != AGX3_TEX_OPERAND_REGISTER))
      return false;

   const unsigned slots = enc == AGX_ISA_ENCODING_AGX3C ? 8 : 6;
   const uint16_t waits = enc == AGX_ISA_ENCODING_AGX3C ? 0x7f : 0x3f;
   return d->slot >= 1 && d->slot <= slots &&
          (d->wait_mask & ~waits) == 0 && one_or_zero(d->wait_mask);
}

bool
agx3_tex_encode_fetch(enum agx_isa_encoding enc,
                      const struct agx3_tex_fetch_desc *d,
                      uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL)
      return false;
   memset(out, 0, AGX3_TEX_MAX_LENGTH);
   *len = 0;
   if (!fetch_ok(enc, d))
      return false;

   struct agx3_field_kv base[20];
   unsigned nb = 0;
   base[nb++] = (struct agx3_field_kv)KV(9, 2);
   base[nb++] = (struct agx3_field_kv)KV(23, 5);
   base[nb++] = (struct agx3_field_kv)KV(38, 1);
   base[nb++] = (struct agx3_field_kv)KV(52, 1);
   base[nb++] = (struct agx3_field_kv)KV(89, 3);
   base[nb++] = (struct agx3_field_kv)KV(90, 3);
   const enum agx3_tex_operand_kind tk =
      d->texture_kind ? d->texture_kind : AGX3_TEX_OPERAND_IMMEDIATE;
   const enum agx3_tex_operand_kind sk =
      d->sampler_kind ? d->sampler_kind : AGX3_TEX_OPERAND_IMMEDIATE;
   const bool immediate_operands = tk == AGX3_TEX_OPERAND_IMMEDIATE &&
                                   sk == AGX3_TEX_OPERAND_IMMEDIATE;

   base[nb++] = (struct agx3_field_kv)KV(103, tk);
   base[nb++] = (struct agx3_field_kv)KV(104, tk == AGX3_TEX_OPERAND_IMMEDIATE ? 1 : 2);
   base[nb++] = (struct agx3_field_kv)KV(112, 4);
   base[nb++] = (struct agx3_field_kv)KV(118, 3);
   base[nb++] = (struct agx3_field_kv)KV(119, 3);
   base[nb++] = (struct agx3_field_kv)KV(132, sk);
   base[nb++] = (struct agx3_field_kv)KV(133, sk == AGX3_TEX_OPERAND_IMMEDIATE ? 1 : 2);
   base[nb++] = (struct agx3_field_kv)KV(138, 1);
   if (enc == AGX_ISA_ENCODING_AGX3C) {
      base[nb++] = (struct agx3_field_kv)KV(24, 2);
      base[nb++] = (struct agx3_field_kv)KV(60, 2);
      base[nb++] = (struct agx3_field_kv)KV(61, 164);
   }

   struct agx3_field_kv ov[24];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(2, d->dest);
   ov[n++] = (struct agx3_field_kv)KV(10, d->width);
   ov[n++] = (struct agx3_field_kv)KV(12, d->slot);
   ov[n++] = (struct agx3_field_kv)KV(19, d->coord);
   ov[n++] = (struct agx3_field_kv)KV(26, d->slot);
   ov[n++] = (struct agx3_field_kv)KV(27, d->coord_kill ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(37, (d->register_lod || d->multisample) ? 0 : 1);
   ov[n++] = (struct agx3_field_kv)KV(75, d->mask);
   ov[n++] = (struct agx3_field_kv)KV(76, d->three_d ? 3 : d->multisample ? 2 : 1);
   if (d->array)
      ov[n++] = (struct agx3_field_kv)KV(77, 2); /* array, register layer */
   ov[n++] = (struct agx3_field_kv)KV(78, 1);  /* texel fetch */
   /* Each operand lands in the field its kind selects: 95/96/97 for the
    * texture, 124/125/126 for the sampler. */
   ov[n++] = (struct agx3_field_kv)KV(
      tk == AGX3_TEX_OPERAND_IMMEDIATE   ? 95
      : tk == AGX3_TEX_OPERAND_REGISTER  ? 96
                                         : 97,
      tk == AGX3_TEX_OPERAND_IMMEDIATE ? d->texture : d->texture_value);
   ov[n++] = (struct agx3_field_kv)KV(
      sk == AGX3_TEX_OPERAND_IMMEDIATE   ? 124
      : sk == AGX3_TEX_OPERAND_REGISTER  ? 125
                                         : 126,
      d->sampler_value);
   ov[n++] = (struct agx3_field_kv)KV(107, d->texture_kill);
   ov[n++] = (struct agx3_field_kv)KV(139, d->multisample ? 0 : d->register_lod ? 4 : 2);
   if (enc == AGX_ISA_ENCODING_AGX3B)
      ov[n++] = (struct agx3_field_kv)KV(147, d->final ? 2 : 0);
   else {
      ov[n++] = (struct agx3_field_kv)KV(150, d->final ? 2 : 0);
      ov[n++] = (struct agx3_field_kv)KV(155, 1);
   }
   for (unsigned i = 0; i < 8; ++i)
      if (d->wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(62 + i, 1);

   /*
    * Immediate operands are ep3; register and uniform operands are ep4. Only
    * AGX3b is witnessed for the non-immediate kinds, so AGX3c refuses them
    * rather than assuming its encoding points line up.
    */
   if (!immediate_operands && enc != AGX_ISA_ENCODING_AGX3B)
      return false;
   const int ep =
      enc == AGX_ISA_ENCODING_AGX3B ? (immediate_operands ? 3 : 4) : 0;
   if (!agx3_bind_compose(enc, "smp", ep, base, nb, ov, n, out, len)) {
      memset(out, 0, AGX3_TEX_MAX_LENGTH);
      *len = 0;
      return false;
   }
   return true;
}

static bool
mask_witnessed(uint8_t mask)
{
   /* All 15 nonempty RGBA subsets are witnessed by native fragment samples.
    * Results remain densely packed in selected-component order. */
   return mask != 0 && (mask & ~15u) == 0;
}

static bool
sample_ok(enum agx_isa_encoding enc, const struct agx3_tex_sample_desc *d)
{
   if (d == NULL || (enc != AGX_ISA_ENCODING_AGX3B &&
                     enc != AGX_ISA_ENCODING_AGX3C))
      return false;

   if ((d->width != AGX3_TEX_W16 && d->width != AGX3_TEX_W32) ||
       d->dest >= 128 || (d->width == AGX3_TEX_W32 && (d->dest & 1)) ||
       (d->coord & 3) || d->coord > 60 || !mask_witnessed(d->mask) ||
       d->texture > 15 || d->sampler > 15)
      return false;

   if (d->query_lod &&
       (enc != AGX_ISA_ENCODING_AGX3B || d->array || d->cube || d->three_d ||
        d->shadow || d->gather || d->sample_offset || d->mask != 3 ||
        d->width != AGX3_TEX_W32 || d->lod != AGX3_TEX_LOD_AUTO))
      return false;

   if (d->three_d && (enc != AGX_ISA_ENCODING_AGX3B || d->array ||
                       d->cube || d->shadow || d->gather || d->sample_offset ||
                       (d->lod != AGX3_TEX_LOD_LEVEL &&
                        d->lod != AGX3_TEX_LOD_BIAS_REGISTER &&
                        d->lod != AGX3_TEX_LOD_REGISTER &&
                        d->lod != AGX3_TEX_LOD_AUTO)))
      return false;

   if (d->lod == AGX3_TEX_LOD_BIAS_REGISTER &&
       (enc != AGX_ISA_ENCODING_AGX3B || d->cube || d->shadow || d->gather))
      return false;

   /* G16G sample(level(0), int2(-1,2)) uses the same packed signed
    * nibbles as gather. Native indexed array gather(-8,-8) also uses f43.
    * Native array bias samples use the same field. Cube and shadow offsets
    * remain unqualified. */
   if (d->sample_offset &&
       (enc != AGX_ISA_ENCODING_AGX3B ||
        d->cube || d->shadow))
      return false;

   if (d->retain_helpers && enc != AGX_ISA_ENCODING_AGX3B)
      return false;

   if (d->gather &&
       (enc != AGX_ISA_ENCODING_AGX3B ||
        d->gather_component > 3 || d->mask != 15 ||
        d->width != AGX3_TEX_W32 || d->lod != AGX3_TEX_LOD_AUTO))
      return false;

   /* M4 25G83 sample_compare places its F32 reference in coordinate half4.
    * Cube face and gather controls compose independently; M4 CTS validates
    * cube comparisons and four-component comparison gathers as well. */
   if (d->shadow &&
       (enc != AGX_ISA_ENCODING_AGX3B || d->coord != 0 ||
        d->mask != (d->gather ? 15 : 1) || d->width != AGX3_TEX_W32 ||
        d->lod == AGX3_TEX_LOD_REGISTER || d->lod_x64 != 0))
      return false;

   if (d->lod == AGX3_TEX_LOD_LEVEL) {
      /* 12-bit immediate level field; witnessed up to 464 (level 7.25). */
      if (d->lod_x64 > 0xfff)
         return false;
   } else if ((d->lod != AGX3_TEX_LOD_AUTO &&
               !((d->lod == AGX3_TEX_LOD_REGISTER || d->lod == AGX3_TEX_LOD_BIAS_REGISTER) &&
                 enc == AGX_ISA_ENCODING_AGX3B)) || d->lod_x64 != 0) {
      return false;
   }

   if (enc == AGX_ISA_ENCODING_AGX3C && d->mask != 1 && d->mask != 3 &&
       d->mask != 8 && d->mask != 12 && d->mask != 15)
      return false;

   if (d->cube && (enc != AGX_ISA_ENCODING_AGX3B ||
                   (d->lod != AGX3_TEX_LOD_AUTO &&
                    d->lod != AGX3_TEX_LOD_LEVEL &&
                    d->lod != AGX3_TEX_LOD_REGISTER)))
      return false;

   /* Register LOD and layer use one fused PACK_SMPIR operand. */
   if (d->array && enc != AGX_ISA_ENCODING_AGX3B)
      return false;

   const unsigned tk = d->texture_kind ? d->texture_kind : 1;
   const unsigned sk = d->sampler_kind ? d->sampler_kind : 1;
   if (tk > 3 || sk > 3 || (tk == 1) != (sk == 1) ||
       ((tk != 1 || sk != 1) && enc != AGX_ISA_ENCODING_AGX3B) ||
       (tk != 1 && ((d->texture_value & 1) || d->texture_value > 252)) ||
       (sk != 1 && ((d->sampler_value & 1) || d->sampler_value > 252)) ||
       (d->texture_kill && tk != 2) || (d->sampler_kill && sk != 2))
      return false;

   const unsigned slots = enc == AGX_ISA_ENCODING_AGX3C ? 8 : 6;
   const uint16_t waits = enc == AGX_ISA_ENCODING_AGX3C ? 0xff : 0x3f;
   return d->slot >= 1 && d->slot <= slots &&
          (d->wait_mask & ~waits) == 0 && one_or_zero(d->wait_mask);
}

bool
agx3_tex_encode_sample(enum agx_isa_encoding enc,
                       const struct agx3_tex_sample_desc *d,
                       uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL)
      return false;
   memset(out, 0, AGX3_TEX_MAX_LENGTH);
   *len = 0;
   if (!sample_ok(enc, d))
      return false;

   /* Ordinary colour samples carry f74=1 on every mask value. Native M4
    * depth comparisons instead use f74=0, mask1 and f143=1. */
   const unsigned tk = d->texture_kind ? d->texture_kind : 1;
   const unsigned sk = d->sampler_kind ? d->sampler_kind : 1;
   const bool immediate = tk == 1 && sk == 1;
   struct agx3_field_kv base[20];
   unsigned nb = 0;
   base[nb++] = (struct agx3_field_kv)KV(9, 2);
   base[nb++] = (struct agx3_field_kv)KV(23, 5);
   base[nb++] = (struct agx3_field_kv)KV(38, 1);
   base[nb++] = (struct agx3_field_kv)KV(52, 1);
   base[nb++] = (struct agx3_field_kv)KV(74, !d->shadow && !d->gather && !d->query_lod);
   base[nb++] = (struct agx3_field_kv)KV(89, 3);
   base[nb++] = (struct agx3_field_kv)KV(90, 3);
   base[nb++] = (struct agx3_field_kv)KV(103, tk);
   base[nb++] = (struct agx3_field_kv)KV(104, immediate ? 1 : 2);
   base[nb++] = (struct agx3_field_kv)KV(112, 4);
   base[nb++] = (struct agx3_field_kv)KV(118, 3);
   base[nb++] = (struct agx3_field_kv)KV(119, 3);
   base[nb++] = (struct agx3_field_kv)KV(132, sk);
   base[nb++] = (struct agx3_field_kv)KV(133, immediate ? 1 : 2);
   base[nb++] = (struct agx3_field_kv)KV(138, 1);
   if (enc == AGX_ISA_ENCODING_AGX3C) {
      base[nb++] = (struct agx3_field_kv)KV(24, 2);
      base[nb++] = (struct agx3_field_kv)KV(60, 2);
      base[nb++] = (struct agx3_field_kv)KV(61, 164);
   }

   struct agx3_field_kv ov[31];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(2, d->dest);
   ov[n++] = (struct agx3_field_kv)KV(10, d->width);
   ov[n++] = (struct agx3_field_kv)KV(12, d->slot);
   ov[n++] = (struct agx3_field_kv)KV(19, d->coord);
   ov[n++] = (struct agx3_field_kv)KV(26, d->slot);
   ov[n++] = (struct agx3_field_kv)KV(27, d->coord_kill ? 1 : 0);
   ov[n++] = (struct agx3_field_kv)KV(75, d->mask);
   ov[n++] = (struct agx3_field_kv)KV(76, d->cube ? 4 : d->three_d ? 3 : 1);
   ov[n++] = (struct agx3_field_kv)KV(78, 2);  /* filtered sample */
   if (d->query_lod)
      ov[n++] = (struct agx3_field_kv)KV(146, 1);
   if (d->gather) {
      ov[n++] = (struct agx3_field_kv)KV(144, 1);
      ov[n++] = (struct agx3_field_kv)KV(145, d->gather_component);
   }
   if (d->sample_offset) {
      ov[n++] = (struct agx3_field_kv)KV(141, 1);
      ov[n++] = (struct agx3_field_kv)KV(51, 1);
      ov[n++] = (struct agx3_field_kv)KV(43, d->sample_offset);
   }
   if (d->shadow)
      ov[n++] = (struct agx3_field_kv)KV(143, 1);
   if (d->array)
      ov[n++] = (struct agx3_field_kv)KV(77, 2); /* register array layer */
   ov[n++] = (struct agx3_field_kv)KV(94 + tk, tk == 1 ? d->texture : d->texture_value);
   ov[n++] = (struct agx3_field_kv)KV(123 + sk, sk == 1 ? d->sampler : d->sampler_value);
   if (d->texture_kill)
      ov[n++] = (struct agx3_field_kv)KV(107, 1);
   if (d->sampler_kill)
      ov[n++] = (struct agx3_field_kv)KV(136, 1);
   /* f156 appears in native terminal fragment samples. On G16G, setting it
    * before later implicit samples stops their helper-coordinate updates:
    * a new staging bank then gives the wrong LOD at primitive boundaries.
    * Reusing an old bank merely hides the bug with stale helper coordinates.
    * This is independent of LOD selection (f139), including register LODs.
    * Keep the exact native specimens by default; compiler-generated shaders
    * request retention until the normal colour termination protocol.
    */
   if (d->lod == AGX3_TEX_LOD_AUTO) {
      ov[n++] = (struct agx3_field_kv)KV(156, !d->retain_helpers && !d->gather);
   } else if (d->lod == AGX3_TEX_LOD_REGISTER ||
              d->lod == AGX3_TEX_LOD_BIAS_REGISTER) {
      ov[n++] = (struct agx3_field_kv)KV(28, 0); /* packed level bank */
      ov[n++] = (struct agx3_field_kv)KV(139, d->lod == AGX3_TEX_LOD_BIAS_REGISTER ? 3 : 4);
      ov[n++] = (struct agx3_field_kv)KV(156, !d->retain_helpers);
   } else {
      ov[n++] = (struct agx3_field_kv)KV(37, 1);  /* immediate LOD operand */
      ov[n++] = (struct agx3_field_kv)KV(139, 2); /* explicit-level mode */
      ov[n++] = (struct agx3_field_kv)KV(29, d->lod_x64);
      if (d->shadow || d->sample_offset)
         ov[n++] = (struct agx3_field_kv)KV(156, !d->retain_helpers);
   }
   if (enc == AGX_ISA_ENCODING_AGX3B)
      ov[n++] = (struct agx3_field_kv)KV(147, d->final ? 2 : 0);
   else
      ov[n++] = (struct agx3_field_kv)KV(150, d->final ? 2 : 0);
   for (unsigned i = 0; i < 8; ++i)
      if (d->wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(62 + i, 1);

   if (!agx3_bind_compose(enc, "smp", immediate ? 3 : 4, base, nb, ov, n, out, len)) {
      memset(out, 0, AGX3_TEX_MAX_LENGTH);
      *len = 0;
      return false;
   }
   return true;
}

bool
agx3_tex_stage_copy_hints(enum agx_isa_encoding enc, uint16_t dest, uint16_t src,
                    uint8_t slot, uint16_t wait_mask,
                          bool src_cache, bool src_discard,
                    uint8_t out[AGX3_TEX_COPY_MAX_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL)
      return false;
   memset(out, 0, AGX3_TEX_COPY_MAX_LENGTH);
   *len = 0;
   if ((src_cache && src_discard) ||
       (enc == AGX_ISA_ENCODING_AGX3C && (src_cache || !src_discard)))
      return false;

   if (enc != AGX_ISA_ENCODING_AGX3B && enc != AGX_ISA_ENCODING_AGX3C)
      return false;

   /* One even 32-bit register each way, as in every witness. */
   if ((dest & 1) || (src & 1) || dest >= 256 || src >= 256)
      return false;

   const unsigned slots = enc == AGX_ISA_ENCODING_AGX3C ? 8 : 6;
   const uint16_t waits = enc == AGX_ISA_ENCODING_AGX3C ? 0xff : 0x3f;
   if (slot < 1 || slot > slots || (wait_mask & ~waits) ||
       !one_or_zero(wait_mask))
      return false;

   struct agx3_field_kv base[8];
   unsigned nb = 0;
   base[nb++] = (struct agx3_field_kv)KV(9, 5);
   base[nb++] = (struct agx3_field_kv)KV(10, 2);
   base[nb++] = (struct agx3_field_kv)KV(23, 2);
   base[nb++] = (struct agx3_field_kv)KV(24, 2);
   if (enc == AGX_ISA_ENCODING_AGX3C) {
      base[nb++] = (struct agx3_field_kv)KV(28, 1);
      base[nb++] = (struct agx3_field_kv)KV(29, 35);
   }

   struct agx3_field_kv ov[12];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(5, dest);
   /* Preserve the upper register bank. Unlike PACK_SMPIR below, COPY has
    * no post-compose source-bit fixup: masking here aliases r64 with r0. */
   ov[n++] = (struct agx3_field_kv)KV(16, src);
   ov[n++] = (struct agx3_field_kv)KV(25, src_cache);
   ov[n++] = (struct agx3_field_kv)KV(27, src_discard);
   ov[n++] = (struct agx3_field_kv)KV(12, slot);
   for (unsigned i = 0; i < 8; ++i)
      if (wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(30 + i, 1);

   /* The compact form first; Apple escalates the encoding point when a slot
    * or wait bit does not fit, and the composer's first-success order
    * reproduces that choice on every witness. */
   for (int ep = 0; ep <= 1; ++ep) {
      if (agx3_bind_compose(enc, "copy", ep, base, nb, ov, n, out, len))
         return true;
   }
   memset(out, 0, AGX3_TEX_COPY_MAX_LENGTH);
   *len = 0;
   return false;
}

bool
agx3_tex_stage_copy(enum agx_isa_encoding enc, uint16_t dest, uint16_t src,
                    uint8_t slot, uint16_t wait_mask,
                    uint8_t out[AGX3_TEX_COPY_MAX_LENGTH], uint8_t *len)
{
   return agx3_tex_stage_copy_hints(enc,dest,src,slot,wait_mask,false,true,out,len);
}

bool
agx3_tex_encode_image_write_rgba8_2d(
   enum agx_isa_encoding enc, uint16_t wait_mask, uint8_t image_index,
   bool normalized,
   uint8_t out[AGX3_IMAGE_WRITE_LENGTH], uint8_t *len)
{
   if (out == NULL || len == NULL)
      return false;

   memset(out, 0, AGX3_IMAGE_WRITE_LENGTH);
   *len = 0;
   /* This argument is the dependency consumed by the first data-staging COPY,
    * not an imgwci field. The fixed native instruction always waits for the
    * staging-slot2 writes, so both an outstanding slot1 producer and data
    * already retired by an earlier consumer use the same grounded bytes. */
   if (enc != AGX_ISA_ENCODING_AGX3B || (wait_mask & ~1u) != 0 ||
       !((image_index == 0 && !normalized) ||
         (image_index == 1 && normalized)))
      return false;

   /* Nonzero field values recovered by decoding the native 16-byte
    * imgwci.ep0. Every omitted field is intentionally zero; the composer
    * validates the complete selected form. Fields 70/91 are the only
    * differences in Apple's sampled-float-to-write-state1 witness. */
   const struct agx3_field_kv fields[] = {
      KV(9, 5),   KV(12, 1),  KV(13, 1),   KV(24, 1), KV(38, 1),
      KV(51, 1),  KV(52, 1),  KV(60, 1),   KV(71, 2), KV(72, 1),
      KV(74, 1),  KV(79, 4),  KV(85, 3),   KV(86, 3), KV(99, 1),
      KV(100, 1), KV(113, 1), KV(70, normalized), KV(91, image_index),
   };

   if (!agx3_bind_compose(enc, "imgwci", 0, fields,
                          sizeof(fields) / sizeof(fields[0]),
                          NULL, 0, out, len) ||
       *len != AGX3_IMAGE_WRITE_LENGTH) {
      memset(out, 0, AGX3_IMAGE_WRITE_LENGTH);
      *len = 0;
      return false;
   }

   return true;
}

/* G16G applegpu-nt argument-buffer writes. The operand supplies an index
 * into the descriptor heap addressed by u0, in a GPR or uniform pair.
 * Pixel comparison qualifies this ABI on M4 for R8 and RG16F writes. */
bool
agx3_tex_encode_image_write_indexed_typed(
   enum agx_isa_encoding enc, uint16_t pointer, bool uniform, bool kill, bool floating, bool three_d, bool array, uint16_t staging_base, uint8_t staging_slot,
   uint8_t out[AGX3_IMAGE_WRITE_LENGTH], uint8_t *len)
{
   if (!out || !len)
      return false;
   memset(out, 0, AGX3_IMAGE_WRITE_LENGTH);
   *len = 0;
   if ((three_d && array) || staging_base > 240 || (staging_base & 15) ||
       enc != AGX_ISA_ENCODING_AGX3B || (pointer & 1) || pointer > 252 ||
       (uniform && kill) || (staging_slot < 1 || staging_slot > 3))
      return false;
   const struct agx3_field_kv fields[] = {
      /* f12 publishes completion; f59..64 wait for staging producers.
       * They are separate fields. Use the slot tracked by the compiler for
       * both, so a subsequent publisher cannot overwrite live staging. */
      KV(5,staging_base), KV(9,5), KV(12,staging_slot), KV(13,1), KV(24,1), KV(38,1),
      KV(51,1), KV(52,1), KV(58 + staging_slot,1), KV(70,floating), KV(71,2), KV(72,three_d ? 3 : 1),
      /* f79 selects the table-base uniform, in half-register units.
       * Native array witnesses use u4; Mesa's resolved PBE heap is in u0.
       * Mirroring the heap into u4 makes native f79=4 pass the same probe,
       * independently of staging slots or the descriptor's table index. */
      KV(73,array ? 2 : 0), KV(79,0),
      KV(74,1), KV(85,3), KV(86,3), KV(99,uniform ? 3 : 2),
      KV(100,2), KV(113,1), KV(uniform ? 93 : 92,pointer), KV(103,kill),
   };
   return agx3_bind_compose(enc, "imgwci", 0, fields, sizeof(fields) / sizeof(fields[0]),
                            NULL, 0, out, len) &&
          *len == AGX3_IMAGE_WRITE_LENGTH;
}

bool
agx3_tex_encode_image_write_indexed_2d(
   enum agx_isa_encoding enc, uint16_t pointer, bool uniform, bool kill,
   uint16_t staging_base, uint8_t staging_slot,
   uint8_t out[AGX3_IMAGE_WRITE_LENGTH], uint8_t *len)
{
   return agx3_tex_encode_image_write_indexed_typed(
      enc, pointer, uniform, kill, true, false, false, staging_base, staging_slot, out, len);
}

static uint16_t
word(const uint8_t *p, unsigned i)
{
   return (uint16_t)p[2 * i] | ((uint16_t)p[2 * i + 1] << 8);
}

static bool
decode_b(const uint8_t *p, struct agx3_tex_fetch_desc *d)
{
   const uint16_t w0 = word(p, 0), w1 = word(p, 1), w2 = word(p, 2),
                  w4 = word(p, 4), w5 = word(p, 5);
   d->width = (w0 & 0x8000) ? AGX3_TEX_W32 : AGX3_TEX_W16;
   d->dest = d->width == AGX3_TEX_W32
                ? (((w0 >> 3) & 0x1f) << 1) | (((w1 >> 6) & 1) << 6)
                : ((w0 >> 3) & 0x1f) | (((w1 >> 6) & 3) << 5);
   d->coord = (((w0 >> 8) & 7) << 1) | (((w1 >> 8) & 3) << 4);
   d->coord_kill = (w1 >> 13) & 1;
   d->final = (w1 >> 15) & 1;

   const unsigned mask_code = ((w1 >> 11) & 3) | (((w2 >> 5) & 1) << 2) |
                              (((w5 >> 6) & 1) << 3);
   static const uint8_t masks[16] = {
      1, 2, 4, 8, 9, 3, 7, 15, 5, 6, 10, 12, 13, 11, 14, 0,
   };
   d->mask = masks[mask_code];
   if (!d->mask)
      return false;
   d->sampler_value = ((w4 >> 8) & 1) | ((w2 & 0xf) << 1) |
                      (((w4 >> 1) & 7) << 5);

   const unsigned slot_code = (w2 >> 10) & 7;
   if (slot_code > 5)
      return false;
   d->slot = slot_code + 1;
   d->texture = ((w4 >> 7) & 1) | (((w0 >> 11) & 0xf) << 1) |
                (((w4 >> 4) & 7) << 5);

   const unsigned wait_code = (w2 >> 7) & 7;
   if (wait_code > 6)
      return false;
   d->wait_mask = wait_code == 0 ? 0 : (1u << (wait_code - 1));
   return true;
}

static bool
decode_c(const uint8_t *p, struct agx3_tex_fetch_desc *d)
{
   const uint16_t w0 = word(p, 0), w1 = word(p, 1), w2 = word(p, 2),
                  w3 = word(p, 3);
   d->width = (w0 & 8) ? AGX3_TEX_W32 : AGX3_TEX_W16;
   d->dest = ((w3 >> 2) & 1) | (((w0 >> 4) & 0xf) << 1) |
             (((w1 >> 6) & 3) << 5) | (((w3 >> 12) & 1) << 7);
   d->coord = (((w1 >> 10) & 0x3f) << 2) | (((w1 >> 9) & 1) << 8);
   d->coord_kill = (w1 >> 4) & 1;
   d->final = (w2 >> 15) & 1;

   const unsigned mask_code = ((w3 >> 5) & 3) | (((w3 >> 11) & 1) << 2);
   if (mask_code == 0)
      d->mask = 1;
   else if (mask_code == 7)
      d->mask = 15;
   else
      return false;

   d->slot = ((w2 >> 3) & 3) | (((w3 >> 4) & 1) << 2);
   d->slot++;
   d->texture = ((w3 >> 3) & 1) | (((w2 >> 12) & 1) << 1) |
                (((w2 >> 13) & 3) << 2);
   const unsigned wait_code = (w3 >> 13) & 7;
   d->wait_mask = wait_code == 0 ? 0 : (1u << (wait_code - 1));
   return true;
}

bool
agx3_tex_decode_fetch(enum agx_isa_encoding enc, const void *instruction,
                      uint32_t instruction_size,
                      struct agx3_tex_fetch_desc *out)
{
   if (out == NULL)
      return false;
   memset(out, 0, sizeof(*out));
   if (enc != AGX_ISA_ENCODING_AGX3B && enc != AGX_ISA_ENCODING_AGX3C)
      return false;
   if (enc == AGX_ISA_ENCODING_AGX3B && instruction && instruction_size == 20) {
      const uint8_t *p = instruction;
      struct agx3_tex_fetch_desc d = {0};
      if (!decode_b(p, &d))
         return false;
      d.texture = 0;
      d.texture_value = (((word(p, 0) >> 12) & 7) << 1) |
                        (((word(p, 4) >> 4) & 7) << 4) |
                        (((p[15] >> 2) & 1) << 7);
      d.wait_mask = ((p[4] >> 7) & 1) | ((p[5] & 3) << 1) |
                    (((p[18] >> 4) & 7) << 3);
      d.sampler_kind = AGX3_TEX_OPERAND_IMMEDIATE;
      /* Repack both native index kinds. Their context selector, upper bits,
       * and every reserved field are validated by exact byte comparison. */
      for (unsigned kind = AGX3_TEX_OPERAND_REGISTER;
           kind <= AGX3_TEX_OPERAND_UNIFORM; ++kind) {
         d.texture_kind = kind;
         d.texture_kill = kind == AGX3_TEX_OPERAND_REGISTER && (p[15] & 8);
         for (unsigned axis = 0; axis < 16; ++axis) {
            d.array = axis & 1;
            d.register_lod = axis & 2;
            d.multisample = axis & 4;
            d.three_d = axis & 8;
            uint8_t canonical[AGX3_TEX_MAX_LENGTH], length;
            if (agx3_tex_encode_fetch(enc, &d, canonical, &length) &&
                length == instruction_size &&
                !memcmp(canonical, instruction, length)) {
               *out = d;
               return true;
            }
         }
      }
      return false;
   }
   const uint32_t expected = enc == AGX_ISA_ENCODING_AGX3B ? 14 : 8;
   if (instruction == NULL || instruction_size != expected)
      return false;

   struct agx3_tex_fetch_desc d = {0};
   const uint8_t *p = instruction;
   if (!(enc == AGX_ISA_ENCODING_AGX3B ? decode_b(p, &d) : decode_c(p, &d)) ||
       !fetch_ok(enc, &d))
      return false;

   /* The array axis is a single selector the hand decoders above do not
    * extract; recover it the same way everything else here is validated, by
    * requiring one of the two readings to repack byte-for-byte. */
   for (unsigned a = 0; a < 16; ++a) {
      uint8_t canonical[AGX3_TEX_MAX_LENGTH];
      uint8_t length = 0;
      d.array = a & 1;
      d.register_lod = a & 2;
      d.multisample = a & 4;
      d.three_d = a & 8;
      if (agx3_tex_encode_fetch(enc, &d, canonical, &length) &&
          length == instruction_size &&
          memcmp(canonical, instruction, length) == 0) {
         *out = d;
         return true;
      }
   }

   return false;
}

/* Fresh native G17P LOD fragment witnesses use this 12-byte PACK_SMPIR for a
 * scalar half LOD, preserving the source when it remains live. f5=6 selects
 * operand bank 6, not architectural r3. */
static bool
stage_lod(enum agx_isa_encoding enc, uint16_t src, uint8_t slot,
                   uint16_t wait_mask, bool src_cache, bool src_discard, bool integer, bool uniform,
                   uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   if (!out || !len)
      return false;
   memset(out, 0, AGX3_TEX_MAX_LENGTH);
   *len = 0;
   if (enc != AGX_ISA_ENCODING_AGX3B || src >= 256 || slot < 1 || slot > 6 ||
       (wait_mask & ~0x3f) || (src_cache && src_discard))
      return false;

   const struct agx3_field_kv base[] = {
      KV(5, 6), KV(9, 5), KV(10, 2), KV(23, uniform ? 3 : 2), KV(24, 1),
      KV(38, 1), KV(51, 1), KV(52, 2), KV(74, integer ? 3 : 2), KV(75, 16),
   };
   struct agx3_field_kv ov[10];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(uniform ? 17 : 16, uniform ? src : src & 0x7f);
   ov[n++] = (struct agx3_field_kv)KV(12, slot);
   ov[n++] = (struct agx3_field_kv)KV(25, src_cache);
   ov[n++] = (struct agx3_field_kv)KV(27, src_discard);
   for (unsigned i = 0; i < 6; ++i)
      if (wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(58 + i, 1);
   if (!agx3_bind_compose(enc, "pack_smpir", 0, base,
                         sizeof(base) / sizeof(base[0]), ov, n, out, len))
      return false;

   /* The source is an eight-bit half-register index (f16 bits 0..6 at
    * instruction bits 41..47, bit 7 at bit 48). Apple's encoder's 191
    * bound is a compiler allocation policy, not a narrower wire field.
    * Keep the same native instruction for Mesa's full 256-half bank. */
   if (!uniform)
      out[6] |= (src >> 7) & 1;
   return true;
}

bool
agx3_tex_stage_lod(enum agx_isa_encoding enc, uint16_t src, uint8_t slot,
                   uint16_t wait_mask, bool src_cache, bool src_discard,
                   uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   return stage_lod(enc, src, slot, wait_mask, src_cache, src_discard, false, false, out, len);
}

bool
agx3_tex_stage_fetch_lod(enum agx_isa_encoding enc, uint16_t src, bool uniform, uint8_t slot,
                         uint16_t wait_mask, bool src_cache, bool src_discard,
                         uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   /* Apple G16G Metal tex.read(coord, varying_uint): PACK_SMPIR f74=3;
    * source half2, slot1 gives 1705540600040004f0c00c00. */
   return stage_lod(enc, src, slot, wait_mask, src_cache, src_discard, true, uniform, out, len);
}

/* The dynamic array-layer witness uses PACK_SMPIR like register LOD, but its
 * operand-type selectors differ. In particular, f74=4/f75=0 produces the
 * native 1705540600080000f0c00400 for discarded r2 on slot 1; the LOD
 * baseline's f74=2/f75=16 silently leaves the sample destination unwritten.
 * Register-indexed array samples use the independently witnessed slot-6 form
 * 1705540600180000f0e40400 for discarded r6. */
bool
agx3_tex_stage_array_layer(enum agx_isa_encoding enc, uint16_t src,
                           uint8_t slot, uint16_t wait_mask, bool src_cache,
                           bool src_discard,
                           uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   if (!out || !len)
      return false;
   memset(out, 0, AGX3_TEX_MAX_LENGTH);
   *len = 0;
   if (enc != AGX_ISA_ENCODING_AGX3B || src >= 256 || slot < 1 || slot > 6 ||
       (wait_mask & ~0x3f) || (src_cache && src_discard))
      return false;

   const struct agx3_field_kv base[] = {
      KV(5, 6), KV(9, 5), KV(10, 2), KV(23, 2), KV(24, 1),
      KV(38, 1), KV(51, 1), KV(52, 2), KV(74, 4),
   };
   struct agx3_field_kv ov[10];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(16, src & 0x7f);
   ov[n++] = (struct agx3_field_kv)KV(12, slot);
   ov[n++] = (struct agx3_field_kv)KV(25, src_cache);
   ov[n++] = (struct agx3_field_kv)KV(27, src_discard);
   for (unsigned i = 0; i < 6; ++i)
      if (wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(58 + i, 1);
   if (!agx3_bind_compose(enc, "pack_smpir", 0, base,
                         sizeof(base) / sizeof(base[0]), ov, n, out, len))
      return false;

   out[6] |= (src >> 7) & 1;
   return true;
}

/* AIR-NT 32023.883 G16G texture2d_array.sample(level(v.w)) packs the
 * integer layer and half-float LOD together. Two independent PACKs to bank6
 * would overwrite one another. Native layer4/lod5 is 1705540600081400f0c02500.
 * Cube samples use the same fused operand with face selector f74=10:
 * native face10/lod11 is 1705540600142c00f0c01100. Carry the two source
 * lifetime hints independently. */
bool
agx3_tex_stage_layer_lod(enum agx_isa_encoding enc, uint16_t layer,
                        uint16_t lod, uint8_t slot, uint16_t wait_mask,
                        bool layer_discard, bool lod_discard, bool cube,
                        uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   if (!out || !len)
      return false;
   memset(out, 0, AGX3_TEX_MAX_LENGTH);
   *len = 0;
   if (enc != AGX_ISA_ENCODING_AGX3B || layer >= 192 || lod >= 192 ||
       slot < 1 || slot > 6 || (wait_mask & ~0x3f))
      return false;
   const struct agx3_field_kv base[] = {
      KV(5,6), KV(9,5), KV(10,2), KV(12,slot), KV(16,layer),
      KV(23,2), KV(24,1), KV(27,layer_discard), KV(30,lod),
      KV(37,2), KV(38,1), KV(39,lod_discard),
      KV(41,1), KV(51,1), KV(52,2), KV(74,cube ? 10 : 4), KV(76,2), KV(77,16),
   };
   struct agx3_field_kv waits[6];
   unsigned n = 0;
   for (unsigned i = 0; i < 6; ++i)
      if (wait_mask & (1u << i))
         waits[n++] = (struct agx3_field_kv)KV(58+i,1);
   return agx3_bind_compose(enc, "pack_smpir", 0, base, sizeof(base) / sizeof(base[0]),
                            waits, n, out, len);
}

/* Native G16G dynamic cube-array level: prepacked layer + face + half LOD.
 * All three must be published together; separate writes overwrite bank 6. */
bool
agx3_tex_stage_cube_array_lod(enum agx_isa_encoding enc, uint16_t face,
                             uint16_t lod, uint16_t layer, uint8_t slot,
                             uint16_t wait_mask, bool face_discard,
                             bool lod_discard,
                             uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   if (!out || !len)
      return false;
   memset(out, 0, AGX3_TEX_MAX_LENGTH);
   *len = 0;
   /* The upper bank of the third operand is not yet qualified. */
   if (enc != AGX_ISA_ENCODING_AGX3B || face >= 192 || lod >= 192 ||
       layer >= 128 || (layer & 1) || slot < 1 || slot > 6 ||
       (wait_mask & ~0x3f))
      return false;
   const struct agx3_field_kv base[] = {
      KV(5,6), KV(9,5), KV(10,2), KV(12,slot), KV(16,face),
      KV(23,2), KV(24,1), KV(27,face_discard), KV(30,lod),
      KV(37,2), KV(38,1), KV(39,lod_discard), KV(41,1),
      KV(44,layer), KV(51,2), KV(52,2), KV(55,1),
      KV(74,10), KV(75,12), KV(76,2), KV(77,16),
   };
   struct agx3_field_kv waits[6];
   unsigned n = 0;
   for (unsigned i = 0; i < 6; ++i)
      if (wait_mask & (1u << i))
         waits[n++] = (struct agx3_field_kv)KV(58+i,1);
   return agx3_bind_compose(enc, "pack_smpir", 0, base, sizeof(base) / sizeof(base[0]),
                            waits, n, out, len);
}

/* Cube arrays pack the cube index and projected face into one auxiliary
 * operand. Native G16G layer11/face10 is 1705540600162800f0c02900. */
bool
agx3_tex_stage_cube_array(enum agx_isa_encoding enc, uint16_t layer,
                          uint16_t face, uint8_t slot,
                          bool layer_discard, bool face_discard, uint8_t out[AGX3_TEX_MAX_LENGTH],
                          uint8_t *len)
{
   if (!out || !len)
      return false;
   memset(out, 0, AGX3_TEX_MAX_LENGTH);
   *len = 0;
   if (enc != AGX_ISA_ENCODING_AGX3B || layer >= 256 || face >= 256 ||
       slot < 1 || slot > 6)
      return false;
   const struct agx3_field_kv fields[] = {
      KV(5,6), KV(9,5), KV(10,2), KV(12,slot), KV(16,layer),
      KV(23,2), KV(24,1), KV(27,layer_discard), KV(30,face),
      KV(37,2), KV(38,1), KV(39,face_discard), KV(41,1),
      KV(51,1), KV(52,2), KV(74,4), KV(76,10), KV(77,12),
   };
   return agx3_bind_compose(enc, "pack_smpir", 0, fields,
                            sizeof(fields) / sizeof(fields[0]), NULL, 0,
                            out, len);
}

/* Witnessed by native G16G read-layer Metal compute pipeline. Both operations
 * use ordinary GPR destinations so register allocation owns all temporaries. */
bool
agx3_tex_pack_aux(enum agx_isa_encoding enc, bool sample,
                 uint16_t dest_half, uint16_t source_half,
                 uint16_t layer_half, uint16_t wait_mask,
                 uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len)
{
   if (!out || !len)
      return false;
   memset(out, 0, AGX3_TEX_MAX_LENGTH);
   *len = 0;
   /* The second source's upper bank bit is not qualified yet. */
   if (enc != AGX_ISA_ENCODING_AGX3B ||
       dest_half > 254 || (dest_half & 1) || source_half > 255 ||
       (layer_half != UINT16_MAX && ((layer_half & 1) || layer_half >= 128)) ||
       (wait_mask & ~0x3f))
      return false;
   const struct agx3_field_kv base[] = {
      KV(9, 2), KV(10, 2), KV(11, 1), KV(23, 2), KV(24, 1),
      KV(38, 1), KV(52, 2),
   };
   struct agx3_field_kv ov[20];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(2, dest_half);
   ov[n++] = (struct agx3_field_kv)KV(16, source_half & 127);
   ov[n++] = (struct agx3_field_kv)KV(74, sample ? 9 : 4);
   ov[n++] = (struct agx3_field_kv)KV(51, sample && layer_half != UINT16_MAX ? 2 : 1);
   if (sample) {
      ov[n++] = (struct agx3_field_kv)KV(37, 1);
      ov[n++] = (struct agx3_field_kv)KV(75, 12);
      ov[n++] = (struct agx3_field_kv)KV(76, 1);
      ov[n++] = (struct agx3_field_kv)KV(77, 16);
      if (layer_half != UINT16_MAX)
         ov[n++] = (struct agx3_field_kv)KV(44, layer_half & 127);
   }
   for (unsigned i = 0; i < 6; ++i)
      if (wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(58 + i, 1);
   if (!agx3_bind_compose(enc, "pack_smpir", 0, base, sizeof(base) / sizeof(base[0]),
                        ov, n, out, len))
      return false;
   out[6] |= (source_half >> 7) & 1;
   return true;
}
