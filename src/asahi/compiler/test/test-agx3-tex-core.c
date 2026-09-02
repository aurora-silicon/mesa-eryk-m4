/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_tex_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                \
         return 1;                                                             \
      }                                                                        \
   } while (0)

static bool
same(const struct agx3_tex_fetch_desc *a,
     const struct agx3_tex_fetch_desc *b)
{
   return a->dest == b->dest && a->coord == b->coord &&
          a->width == b->width && a->mask == b->mask &&
          a->texture == b->texture && a->slot == b->slot &&
          a->wait_mask == b->wait_mask && a->coord_kill == b->coord_kill &&
          a->final == b->final &&
          a->array == b->array;
}

static int
roundtrip(enum agx_isa_encoding enc, const struct agx3_tex_fetch_desc *d)
{
   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t length = 0;
   struct agx3_tex_fetch_desc decoded;
   CHECK(agx3_tex_encode_fetch(enc, d, bytes, &length));
   CHECK(length == (enc == AGX_ISA_ENCODING_AGX3B ? 14 : 8));
   CHECK(agx3_tex_decode_fetch(enc, bytes, length, &decoded));
   CHECK(same(d, &decoded));
   return 0;
}

static int
test_exact_witnesses(void)
{
   static const uint8_t b[] = {
      0x05, 0x80, 0x0c, 0xb8, 0xb0, 0x00, 0x17,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
   };
   static const uint8_t c[] = {0x0f, 0x06, 0x1a, 0x00,
                               0x40, 0x80, 0x60, 0x29};
   static const uint8_t b_mask1_d0[] = {
      0x05, 0x80, 0x0c, 0xa0, 0x90, 0x00, 0x17,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
   };
   static const uint8_t b_mask1_d2[] = {
      0x0d, 0x80, 0x0c, 0xa0, 0x90, 0x00, 0x17,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
   };
   static const uint8_t c_mask1_d0[] = {0x0f, 0x06, 0x1a, 0x00,
                                        0x40, 0x80, 0x00, 0x21};
   static const uint8_t c_mask1_d2[] = {0x1f, 0x06, 0x1a, 0x00,
                                        0x40, 0x80, 0x00, 0x21};
   struct agx3_tex_fetch_desc d = {
      .dest = 0,
      .coord = 0,
      .width = AGX3_TEX_W32,
      .mask = 15,
      .texture = 0,
      .slot = 1,
      .wait_mask = 1,
      .coord_kill = true,
      .final = true,
   };
   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t length;
   struct agx3_tex_fetch_desc decoded;
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   CHECK(length == sizeof(b) && memcmp(bytes, b, sizeof(b)) == 0);
   CHECK(agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, b, sizeof(b), &decoded));
   CHECK(same(&d, &decoded));
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3C, &d, bytes, &length));
   CHECK(length == sizeof(c) && memcmp(bytes, c, sizeof(c)) == 0);
   CHECK(agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3C, c, sizeof(c), &decoded));
   CHECK(same(&d, &decoded));

   d.mask = 1;
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   CHECK(length == sizeof(b_mask1_d0) &&
         memcmp(bytes, b_mask1_d0, sizeof(b_mask1_d0)) == 0);
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3C, &d, bytes, &length));
   CHECK(length == sizeof(c_mask1_d0) &&
         memcmp(bytes, c_mask1_d0, sizeof(c_mask1_d0)) == 0);
   d.dest = 2;
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   CHECK(length == sizeof(b_mask1_d2) &&
         memcmp(bytes, b_mask1_d2, sizeof(b_mask1_d2)) == 0);
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3C, &d, bytes, &length));
   CHECK(length == sizeof(c_mask1_d2) &&
         memcmp(bytes, c_mask1_d2, sizeof(c_mask1_d2)) == 0);
   return 0;
}

