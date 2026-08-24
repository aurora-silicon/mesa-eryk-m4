/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_ppp_viewport_payload_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      checks++;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                  \
         return 1;                                                             \
      }                                                                        \
   } while (0)

static const enum agx_chip supported_chips[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X,    AGX_CHIP_G14G,
   AGX_CHIP_G14X, AGX_CHIP_G15P,    AGX_CHIP_G16P,
   AGX_CHIP_G17A, AGX_CHIP_UNKNOWN, (enum agx_chip)999,
};

static uint32_t *
viewport_word(struct agx_ppp_viewport_transform_raw *viewport, unsigned word)
{
   switch (word) {
   case 0:
      return &viewport->translate_x;
   case 1:
      return &viewport->scale_x;
   case 2:
      return &viewport->translate_y;
   case 3:
      return &viewport->scale_y;
   case 4:
      return &viewport->translate_z;
   case 5:
      return &viewport->scale_z;
   default:
      return NULL;
   }
}

static const uint32_t *
viewport_const_word(const struct agx_ppp_viewport_transform_raw *viewport,
                    unsigned word)
{
   switch (word) {
   case 0:
      return &viewport->translate_x;
   case 1:
      return &viewport->scale_x;
   case 2:
      return &viewport->translate_y;
   case 3:
      return &viewport->scale_y;
   case 4:
      return &viewport->translate_z;
   case 5:
      return &viewport->scale_z;
   default:
      return NULL;
   }
}

static uint32_t
sequence_word(unsigned first_byte)
{
   return (uint32_t)first_byte | ((uint32_t)(first_byte + 1) << 8) |
          ((uint32_t)(first_byte + 2) << 16) |
          ((uint32_t)(first_byte + 3) << 24);
}

static void
init_sequence_fields(struct agx_ppp_viewport_payload_fields *fields,
                     unsigned count)
{
   memset(fields, 0, sizeof(*fields));
   fields->viewport_count = (uint8_t)count;

   unsigned byte = 0;
   for (unsigned i = 0; i < count; ++i) {
      fields->region_clip_raw[i] = (uint64_t)sequence_word(byte) |
                                   ((uint64_t)sequence_word(byte + 4) << 32);
      byte += 8;
   }

   fields->viewport_control_raw = sequence_word(byte);
   byte += 4;

   for (unsigned i = 0; i < count; ++i) {
      for (unsigned w = 0; w < 6; ++w) {
         *viewport_word(&fields->viewports[i], w) = sequence_word(byte);
         byte += 4;
      }
   }
}

static int
check_fields_equal(const struct agx_ppp_viewport_payload_fields *actual,
                   const struct agx_ppp_viewport_payload_fields *expected)
{
   CHECK(actual->viewport_count == expected->viewport_count);
   CHECK(actual->viewport_control_raw == expected->viewport_control_raw);

   for (unsigned i = 0; i < expected->viewport_count; ++i) {
      CHECK(actual->region_clip_raw[i] == expected->region_clip_raw[i]);
      for (unsigned w = 0; w < 6; ++w)
         CHECK(*viewport_const_word(&actual->viewports[i], w) ==
               *viewport_const_word(&expected->viewports[i], w));
   }

   return 0;
}

static int
check_zero_fields(const struct agx_ppp_viewport_payload_fields *fields)
{
   const uint8_t *raw = (const uint8_t *)fields;
   for (unsigned i = 0; i < sizeof(*fields); ++i)
      CHECK(raw[i] == 0);
   return 0;
}

