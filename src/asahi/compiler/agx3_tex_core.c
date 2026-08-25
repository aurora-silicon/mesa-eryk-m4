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
       (d->mask != 1 && d->mask != 15) || d->texture > 15)
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
   base[nb++] = (struct agx3_field_kv)KV(103, 1);
   base[nb++] = (struct agx3_field_kv)KV(104, 1);
   base[nb++] = (struct agx3_field_kv)KV(112, 4);
   base[nb++] = (struct agx3_field_kv)KV(118, 3);
   base[nb++] = (struct agx3_field_kv)KV(119, 3);
   base[nb++] = (struct agx3_field_kv)KV(132, 1);
   base[nb++] = (struct agx3_field_kv)KV(133, 1);
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
   ov[n++] = (struct agx3_field_kv)KV(37, 1);  /* immediate LOD */
   ov[n++] = (struct agx3_field_kv)KV(75, d->mask);
   ov[n++] = (struct agx3_field_kv)KV(76, 1);  /* 2D */
   ov[n++] = (struct agx3_field_kv)KV(78, 1);  /* texel fetch */
   ov[n++] = (struct agx3_field_kv)KV(95, d->texture);
   ov[n++] = (struct agx3_field_kv)KV(124, 0); /* sampler zero */
   ov[n++] = (struct agx3_field_kv)KV(139, 2); /* LOD0 immediate shape */
   if (enc == AGX_ISA_ENCODING_AGX3B)
      ov[n++] = (struct agx3_field_kv)KV(147, d->final ? 2 : 0);
   else {
      ov[n++] = (struct agx3_field_kv)KV(150, d->final ? 2 : 0);
      ov[n++] = (struct agx3_field_kv)KV(155, 1);
   }
   for (unsigned i = 0; i < 8; ++i)
      if (d->wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(62 + i, 1);

   const int ep = enc == AGX_ISA_ENCODING_AGX3B ? 3 : 0;
   if (!agx3_bind_compose(enc, "smp", ep, base, nb, ov, n, out, len)) {
      memset(out, 0, AGX3_TEX_MAX_LENGTH);
      *len = 0;
      return false;
   }
   return true;
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
   if (mask_code == 0)
      d->mask = 1;
   else if (mask_code == 7)
      d->mask = 15;
   else
      return false;

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
   const uint32_t expected = enc == AGX_ISA_ENCODING_AGX3B ? 14 : 8;
   if (instruction == NULL || instruction_size != expected)
      return false;

   struct agx3_tex_fetch_desc d = {0};
   const uint8_t *p = instruction;
   if (!(enc == AGX_ISA_ENCODING_AGX3B ? decode_b(p, &d) : decode_c(p, &d)) ||
       !fetch_ok(enc, &d))
      return false;

   uint8_t canonical[AGX3_TEX_MAX_LENGTH];
   uint8_t length = 0;
   if (!agx3_tex_encode_fetch(enc, &d, canonical, &length) ||
       length != instruction_size || memcmp(canonical, instruction, length))
      return false;

   *out = d;
   return true;
}