static int
test_supported_axes(void)
{
   for (unsigned ei = 0; ei < 2; ++ei) {
      const enum agx_isa_encoding enc =
         ei ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      struct agx3_tex_fetch_desc d = {
         .width = AGX3_TEX_W32,
         .mask = 15,
         .slot = 1,
      };

      for (unsigned v = 0; v < 128; v += 2) {
         d.dest = v;
         CHECK(roundtrip(enc, &d) == 0);
      }
      d.width = AGX3_TEX_W16;
      for (unsigned v = 0; v < 128; ++v) {
         d.dest = v;
         CHECK(roundtrip(enc, &d) == 0);
      }
      d.dest = 0;
      d.width = AGX3_TEX_W32;
      for (unsigned v = 0; v <= 60; v += 4) {
         d.coord = v;
         CHECK(roundtrip(enc, &d) == 0);
      }
      d.coord = 0;
      for (unsigned v = 0; v < 16; ++v) {
         d.texture = v;
         CHECK(roundtrip(enc, &d) == 0);
      }
      d.texture = 0;
      for (unsigned v = 1; v <= (ei ? 8 : 6); ++v) {
         d.slot = v;
         CHECK(roundtrip(enc, &d) == 0);
      }
      d.slot = 1;
      for (unsigned v = 0; v < (ei ? 7 : 6); ++v) {
         d.wait_mask = 1u << v;
         CHECK(roundtrip(enc, &d) == 0);
      }
      d.wait_mask = 0;
      d.mask = 1;
      CHECK(roundtrip(enc, &d) == 0);
      d.mask = 15;
      d.coord_kill = true;
      CHECK(roundtrip(enc, &d) == 0);
      d.final = true;
      CHECK(roundtrip(enc, &d) == 0);
   }
   return 0;
}

static bool
rejected_and_cleared(enum agx_isa_encoding enc,
                     const struct agx3_tex_fetch_desc *d)
{
   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t length = 0xff;
   memset(bytes, 0xff, sizeof(bytes));
   if (agx3_tex_encode_fetch(enc, d, bytes, &length) || length != 0)
      return false;
   for (unsigned i = 0; i < sizeof(bytes); ++i)
      if (bytes[i] != 0)
         return false;
   return true;
}

static int
test_fail_closed(void)
{
   struct agx3_tex_fetch_desc d = {
      .width = AGX3_TEX_W32, .mask = 15, .slot = 1,
   };
   struct agx3_tex_fetch_desc out;
   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t length;

   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX2, &d));
   d.width = (enum agx3_tex_width)0;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3B, &d));
   d.width = AGX3_TEX_W32;
   d.dest = 1;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3B, &d));
   d.dest = 128;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3C, &d));
   d.dest = 0;
   d.coord = 2;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3B, &d));
   d.coord = 64;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3C, &d));
   d.coord = 0;
   d.mask = 3;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3B, &d));
   d.mask = 15;
   d.texture = 16;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3C, &d));
   d.texture = 0;
   d.slot = 7;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3B, &d));
   d.slot = 9;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3C, &d));
   d.slot = 1;
   d.wait_mask = 3;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3B, &d));
   d.wait_mask = 0x80;
   CHECK(rejected_and_cleared(AGX_ISA_ENCODING_AGX3C, &d));

   d.wait_mask = 0;
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   CHECK(!agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, bytes, length - 1,
                                &out));
   CHECK(!agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, bytes, length + 1,
                                &out));
   bytes[12] ^= 0x80; /* reserved/fixed selector bit */
   CHECK(!agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, bytes, length, &out));
   CHECK(!agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, NULL, length, &out));
   CHECK(!agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, bytes, length, NULL));
   CHECK(!agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX2, bytes, 0, &out));
   return 0;
}

static int
hex_eq(const uint8_t *bytes, uint8_t len, const char *hex)
{
   char got[2 * (AGX3_TEX_MAX_LENGTH > AGX3_TEX_COPY_MAX_LENGTH
                    ? AGX3_TEX_MAX_LENGTH
                    : AGX3_TEX_COPY_MAX_LENGTH) +
            1] = {0};
   for (unsigned i = 0; i < len; ++i)
      snprintf(got + 2 * i, 3, "%02x", bytes[i]);
   if (strcmp(got, hex) != 0) {
      fprintf(stderr, "got %s expected %s\n", got, hex);
      return 0;
   }
   return 1;
}

/*
 * Byte-exact replay of Apple's filtered-sample encodings. Kernel witnesses
 * (explicit level) are from research/gpu/isa-re/gen-x/texops/corpus; the
 * implicit-LOD fragment witnesses (ts_auto / fs_bufcoord) were compiled from
 * fragment render pipelines with Apple's offline translator
 * (MetalToolchain v27.1.5194.15) and decode with the same tables. AGX3b
 * bytes are g16g (byte-identical on g15g); AGX3c bytes are g17s.
 */