static int
check_zero_encoding(const struct agx_ppp_viewport_payload_core *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_witnesses(void)
{
   struct agx_ppp_viewport_payload_fields fields;
   init_sequence_fields(&fields, 2);

   uint8_t expected_gen6[72];
   uint8_t expected_gen7[72];
   memset(expected_gen6, 0, sizeof(expected_gen6));
   memset(expected_gen7, 0, sizeof(expected_gen7));
   expected_gen6[1] = 0x1c;
   expected_gen7[1] = 0x4c;
   for (unsigned i = 4; i < sizeof(expected_gen6); ++i) {
      expected_gen6[i] = (uint8_t)(i - 4);
      expected_gen7[i] = (uint8_t)(i - 4);
   }

   for (unsigned c = 0;
        c < sizeof(supported_chips) / sizeof(supported_chips[0]); ++c) {
      struct agx_ppp_viewport_payload_core encoded;
      struct agx_ppp_viewport_payload_fields decoded;
      const bool gen7 = supported_chips[c] == AGX_CHIP_G17G ||
                        supported_chips[c] == AGX_CHIP_G17S ||
                        supported_chips[c] == AGX_CHIP_G17C;
      const uint8_t *expected = gen7 ? expected_gen7 : expected_gen6;

      CHECK(agx_ppp_viewport_payload_core_supported(supported_chips[c]));
      CHECK(agx_ppp_viewport_payload_core_pack(supported_chips[c], &fields,
                                               &encoded));
      CHECK(encoded.length == sizeof(expected_gen6));
      CHECK(memcmp(encoded.bytes, expected, encoded.length) == 0);
      CHECK(agx_ppp_viewport_payload_core_unpack(
         supported_chips[c], expected, sizeof(expected_gen6), &decoded));
      if (check_fields_equal(&decoded, &fields))
         return 1;
   }

   return 0;
}

static int
check_all_counts_and_targets(void)
{
   for (unsigned c = 0;
        c < sizeof(supported_chips) / sizeof(supported_chips[0]); ++c) {
      const unsigned shift = c < 8 ? 12 : 14;

      for (unsigned count = 1; count <= 16; ++count) {
         struct agx_ppp_viewport_payload_fields fields;
         struct agx_ppp_viewport_payload_fields decoded;
         struct agx_ppp_viewport_payload_core encoded;
         init_sequence_fields(&fields, count);

         CHECK(agx_ppp_viewport_payload_core_pack(supported_chips[c], &fields,
                                                  &encoded));
         CHECK(encoded.length == 8u + (32u * count));
         const uint32_t expected_header =
            AGX_PPP_VIEWPORT_PAYLOAD_BASE_HEADER | ((count - 1) << shift);
         CHECK(encoded.bytes[0] == (uint8_t)expected_header);
         CHECK(encoded.bytes[1] == (uint8_t)(expected_header >> 8));
         CHECK(encoded.bytes[2] == (uint8_t)(expected_header >> 16));
         CHECK(encoded.bytes[3] == (uint8_t)(expected_header >> 24));
         CHECK(agx_ppp_viewport_payload_core_unpack(
            supported_chips[c], encoded.bytes, encoded.length, &decoded));
         if (check_fields_equal(&decoded, &fields))
            return 1;
      }
   }

   return 0;
}

static int
check_every_serialized_bit(void)
{
   struct agx_ppp_viewport_payload_fields fields;
   struct agx_ppp_viewport_payload_core baseline;
   init_sequence_fields(&fields, 16);
   CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, &baseline));
   CHECK(baseline.length == AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH);

   for (unsigned bit = 0; bit < baseline.length * 8u; ++bit) {
      uint8_t changed[AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH];
      struct agx_ppp_viewport_payload_fields decoded;
      memcpy(changed, baseline.bytes, baseline.length);
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

      if (bit < 32) {
         CHECK(!agx_ppp_viewport_payload_core_unpack(
            AGX_CHIP_G17S, changed, baseline.length, &decoded));
         if (check_zero_fields(&decoded))
            return 1;
      } else {
         struct agx_ppp_viewport_payload_core repacked;
         CHECK(agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, changed,
                                                    baseline.length, &decoded));
         CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &decoded,
                                                  &repacked));
         CHECK(repacked.length == baseline.length);
         CHECK(memcmp(changed, repacked.bytes, baseline.length) == 0);
      }
   }

   return 0;
}

