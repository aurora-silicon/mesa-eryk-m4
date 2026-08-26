/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_raytracing_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(condition)                                                       \
   do {                                                                        \
      ++checks;                                                                \
      if (!(condition)) {                                                      \
         fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,    \
                 #condition);                                                  \
         return 1;                                                             \
      }                                                                        \
   } while (0)

static const enum agx_chip supported_chips[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15D,
   AGX_CHIP_G16P, AGX_CHIP_G16G, AGX_CHIP_G16S,
   AGX_CHIP_G17P, AGX_CHIP_G17G, AGX_CHIP_G17S,
};

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
   AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
   AGX_CHIP_G17C, AGX_CHIP_UNKNOWN, (enum agx_chip)999,
};

static void
put_u32_le(uint8_t *raw, unsigned offset, uint32_t value)
{
   raw[offset + 0] = (uint8_t)value;
   raw[offset + 1] = (uint8_t)(value >> 8);
   raw[offset + 2] = (uint8_t)(value >> 16);
   raw[offset + 3] = (uint8_t)(value >> 24);
}

static void
put_f32_le(uint8_t *raw, unsigned offset, float value)
{
   uint32_t bits;
   memcpy(&bits, &value, sizeof(bits));
   put_u32_le(raw, offset, bits);
}

int
main(void)
{
   static const uint16_t registers_b[] = {0, 2, 4, 6, 8, 10};
   static const uint8_t bytes_b[][AGX3_CONTINUE_RAY_MAX_LENGTH] = {
      {0x5f, 0x11, 0x54, 0x00, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x02, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x04, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x06, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x08, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x0a, 0x00, 0x00},
   };
   static const uint16_t registers_c[] = {0, 2, 8, 10, 12, 16, 18};
   static const uint8_t bytes_c[][AGX3_CONTINUE_RAY_MAX_LENGTH] = {
      {0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x02},
      {0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x42},
      {0x27, 0x04, 0x09, 0x00, 0x32, 0xc0, 0xaf, 0x02},
      {0x27, 0x04, 0x09, 0x00, 0x32, 0xc0, 0xaf, 0x42},
      {0x27, 0x04, 0x09, 0x00, 0x3a, 0xc0, 0xaf, 0x02},
      {0x27, 0x04, 0x09, 0x00, 0xa2, 0xc0, 0xaf, 0x02},
      {0x27, 0x04, 0x09, 0x00, 0xa2, 0xc0, 0xaf, 0x42},
   };

   for (unsigned c = 0;
        c < sizeof(supported_chips) / sizeof(supported_chips[0]); ++c) {
      const enum agx_chip chip = supported_chips[c];
      const enum agx_isa_encoding encoding =
         agx3_continue_ray_witnessed_chip_encoding(chip);
      const uint16_t *registers = encoding == AGX_ISA_ENCODING_AGX3B
                                     ? registers_b : registers_c;
      const uint8_t (*bytes)[AGX3_CONTINUE_RAY_MAX_LENGTH] =
         encoding == AGX_ISA_ENCODING_AGX3B ? bytes_b : bytes_c;
      const unsigned count = encoding == AGX_ISA_ENCODING_AGX3B
                                ? sizeof(registers_b) / sizeof(registers_b[0])
                                : sizeof(registers_c) / sizeof(registers_c[0]);
      const uint8_t length = encoding == AGX_ISA_ENCODING_AGX3B ? 6 : 8;

      for (unsigned v = 0; v < count; ++v) {
         struct hk_agx3_continue_ray_instruction generated;
         memset(&generated, 0xff, sizeof(generated));
         CHECK(hk_agx3_continue_ray_instruction_generate(
            chip, registers[v], &generated));
         CHECK(generated.length == length);
         CHECK(memcmp(generated.bytes, bytes[v], length) == 0);
         for (unsigned i = length; i < sizeof(generated.bytes); ++i)
            CHECK(generated.bytes[i] == 0);

         uint16_t decoded = UINT16_MAX;
         CHECK(hk_agx3_continue_ray_instruction_decode(
            chip, generated.bytes, generated.length, &decoded));
         CHECK(decoded == registers[v]);
      }
   }

   for (unsigned c = 0;
        c < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++c) {
      struct hk_agx3_continue_ray_instruction rejected;
      memset(&rejected, 0xff, sizeof(rejected));
      CHECK(!hk_agx3_continue_ray_instruction_generate(
         unsupported_chips[c], 0, &rejected));
      CHECK(rejected.length == 0);
      for (unsigned i = 0; i < sizeof(rejected.bytes); ++i)
         CHECK(rejected.bytes[i] == 0);
      uint16_t decoded = 0;
      CHECK(!hk_agx3_continue_ray_instruction_decode(
         unsupported_chips[c], bytes_c[0], 8, &decoded));
   }

   struct hk_agx3_continue_ray_instruction rejected;
   memset(&rejected, 0xff, sizeof(rejected));
   CHECK(!hk_agx3_continue_ray_instruction_generate(AGX_CHIP_G15G, 12,
                                                     &rejected));
   CHECK(rejected.length == 0);
   memset(&rejected, 0xff, sizeof(rejected));
   CHECK(!hk_agx3_continue_ray_instruction_generate(AGX_CHIP_G17G, 4,
                                                     &rejected));
   CHECK(rejected.length == 0);

   uint16_t decoded = 0;
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G15G, bytes_c[0],
                                                   8, &decoded));
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, bytes_b[0],
                                                   6, &decoded));
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, bytes_c[0],
                                                   7, &decoded));
   uint8_t appended[AGX3_CONTINUE_RAY_MAX_LENGTH + 1];
   memcpy(appended, bytes_c[0], 8);
   appended[8] = 0;
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, appended,
                                                   sizeof(appended),
                                                   &decoded));

   uint8_t malformed[AGX3_CONTINUE_RAY_MAX_LENGTH];
   memcpy(malformed, bytes_c[2], sizeof(malformed));
   malformed[1] ^= 0x80;
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, malformed,
                                                   sizeof(malformed),
                                                   &decoded));
   CHECK(!hk_agx3_continue_ray_instruction_generate(AGX_CHIP_G17G, 0, NULL));
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, NULL, 8,
                                                   &decoded));
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, bytes_c[0],
                                                   8, NULL));

   {
      static const struct {
         enum agx_chip chip;
         uint16_t state_register;
         uint8_t bytes[AGX3_INTERSECT_RAY_MAX_LENGTH];
         uint8_t length;
         unsigned varying_bit;
      } vectors[] = {
         {AGX_CHIP_G16G, 0,
          {0x5f, 0x10, 0x54, 0x00, 0x00, 0x41,
           0x26, 0x40, 0xc7, 0x50, 0x4c, 0x00},
          12, 26},
         {AGX_CHIP_G16G, 4,
          {0x5f, 0x10, 0x54, 0x04, 0x00, 0x41,
           0x26, 0x40, 0xc7, 0x50, 0x4c, 0x00},
          12, 26},
         {AGX_CHIP_G17S, 0,
          {0x27, 0x04, 0x29, 0x01, 0xa2, 0xc1, 0xae,
           0x02, 0x01, 0x98, 0x27, 0x00, 0x00, 0x20},
          14, 105},
         {AGX_CHIP_G17S, 8,
          {0x27, 0x04, 0x29, 0x01, 0xa2, 0xc1, 0xae,
           0x02, 0x01, 0x98, 0x27, 0x00, 0x00, 0x22},
          14, 105},
      };

      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         struct hk_agx3_intersect_ray_instruction generated;
         memset(&generated, 0xff, sizeof(generated));
         CHECK(hk_agx3_intersect_ray_instruction_generate(
            vectors[v].chip, vectors[v].state_register, &generated));
         CHECK(generated.length == vectors[v].length);
         CHECK(memcmp(generated.bytes, vectors[v].bytes,
                      vectors[v].length) == 0);
         for (unsigned i = generated.length; i < sizeof(generated.bytes); ++i)
            CHECK(generated.bytes[i] == 0);

         uint16_t intersect_decoded = UINT16_MAX;
         CHECK(hk_agx3_intersect_ray_instruction_decode(
            vectors[v].chip, generated.bytes, generated.length,
            &intersect_decoded));
         CHECK(intersect_decoded == vectors[v].state_register);

         for (unsigned bit = 0; bit < vectors[v].length * 8; ++bit) {
            uint8_t changed[AGX3_INTERSECT_RAY_MAX_LENGTH] = {0};
            memcpy(changed, vectors[v].bytes, vectors[v].length);
            changed[bit / 8] ^= 1u << (bit % 8);
            const bool accepted = hk_agx3_intersect_ray_instruction_decode(
               vectors[v].chip, changed, vectors[v].length,
               &intersect_decoded);
            CHECK(accepted == (bit == vectors[v].varying_bit));
         }

         uint8_t intersect_appended[AGX3_INTERSECT_RAY_MAX_LENGTH + 1];
         memcpy(intersect_appended, vectors[v].bytes, vectors[v].length);
         intersect_appended[vectors[v].length] = 0;
         CHECK(!hk_agx3_intersect_ray_instruction_decode(
            vectors[v].chip, intersect_appended, vectors[v].length + 1,
            &intersect_decoded));
      }

      static const enum agx_chip intersect_supported_chips[] = {
         AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15D,
         AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G17P,
         AGX_CHIP_G17G, AGX_CHIP_G17S,
      };
      for (unsigned c = 0;
           c < sizeof(intersect_supported_chips) /
                  sizeof(intersect_supported_chips[0]);
           ++c) {
         const enum agx_chip chip = intersect_supported_chips[c];
         const enum agx_isa_encoding encoding =
            agx3_intersect_ray_witnessed_chip_encoding(chip);
         const uint16_t state_register =
            encoding == AGX_ISA_ENCODING_AGX3C ? 8 : 4;
         struct hk_agx3_intersect_ray_instruction generated;
         uint16_t intersect_decoded = UINT16_MAX;
         CHECK(hk_agx3_intersect_ray_instruction_generate(
            chip, state_register, &generated));
         CHECK(hk_agx3_intersect_ray_instruction_decode(
            chip, generated.bytes, generated.length, &intersect_decoded));
         CHECK(intersect_decoded == state_register);
      }

      static const enum agx_chip rejected_chips[] = {
         AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
         AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16P, AGX_CHIP_G16C,
         AGX_CHIP_G17A, AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
         (enum agx_chip)999,
      };
      for (unsigned c = 0;
           c < sizeof(rejected_chips) / sizeof(rejected_chips[0]); ++c) {
         struct hk_agx3_intersect_ray_instruction rejected_intersect;
         memset(&rejected_intersect, 0xff, sizeof(rejected_intersect));
         CHECK(!hk_agx3_intersect_ray_instruction_generate(
            rejected_chips[c], 0, &rejected_intersect));
         CHECK(rejected_intersect.length == 0);
         for (unsigned i = 0; i < sizeof(rejected_intersect.bytes); ++i)
            CHECK(rejected_intersect.bytes[i] == 0);
      }

      struct hk_agx3_intersect_ray_instruction rejected_intersect;
      memset(&rejected_intersect, 0xff, sizeof(rejected_intersect));
      CHECK(!hk_agx3_intersect_ray_instruction_generate(
         AGX_CHIP_G16G, 8, &rejected_intersect));
      CHECK(rejected_intersect.length == 0);
      memset(&rejected_intersect, 0xff, sizeof(rejected_intersect));
      CHECK(!hk_agx3_intersect_ray_instruction_generate(
         AGX_CHIP_G17S, 4, &rejected_intersect));
      CHECK(rejected_intersect.length == 0);

      uint16_t intersect_decoded = 0;
      CHECK(!hk_agx3_intersect_ray_instruction_decode(
         AGX_CHIP_G16G, vectors[2].bytes, vectors[2].length,
         &intersect_decoded));
      CHECK(!hk_agx3_intersect_ray_instruction_decode(
         AGX_CHIP_G17S, vectors[0].bytes, vectors[0].length,
         &intersect_decoded));
      CHECK(!hk_agx3_intersect_ray_instruction_generate(AGX_CHIP_G17S, 0,
                                                         NULL));
      CHECK(!hk_agx3_intersect_ray_instruction_decode(
         AGX_CHIP_G17S, NULL, vectors[2].length, &intersect_decoded));
      CHECK(!hk_agx3_intersect_ray_instruction_decode(
         AGX_CHIP_G17S, vectors[2].bytes, vectors[2].length, NULL));
   }

   {
      static const struct {
         enum agx_chip chip;
         uint8_t bytes[AGX3_INC_STK_FIXED_WITNESS_LENGTH];
      } vectors[] = {
         {AGX_CHIP_G16G,
          {0x07, 0x00, 0x54, 0x12, 0x80, 0x00, 0x00, 0x00}},
         {AGX_CHIP_G17S,
          {0x9f, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      };

      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         struct hk_agx3_inc_stk_fixed_witness generated;
         memset(&generated, 0xff, sizeof(generated));
         CHECK(hk_agx3_inc_stk_fixed_witness_generate(vectors[v].chip,
                                                       &generated));
         CHECK(generated.length == sizeof(vectors[v].bytes));
         CHECK(memcmp(generated.bytes, vectors[v].bytes,
                      sizeof(vectors[v].bytes)) == 0);
         CHECK(hk_agx3_inc_stk_fixed_witness_valid(
            vectors[v].chip, generated.bytes, generated.length));

         for (unsigned bit = 0; bit < sizeof(vectors[v].bytes) * 8; ++bit) {
            uint8_t changed[AGX3_INC_STK_FIXED_WITNESS_LENGTH];
            memcpy(changed, vectors[v].bytes, sizeof(changed));
            changed[bit / 8] ^= 1u << (bit % 8);
            CHECK(!hk_agx3_inc_stk_fixed_witness_valid(
               vectors[v].chip, changed, sizeof(changed)));
         }

         for (size_t size = 0; size < sizeof(vectors[v].bytes); ++size) {
            CHECK(!hk_agx3_inc_stk_fixed_witness_valid(
               vectors[v].chip, vectors[v].bytes, size));
         }

         uint8_t appended[AGX3_INC_STK_FIXED_WITNESS_LENGTH + 1];
         memcpy(appended, vectors[v].bytes, sizeof(vectors[v].bytes));
         appended[sizeof(vectors[v].bytes)] = 0;
         CHECK(!hk_agx3_inc_stk_fixed_witness_valid(
            vectors[v].chip, appended, sizeof(appended)));
      }

      CHECK(!hk_agx3_inc_stk_fixed_witness_valid(
         AGX_CHIP_G16G, vectors[1].bytes, sizeof(vectors[1].bytes)));
      CHECK(!hk_agx3_inc_stk_fixed_witness_valid(
         AGX_CHIP_G17S, vectors[0].bytes, sizeof(vectors[0].bytes)));

      static const enum agx_chip inc_stk_supported_chips[] = {
         AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15D,
         AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G17P,
         AGX_CHIP_G17G, AGX_CHIP_G17S,
      };
      for (unsigned c = 0;
           c < sizeof(inc_stk_supported_chips) /
                  sizeof(inc_stk_supported_chips[0]);
           ++c) {
         struct hk_agx3_inc_stk_fixed_witness generated;
         const enum agx_chip chip = inc_stk_supported_chips[c];
         CHECK(hk_agx3_inc_stk_fixed_witness_generate(chip, &generated));
         CHECK(hk_agx3_inc_stk_fixed_witness_valid(
            chip, generated.bytes, generated.length));
      }

      static const enum agx_chip inc_stk_rejected_chips[] = {
         AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
         AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16P, AGX_CHIP_G16C,
         AGX_CHIP_G17A, AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
         (enum agx_chip)999,
      };
      for (unsigned c = 0;
           c < sizeof(inc_stk_rejected_chips) /
                  sizeof(inc_stk_rejected_chips[0]);
           ++c) {
         struct hk_agx3_inc_stk_fixed_witness rejected_inc_stk;
         memset(&rejected_inc_stk, 0xff, sizeof(rejected_inc_stk));
         CHECK(!hk_agx3_inc_stk_fixed_witness_generate(
            inc_stk_rejected_chips[c], &rejected_inc_stk));
         CHECK(rejected_inc_stk.length == 0);
         for (unsigned i = 0; i < sizeof(rejected_inc_stk.bytes); ++i)
            CHECK(rejected_inc_stk.bytes[i] == 0);
      }

      CHECK(!hk_agx3_inc_stk_fixed_witness_generate(AGX_CHIP_G17S, NULL));
      CHECK(!hk_agx3_inc_stk_fixed_witness_valid(
         AGX_CHIP_G17S, NULL, AGX3_INC_STK_FIXED_WITNESS_LENGTH));
   }

   {
      struct hk_agx3_continue_ray_fixed_first_site_witness b;
      struct hk_agx3_continue_ray_fixed_first_site_witness c;
      CHECK(hk_agx3_continue_ray_fixed_first_site_witness_generate(
         AGX_CHIP_G16G, &b));
      CHECK(hk_agx3_continue_ray_fixed_first_site_witness_generate(
         AGX_CHIP_G17S, &c));
      CHECK(b.length == 6 && c.length == 8);
      CHECK(hk_agx3_continue_ray_fixed_first_site_witness_valid(
         AGX_CHIP_G16G, b.bytes, b.length));
      CHECK(hk_agx3_continue_ray_fixed_first_site_witness_valid(
         AGX_CHIP_G17S, c.bytes, c.length));
      CHECK(!hk_agx3_continue_ray_fixed_first_site_witness_valid(
         AGX_CHIP_G16G, c.bytes, c.length));
      CHECK(!hk_agx3_continue_ray_fixed_first_site_witness_valid(
         AGX_CHIP_G17S, b.bytes, b.length));

      for (unsigned i = 0;
           i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
         struct hk_agx3_continue_ray_fixed_first_site_witness witness;
         CHECK(hk_agx3_continue_ray_fixed_first_site_witness_generate(
            supported_chips[i], &witness));
         CHECK(hk_agx3_continue_ray_fixed_first_site_witness_valid(
            supported_chips[i], witness.bytes, witness.length));

         for (size_t size = 0; size < witness.length; ++size) {
            CHECK(!hk_agx3_continue_ray_fixed_first_site_witness_valid(
               supported_chips[i], witness.bytes, size));
         }
      }

      for (unsigned i = 0;
           i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]);
           ++i) {
         struct hk_agx3_continue_ray_fixed_first_site_witness rejected_fixed;
         memset(&rejected_fixed, 0xff, sizeof(rejected_fixed));
         CHECK(!hk_agx3_continue_ray_fixed_first_site_witness_generate(
            unsupported_chips[i], &rejected_fixed));
         CHECK(rejected_fixed.length == 0);
         for (unsigned j = 0; j < sizeof(rejected_fixed.bytes); ++j)
            CHECK(rejected_fixed.bytes[j] == 0);
      }

      for (unsigned bit = 0; bit < c.length * 8; ++bit) {
         uint8_t changed[AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH];
         memcpy(changed, c.bytes, c.length);
         changed[bit / 8] ^= 1u << (bit % 8);
         CHECK(!hk_agx3_continue_ray_fixed_first_site_witness_valid(
            AGX_CHIP_G17S, changed, c.length));
      }

      uint8_t appended[AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH +
                       1];
      memcpy(appended, c.bytes, c.length);
      appended[c.length] = 0;
      CHECK(!hk_agx3_continue_ray_fixed_first_site_witness_valid(
         AGX_CHIP_G17S, appended, c.length + 1));
      CHECK(!hk_agx3_continue_ray_fixed_first_site_witness_generate(
         AGX_CHIP_G17S, NULL));
      CHECK(!hk_agx3_continue_ray_fixed_first_site_witness_valid(
         AGX_CHIP_G17S, NULL, c.length));
   }

   {
      static const enum agx_chip scoped_chips[] = {
         AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15D,
         AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G17P,
         AGX_CHIP_G17G, AGX_CHIP_G17S,
      };
      static const enum agx_chip rejected_chips[] = {
         AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
         AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16P, AGX_CHIP_G16C,
         AGX_CHIP_G17A, AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
         (enum agx_chip)999,
      };

      struct hk_agx3_update_node_expansion b;
      struct hk_agx3_update_node_expansion c;
      CHECK(hk_agx3_update_node_expansion_generate(AGX_CHIP_G16G, &b));
      CHECK(hk_agx3_update_node_expansion_generate(AGX_CHIP_G17S, &c));
      CHECK(b.length == 10 && c.length == 14);
      CHECK(!hk_agx3_update_node_expansion_valid(AGX_CHIP_G16G, c.bytes,
                                                  c.length));
      CHECK(!hk_agx3_update_node_expansion_valid(AGX_CHIP_G17S, b.bytes,
                                                  b.length));

      for (unsigned i = 0;
           i < sizeof(scoped_chips) / sizeof(scoped_chips[0]); ++i) {
         struct hk_agx3_update_node_expansion expansion;
         CHECK(hk_agx3_update_node_expansion_generate(scoped_chips[i],
                                                       &expansion));
         CHECK(hk_agx3_update_node_expansion_valid(
            scoped_chips[i], expansion.bytes, expansion.length));
         for (unsigned bit = 0; bit < expansion.length * 8; ++bit) {
            uint8_t changed[AGX3_UPDATE_NODE_MAX_LENGTH];
            memcpy(changed, expansion.bytes, expansion.length);
            changed[bit / 8] ^= 1u << (bit % 8);
            CHECK(!hk_agx3_update_node_expansion_valid(
               scoped_chips[i], changed, expansion.length));
         }
         for (size_t size = 0; size < expansion.length; ++size) {
            CHECK(!hk_agx3_update_node_expansion_valid(
               scoped_chips[i], expansion.bytes, size));
         }
      }

      for (unsigned i = 0;
           i < sizeof(rejected_chips) / sizeof(rejected_chips[0]); ++i) {
         struct hk_agx3_update_node_expansion rejected;
         memset(&rejected, 0xff, sizeof(rejected));
         CHECK(!hk_agx3_update_node_expansion_generate(rejected_chips[i],
                                                        &rejected));
         CHECK(rejected.length == 0);
         for (unsigned j = 0; j < sizeof(rejected.bytes); ++j)
            CHECK(rejected.bytes[j] == 0);
      }

      CHECK(!hk_agx3_update_node_expansion_generate(AGX_CHIP_G17S, NULL));
      CHECK(!hk_agx3_update_node_expansion_valid(AGX_CHIP_G17S, NULL,
                                                  c.length));
   }

   {
      struct hk_agx3_update_bvh_fixed_witness witness;
      memset(&witness, 0xff, sizeof(witness));
      CHECK(hk_agx3_update_bvh_fixed_witness_generate(AGX_CHIP_G17S,
                                                       &witness));
      CHECK(witness.length == AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH);
      CHECK(hk_agx3_update_bvh_fixed_witness_valid(
         AGX_CHIP_G17S, witness.bytes, witness.length));

      for (unsigned bit = 0; bit < witness.length * 8; ++bit) {
         uint8_t changed[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH];
         memcpy(changed, witness.bytes, witness.length);
         changed[bit / 8] ^= 1u << (bit % 8);
         CHECK(!hk_agx3_update_bvh_fixed_witness_valid(
            AGX_CHIP_G17S, changed, witness.length));
      }
      for (size_t size = 0; size < witness.length; ++size) {
         CHECK(!hk_agx3_update_bvh_fixed_witness_valid(
            AGX_CHIP_G17S, witness.bytes, size));
      }

      const enum agx_chip siblings[] = {AGX_CHIP_G17G, AGX_CHIP_G17C,
                                        AGX_CHIP_G17P, AGX_CHIP_G16G};
      for (unsigned i = 0; i < sizeof(siblings) / sizeof(siblings[0]); ++i) {
         struct hk_agx3_update_bvh_fixed_witness rejected;
         memset(&rejected, 0xff, sizeof(rejected));
         CHECK(!hk_agx3_update_bvh_fixed_witness_generate(siblings[i],
                                                           &rejected));
         CHECK(rejected.length == 0);
         CHECK(!hk_agx3_update_bvh_fixed_witness_valid(
            siblings[i], witness.bytes, witness.length));
      }

      uint8_t appended[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH + 1];
      memcpy(appended, witness.bytes, witness.length);
      appended[witness.length] = 0;
      CHECK(!hk_agx3_update_bvh_fixed_witness_valid(
         AGX_CHIP_G17S, appended, sizeof(appended)));
      CHECK(!hk_agx3_update_bvh_fixed_witness_generate(AGX_CHIP_G17S,
                                                        NULL));
      CHECK(!hk_agx3_update_bvh_fixed_witness_valid(
         AGX_CHIP_G17S, NULL, witness.length));
   }

   {
      uint8_t raw[AGX_RT_HIT_RESULT_SIZE] = {0};
      put_u32_le(raw, AGX_RT_HIT_OFF_TYPE, 1);
      put_f32_le(raw, AGX_RT_HIT_OFF_T, 12.5f);
      put_u32_le(raw, AGX_RT_HIT_OFF_PRIMITIVE_ID, 7);
      put_u32_le(raw, AGX_RT_HIT_OFF_GEOMETRY_ID, 3);
      put_u32_le(raw, AGX_RT_HIT_OFF_PRIMITIVE_DATA, 0xdeadbeefu);
      put_u32_le(raw, AGX_RT_HIT_OFF_PRIMITIVE_DATA + 4, 0xcafeu);
      put_u32_le(raw, AGX_RT_HIT_OFF_INSTANCE_ID, 42);
      put_u32_le(raw, AGX_RT_HIT_OFF_USER_INSTANCE_ID, 99);
      raw[AGX_RT_HIT_OFF_INSTANCE_COUNT] = 2;
      put_f32_le(raw, AGX_RT_HIT_OFF_BARYCENTRICS, 0.25f);
      put_f32_le(raw, AGX_RT_HIT_OFF_BARYCENTRICS + 4, 0.5f);
      raw[AGX_RT_HIT_OFF_FRONT_FACE] = 1;
      put_f32_le(raw, AGX_RT_HIT_OFF_CURVE_PARAM, 0.75f);
      put_u32_le(raw, AGX_RT_HIT_OFF_INTERSECTION_FN_ID, 5);
      for (unsigned i = 0; i < AGX_RT_TRANSFORM_FLOATS; ++i) {
         put_f32_le(raw, AGX_RT_HIT_OFF_OBJECT_TO_WORLD + 4 * i, (float)i);
         put_f32_le(raw, AGX_RT_HIT_OFF_WORLD_TO_OBJECT + 4 * i,
                    (float)(100 + i));
      }

      struct agx_rt_hit_result hit;
      CHECK(hk_agx3_rt_hit_result_decode(raw, sizeof(raw), &hit));
      CHECK(hit.intersection_type == 1 && hit.t == 12.5f);
      CHECK(hit.primitive_id == 7 && hit.geometry_id == 3);
      CHECK(hit.primitive_data == UINT64_C(0x0000cafedeadbeef));
      CHECK(hit.instance_id == 42 && hit.user_instance_id == 99);
      CHECK(hit.instance_count == 2 && hit.front_face == 1);
      CHECK(hit.barycentrics[0] == 0.25f && hit.barycentrics[1] == 0.5f);
      CHECK(hit.curve_param == 0.75f && hit.intersection_function_id == 5);
      for (unsigned i = 0; i < AGX_RT_TRANSFORM_FLOATS; ++i) {
         CHECK(hit.object_to_world[i] == (float)i);
         CHECK(hit.world_to_object[i] == (float)(100 + i));
      }

      struct agx_rt_hit_result prefix;
      memset(&prefix, 0xa5, sizeof(prefix));
      CHECK(hk_agx3_rt_hit_result_decode(
         raw, AGX_RT_HIT_OFF_INTERSECTION_FN_ID + 4, &prefix));
      for (unsigned i = 0; i < AGX_RT_TRANSFORM_FLOATS; ++i) {
         CHECK(prefix.object_to_world[i] == 0.0f);
         CHECK(prefix.world_to_object[i] == 0.0f);
      }
      CHECK(!hk_agx3_rt_hit_result_decode(raw, 8, &hit));
      CHECK(!hk_agx3_rt_hit_result_decode(NULL, sizeof(raw), &hit));
      CHECK(!hk_agx3_rt_hit_result_decode(raw, sizeof(raw), NULL));

      uint8_t leaf[AGX_RT_INSTANCE_LEAF_SIZE] = {0};
      for (unsigned i = 0; i < AGX_RT_TRANSFORM_FLOATS; ++i)
         put_f32_le(leaf, AGX_RT_INSTANCE_LEAF_OFF_OBJECT_TO_WORLD + 4 * i,
                    (float)(20 + i));
      float transform[AGX_RT_TRANSFORM_FLOATS];
      CHECK(hk_agx3_rt_instance_leaf_object_to_world(
         leaf, AGX_RT_INSTANCE_LEAF_OFF_AABB, transform));
      for (unsigned i = 0; i < AGX_RT_TRANSFORM_FLOATS; ++i)
         CHECK(transform[i] == (float)(20 + i));
      CHECK(!hk_agx3_rt_instance_leaf_object_to_world(
         leaf, AGX_RT_INSTANCE_LEAF_OFF_AABB - 1, transform));
      CHECK(!hk_agx3_rt_instance_leaf_object_to_world(NULL, sizeof(leaf),
                                                       transform));
      CHECK(!hk_agx3_rt_instance_leaf_object_to_world(
         leaf, sizeof(leaf), NULL));
      CHECK(AGX_RT_RAY_QUERY_STATE_SIZE == 168);
   }

   printf("hk_agx3_raytracing_core: PASS (%u checks)\n", checks);
   return 0;
}