static int
test_sample_witnesses(void)
{
   struct svec {
      const char *name;
      struct agx3_tex_sample_desc d;
      const char *hex_b, *hex_c;
   };
   static const struct svec v[] = {
      {"t_base",
       {.dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
        .texture = 0, .sampler = 0, .lod = AGX3_TEX_LOD_LEVEL, .lod_x64 = 0,
        .slot = 1, .wait_mask = 1, .coord_kill = true, .final = true},
       "05800cb8b0000900000010000100",
       "0f041200418060290100010000000000008000000000"},
      {"x_mask_x",
       {.dest = 2, .coord = 0, .width = AGX3_TEX_W32, .mask = 1,
        .lod = AGX3_TEX_LOD_LEVEL, .slot = 1, .wait_mask = 1,
        .coord_kill = true, .final = true},
       "0d800ca090000900000010000100",
       "1f041200418000210100010000000000008000000000"},
      {"x_mask_xy",
       {.dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 3,
        .lod = AGX3_TEX_LOD_LEVEL, .slot = 1, .wait_mask = 1,
        .coord_kill = true, .final = true},
       "05800ca8b0000900000010000100", NULL},
      {"x_mask_zw",
       {.dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 12,
        .lod = AGX3_TEX_LOD_LEVEL, .slot = 1, .wait_mask = 1,
        .coord_kill = true, .final = true},
       "05800cb890000900000050000100",
       "0f041200418060210300010000000000008000000000"},
      {"x_mask_w",
       {.dest = 2, .coord = 0, .width = AGX3_TEX_W32, .mask = 8,
        .lod = AGX3_TEX_LOD_LEVEL, .slot = 1, .wait_mask = 1,
        .coord_kill = true, .final = true},
       "0d800cb890000900000010000100", NULL},
      {"l_lod2 level(2.0)",
       {.dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
        .lod = AGX3_TEX_LOD_LEVEL, .lod_x64 = 128, .slot = 1, .wait_mask = 1,
        .coord_kill = true, .final = true},
       "05800cb8b0000900000010080100",
       "0f041200418060290100010080000000008000000000"},
      {"k_lod7q level(7.25)",
       {.dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
        .lod = AGX3_TEX_LOD_LEVEL, .lod_x64 = 464, .slot = 1, .wait_mask = 1,
        .coord_kill = true, .final = true},
       "05800cb8b00009800000101c0100",
       "0f0412004180602901000100d0010000008000000000"},
      {"L_smp2x sampler 1",
       {.dest = 8, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
        .sampler = 1, .lod = AGX3_TEX_LOD_LEVEL, .slot = 2,
        .coord_kill = true, .final = true},
       "25800cb830240900000110000100",
       "4f041200c98160090100010000000000008000000000"},
      {"L_tex2x texture 1",
       {.dest = 8, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
        .texture = 1, .lod = AGX3_TEX_LOD_LEVEL, .slot = 2,
        .coord_kill = true, .final = true},
       "25800cb830240900800010000100", NULL},
      {"Ls_slots4 #1",
       {.dest = 16, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
        .lod = AGX3_TEX_LOD_LEVEL, .slot = 2, .wait_mask = 1u << 1,
        .coord_kill = true},
       "45800c3830250900000010000100",
       "8f041200c90060490100010000000000008000000000"},
      {"Ls_slots4 #2",
       {.dest = 24, .coord = 4, .width = AGX3_TEX_W32, .mask = 15,
        .texture = 1, .lod = AGX3_TEX_LOD_LEVEL, .slot = 2,
        .wait_mask = 1u << 2, .coord_kill = true},
       "65820c38b0250900800010000100",
       "cf041204c90068690100010000000000008000000000"},
      {"Ls_slots4 #3",
       {.dest = 8, .coord = 8, .width = AGX3_TEX_W32, .mask = 15,
        .texture = 2, .lod = AGX3_TEX_LOD_LEVEL, .slot = 1, .wait_mask = 1,
        .coord_kill = true},
       "258c0c38b0000900000010000100",
       "4f041208411060290100010000000000008000000000"},
      {"Ls_slots4 #4",
       {.dest = 0, .coord = 12, .width = AGX3_TEX_W32, .mask = 15,
        .texture = 3, .lod = AGX3_TEX_LOD_LEVEL, .slot = 3,
        .wait_mask = 1u << 2, .coord_kill = true, .final = true},
       "058e0cb8b0490900800010000100",
       "0f04120c5190686b0100010000000000008000000000"},
      {"ts_auto implicit LOD",
       {.dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
        .lod = AGX3_TEX_LOD_AUTO, .slot = 1, .wait_mask = 1,
        .coord_kill = true, .final = true},
       "05800cb8b0000000010010000100",
       "0f061600418060290118010000000000008000000000"},
   };

   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t length;
   for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
      CHECK(agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3B, &v[i].d, bytes,
                                   &length));
      CHECK(length == 14 && hex_eq(bytes, length, v[i].hex_b));
      if (v[i].hex_c != NULL) {
         CHECK(agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3C, &v[i].d, bytes,
                                      &length));
         CHECK(length == 22 && hex_eq(bytes, length, v[i].hex_c));
      }
   }
   return 0;
}