static int
check_generation_boundary(void)
{
   struct agx_ppp_viewport_payload_fields fields;
   struct agx_ppp_viewport_payload_core gen6;
   struct agx_ppp_viewport_payload_core gen7;
   struct agx_ppp_viewport_payload_fields decoded;

   init_sequence_fields(&fields, 1);
   CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G15G, &fields, &gen6));
   CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, &gen7));
   CHECK(gen6.length == AGX_PPP_VIEWPORT_PAYLOAD_MIN_LENGTH);
   CHECK(gen7.length == gen6.length);
   CHECK(memcmp(gen6.bytes, gen7.bytes, gen6.length) == 0);
   CHECK(agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G15G, gen7.bytes,
                                              gen7.length, &decoded));
   if (check_fields_equal(&decoded, &fields))
      return 1;
   CHECK(agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, gen6.bytes,
                                              gen6.length, &decoded));
   if (check_fields_equal(&decoded, &fields))
      return 1;

   /* N=1 has a zero count field and is truthfully indistinguishable.  Every
    * other count is rejected cross-generation by the exact derived length. */
   for (unsigned count = 2; count <= 16; ++count) {
      init_sequence_fields(&fields, count);
      CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G15G, &fields, &gen6));
      CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, &gen7));

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, gen6.bytes,
                                                  gen6.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G15G, gen7.bytes,
                                                  gen7.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   struct agx_ppp_viewport_payload_fields fields;
   struct agx_ppp_viewport_payload_core witness;
   struct agx_ppp_viewport_payload_core encoded;
   init_sequence_fields(&fields, 2);
   CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, &witness));

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct agx_ppp_viewport_payload_fields decoded;

      CHECK(!agx_ppp_viewport_payload_core_supported(unsupported_chips[i]));
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_ppp_viewport_payload_core_pack(unsupported_chips[i], &fields,
                                                &encoded));
      if (check_zero_encoding(&encoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_viewport_payload_core_unpack(
         unsupported_chips[i], witness.bytes, witness.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   static const uint8_t invalid_counts[] = {0, 17, UINT8_MAX};
   for (unsigned i = 0; i < sizeof(invalid_counts) / sizeof(invalid_counts[0]);
        ++i) {
      fields.viewport_count = invalid_counts[i];
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(
         !agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }
   init_sequence_fields(&fields, 2);

   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, NULL, &encoded));
   if (check_zero_encoding(&encoded))
      return 1;
   CHECK(!agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, NULL));

   for (size_t size = 0; size < witness.length; ++size) {
      struct agx_ppp_viewport_payload_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, witness.bytes,
                                                  size, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   const size_t trailing_sizes[] = {
      witness.length + 1u,
      AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH,
      SIZE_MAX,
   };
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      struct agx_ppp_viewport_payload_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, witness.bytes,
                                                  trailing_sizes[i], &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   uint8_t unaligned[AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH + 1];
   memcpy(&unaligned[1], witness.bytes, witness.length);
   struct agx_ppp_viewport_payload_fields decoded;
   CHECK(agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, &unaligned[1],
                                              witness.length, &decoded));
   if (check_fields_equal(&decoded, &fields))
      return 1;

   uint8_t appended[AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH + 1];
   memcpy(appended, witness.bytes, witness.length);
   appended[witness.length] = 0xa5;
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, appended,
                                               witness.length + 1u, &decoded));
   if (check_zero_fields(&decoded))
      return 1;

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, NULL,
                                               witness.length, &decoded));
   if (check_zero_fields(&decoded))
      return 1;
   CHECK(!agx_ppp_viewport_payload_core_unpack(AGX_CHIP_G17S, witness.bytes,
                                               witness.length, NULL));

   return 0;
}

int
main(void)
{
   if (check_exact_witnesses() || check_all_counts_and_targets() ||
       check_every_serialized_bit() || check_generation_boundary() ||
       check_rejections_and_clearing())
      return 1;

   printf("agx_ppp_viewport_payload_core: %u checks passed\n", checks);
   return 0;
}