/*
 * The coordinate-staging copy, replayed against the kernel witnesses
 * (t_base, x_coordreg, Ls_slots4) and the fragment fs_bufcoord witnesses.
 * The wait bit names the producer's scoreboard slot (f30 + slot - 1); a
 * slot above 1 or an AGX3c wait past slot 6 escalates the encoding point,
 * exactly as Apple's own choice does.
 */
static int
test_copy_witnesses(void)
{
   struct cvec {
      const char *name;
      enum agx_isa_encoding enc;
      uint16_t dest, src;
      uint8_t slot;
      uint16_t wait_mask;
      const char *hex;
   };
   static const struct cvec v[] = {
      {"b x_coordreg r0<-r0 wait s6", AGX_ISA_ENCODING_AGX3B, 0, 0, 1,
       1u << 5, "0b0029c4"},
      {"b x_coordreg r1<-r1", AGX_ISA_ENCODING_AGX3B, 2, 2, 1, 0,
       "1b022904"},
      {"b t_base r1<-r0", AGX_ISA_ENCODING_AGX3B, 2, 0, 1, 0, "1b002904"},
      {"b Ls #1 r0<-r4 slot2 wait s6", AGX_ISA_ENCODING_AGX3B, 0, 8, 2,
       1u << 5, "0b092f00420000840000"},
      {"b Ls #2 r1<-r5 slot2", AGX_ISA_ENCODING_AGX3B, 2, 10, 2, 0,
       "1b0b2f00420000040000"},
      {"b Ls #3 r2<-r6 slot3", AGX_ISA_ENCODING_AGX3B, 4, 12, 3, 0,
       "2b0d2f00420000080000"},
      {"b Ls #5 r4<-r0 wait s1", AGX_ISA_ENCODING_AGX3B, 8, 0, 1, 1,
       "4b002924"},
      {"b Ls #6 r5<-r1", AGX_ISA_ENCODING_AGX3B, 10, 2, 1, 0, "5b022904"},
      {"b Ls #7 r6<-r2 slot3", AGX_ISA_ENCODING_AGX3B, 12, 4, 3, 0,
       "6b052f00420000080000"},
      {"c fs_bufcoord r0<-r0 wait s8", AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
       1u << 7, "2b0007022680a402"},
      {"c fs_bufcoord r1<-r1", AGX_ISA_ENCODING_AGX3C, 2, 2, 1, 0,
       "1b022904"},
      {"c t_base r1<-r0", AGX_ISA_ENCODING_AGX3C, 2, 0, 1, 0, "1b002904"},
      {"c Ls #1 r0<-r4 slot2 wait s8", AGX_ISA_ENCODING_AGX3C, 0, 8, 2,
       1u << 7, "2b0007122681a402"},
      {"c Ls #2 r1<-r5 slot2", AGX_ISA_ENCODING_AGX3C, 2, 10, 2, 0,
       "330207122681a402"},
      {"c Ls #3 r2<-r6 slot3", AGX_ISA_ENCODING_AGX3C, 4, 12, 3, 0,
       "2300071a2682a40a"},
      {"c Ls #5 r4<-r0 wait s1", AGX_ISA_ENCODING_AGX3C, 8, 0, 1, 1,
       "4b002924"},
      {"c Ls #7 r6<-r2 slot3", AGX_ISA_ENCODING_AGX3C, 12, 4, 3, 0,
       "2300070a2682a41a"},
   };

   uint8_t bytes[AGX3_TEX_COPY_MAX_LENGTH];
   uint8_t length;
   for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
      CHECK(agx3_tex_stage_copy(v[i].enc, v[i].dest, v[i].src, v[i].slot,
                                v[i].wait_mask, bytes, &length));
      if (!hex_eq(bytes, length, v[i].hex)) {
         fprintf(stderr, "copy witness: %s\n", v[i].name);
         return 1;
      }
      checks++;
   }
   return 0;
}

static int
test_sample_fail_closed(void)
{
   const struct agx3_tex_sample_desc good = {
      .dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
      .lod = AGX3_TEX_LOD_AUTO, .slot = 1, .wait_mask = 1,
   };
   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t length;
   struct agx3_tex_sample_desc d;

   CHECK(agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3B, &good, bytes,
                                &length));
   CHECK(!agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX2, &good, bytes,
                                 &length));

   d = good;
   d.mask = 2; /* mask codes Apple never emitted stay closed */
   CHECK(!agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   d = good;
   d.sampler = 16;
   CHECK(!agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   d = good;
   d.lod_x64 = 64; /* AUTO carries no level operand */
   CHECK(!agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   d = good;
   d.lod = AGX3_TEX_LOD_LEVEL;
   d.lod_x64 = 0x1000;
   CHECK(!agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   d = good;
   d.coord = 2;
   CHECK(!agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));
   d = good;
   d.wait_mask = 3;
   CHECK(!agx3_tex_encode_sample(AGX_ISA_ENCODING_AGX3B, &d, bytes, &length));

   /* Stage-copy edges: odd registers, bad slots, wide waits. */
   CHECK(!agx3_tex_stage_copy(AGX_ISA_ENCODING_AGX3B, 1, 0, 1, 0, bytes,
                              &length));
   CHECK(!agx3_tex_stage_copy(AGX_ISA_ENCODING_AGX3B, 0, 3, 1, 0, bytes,
                              &length));
   CHECK(!agx3_tex_stage_copy(AGX_ISA_ENCODING_AGX3B, 0, 0, 0, 0, bytes,
                              &length));
   CHECK(!agx3_tex_stage_copy(AGX_ISA_ENCODING_AGX3B, 0, 0, 7, 0, bytes,
                              &length));
   CHECK(!agx3_tex_stage_copy(AGX_ISA_ENCODING_AGX3B, 0, 0, 1, 1u << 6,
                              bytes, &length));
   CHECK(!agx3_tex_stage_copy(AGX_ISA_ENCODING_AGX3C, 0, 0, 9, 0, bytes,
                              &length));
   CHECK(!agx3_tex_stage_copy(AGX_ISA_ENCODING_AGX3B, 0, 0, 1, 3, bytes,
                              &length));
   return 0;
}

/*
 * The 2D-array texel fetch. Both vectors are Apple's own bytes, from
 * `texture2d<float, access::read>::read()` and
 * `texture2d_array<float, access::read>::read()` compiled by
 * `metal-nt -arch applegpu_g16g`; they differ in the array selector alone.
 */
static int
test_array_fetch(void)
{
   static const uint8_t plain_2d[14] = {0x05, 0x80, 0x0c, 0xb8, 0x30, 0x01,
                                        0x17, 0x01, 0x00, 0x00, 0x00, 0x00,
                                        0x01, 0x00};
   static const uint8_t array_2d[14] = {0x05, 0x80, 0x0c, 0xb8, 0xb0, 0x00,
                                        0x97, 0x01, 0x00, 0x00, 0x00, 0x00,
                                        0x01, 0x00};

   const struct agx3_tex_fetch_desc plain = {
      .dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
      .texture = 0, .slot = 1, .wait_mask = 1u << 1, .coord_kill = true,
      .final = true, .array = false,
   };
   const struct agx3_tex_fetch_desc arr = {
      .dest = 0, .coord = 0, .width = AGX3_TEX_W32, .mask = 15,
      .texture = 0, .slot = 1, .wait_mask = 1u << 0, .coord_kill = true,
      .final = true, .array = true,
   };

   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t length = 0;

   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &plain, bytes,
                               &length));
   CHECK(length == 14);
   CHECK(memcmp(bytes, plain_2d, 14) == 0);

   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &arr, bytes, &length));
   CHECK(length == 14);
   CHECK(memcmp(bytes, array_2d, 14) == 0);

   /* The array flag survives a decode, and the two forms stay distinct. */
   struct agx3_tex_fetch_desc back;
   CHECK(agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, array_2d, 14, &back));
   CHECK(back.array);
   CHECK(same(&back, &arr));
   CHECK(agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, plain_2d, 14, &back));
   CHECK(!back.array);
   CHECK(same(&back, &plain));

   return 0;
}


/*
 * Bindless operand kinds. The texture and sampler each select one of three
 * consecutive fields by kind (95/96/97 and 124/125/126), and immediate vs
 * non-immediate also selects the encoding point: ep3 for immediate, ep4 for
 * register and uniform. All three kinds are witnessed byte-exactly from
 * Apple shaders captured on an A18 Pro; see test-agx3-smp-witness.c.
 */
static int
test_bindless_operand_kinds(void)
{
   struct agx3_tex_fetch_desc d;
   uint8_t out[AGX3_TEX_MAX_LENGTH];
   uint8_t len;

#define FETCH_BASE()                                                           \
   do {                                                                        \
      memset(&d, 0, sizeof(d));                                                \
      d.dest = 8;                                                              \
      d.coord = 16;                                                            \
      d.width = 1;                                                             \
      d.mask = 15;                                                             \
      d.slot = 1;                                                              \
      d.array = true;                                                          \
      len = 0;                                                                 \
   } while (0)

   /* Immediate operands keep the 14-byte ep3 form. A caller that never heard
    * of operand kinds leaves them zero and must encode exactly as before. */
   FETCH_BASE();
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &d, out, &len));
   CHECK(len == 14);

   /* Register descriptors: the ep4 form, 20 bytes. */
   FETCH_BASE();
   d.texture_kind = AGX3_TEX_OPERAND_REGISTER;
   d.sampler_kind = AGX3_TEX_OPERAND_REGISTER;
   d.texture_value = 0;
   d.sampler_value = 4;
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &d, out, &len));
   CHECK(len == 20);

   /* Uniform descriptors: also ep4. */
   FETCH_BASE();
   d.texture_kind = AGX3_TEX_OPERAND_UNIFORM;
   d.sampler_kind = AGX3_TEX_OPERAND_UNIFORM;
   d.texture_value = 16;
   d.sampler_value = 20;
   CHECK(agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &d, out, &len));
   CHECK(len == 20);

   /* Mixing an immediate sampler with a non-immediate texture is refused.
    * No Apple shader mixes them, and the composed table rejects the pairing,
    * so this fails closed rather than inventing a hybrid. Honeykrisp's meta
    * copy wants exactly this shape today, which is why it must materialise a
    * sampler descriptor rather than passing an immediate index. */
   FETCH_BASE();
   d.texture_kind = AGX3_TEX_OPERAND_REGISTER;
   d.sampler_kind = AGX3_TEX_OPERAND_IMMEDIATE;
   CHECK(!agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3B, &d, out, &len));

   /* Only AGX3b is witnessed for the non-immediate kinds. */
   FETCH_BASE();
   d.texture_kind = AGX3_TEX_OPERAND_REGISTER;
   d.sampler_kind = AGX3_TEX_OPERAND_REGISTER;
   CHECK(!agx3_tex_encode_fetch(AGX_ISA_ENCODING_AGX3C, &d, out, &len));

#undef FETCH_BASE
   return 0;
}

int
main(void)
{
   if (test_exact_witnesses() || test_supported_axes() || test_fail_closed() ||
       test_sample_witnesses() || test_copy_witnesses() ||
       test_sample_fail_closed() || test_array_fetch() ||
       test_bindless_operand_kinds())
      return 1;
   printf("agx3 texture core: %u checks passed\n", checks);
   return 0;
}
