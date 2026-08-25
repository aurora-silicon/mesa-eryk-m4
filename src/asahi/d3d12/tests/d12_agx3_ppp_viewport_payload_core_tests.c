/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_agx3_ppp_viewport_payload_core.h"

#include "asahi/lib/agx_ppp_viewport_payload_core.h"

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
d12_viewport_word(struct d12_agx3_ppp_viewport_transform_raw *viewport,
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

static const uint32_t *
d12_viewport_const_word(
   const struct d12_agx3_ppp_viewport_transform_raw *viewport, unsigned word)
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

static uint32_t *
shared_viewport_word(struct agx_ppp_viewport_transform_raw *viewport,
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
init_fields(struct d12_agx3_ppp_viewport_fields *d12,
            struct agx_ppp_viewport_payload_fields *shared, unsigned count)
{
   memset(d12, 0, sizeof(*d12));
   memset(shared, 0, sizeof(*shared));
   d12->viewport_count = (uint8_t)count;
   shared->viewport_count = (uint8_t)count;

   unsigned byte = 0;
   for (unsigned i = 0; i < count; ++i) {
      const uint64_t region = (uint64_t)sequence_word(byte) |
                              ((uint64_t)sequence_word(byte + 4) << 32);
      d12->region_clip_raw[i] = region;
      shared->region_clip_raw[i] = region;
      byte += 8;
   }

   d12->viewport_control_raw = sequence_word(byte);
   shared->viewport_control_raw = sequence_word(byte);
   byte += 4;

   for (unsigned i = 0; i < count; ++i) {
      for (unsigned w = 0; w < 6; ++w) {
         const uint32_t value = sequence_word(byte);
         *d12_viewport_word(&d12->viewports[i], w) = value;
         *shared_viewport_word(&shared->viewports[i], w) = value;
         byte += 4;
      }
   }
}

static int
check_fields_equal(const struct d12_agx3_ppp_viewport_fields *actual,
                   const struct d12_agx3_ppp_viewport_fields *expected)
{
   CHECK(actual->viewport_count == expected->viewport_count);
   CHECK(actual->viewport_control_raw == expected->viewport_control_raw);
   for (unsigned i = 0; i < expected->viewport_count; ++i) {
      CHECK(actual->region_clip_raw[i] == expected->region_clip_raw[i]);
      for (unsigned w = 0; w < 6; ++w)
         CHECK(*d12_viewport_const_word(&actual->viewports[i], w) ==
               *d12_viewport_const_word(&expected->viewports[i], w));
   }
   return 0;
}

static int
check_zero_fields(const struct d12_agx3_ppp_viewport_fields *fields)
{
   const uint8_t *raw = (const uint8_t *)fields;
   for (unsigned i = 0; i < sizeof(*fields); ++i)
      CHECK(raw[i] == 0);
   return 0;
}

static int
check_zero_encoding(const struct d12_agx3_ppp_viewport_payload *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_shared_equality(void)
{
   for (unsigned c = 0;
        c < sizeof(supported_chips) / sizeof(supported_chips[0]); ++c) {
      for (unsigned count = 1; count <= 16; ++count) {
         struct d12_agx3_ppp_viewport_fields fields;
         struct d12_agx3_ppp_viewport_fields decoded;
         struct agx_ppp_viewport_payload_fields shared_fields;
         struct d12_agx3_ppp_viewport_payload encoded;
         struct agx_ppp_viewport_payload_core shared;
         init_fields(&fields, &shared_fields, count);

         CHECK(d12_agx3_encode_ppp_viewport_payload(supported_chips[c], &fields,
                                                    &encoded));
         CHECK(agx_ppp_viewport_payload_core_pack(supported_chips[c],
                                                  &shared_fields, &shared));
         CHECK(encoded.length == 8u + (32u * count));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, shared.bytes, encoded.length) == 0);
         CHECK(d12_agx3_decode_ppp_viewport_payload(
            supported_chips[c], encoded.bytes, encoded.length, &decoded));
         if (check_fields_equal(&decoded, &fields))
            return 1;
      }
   }

   return 0;
}

static int
check_exact_generation_witnesses(void)
{
   struct d12_agx3_ppp_viewport_fields fields;
   struct agx_ppp_viewport_payload_fields shared_fields;
   struct d12_agx3_ppp_viewport_payload encoded;
   init_fields(&fields, &shared_fields, 2);

   uint8_t expected_gen6[72];
   uint8_t expected_gen7[72];
   memset(expected_gen6, 0, sizeof(expected_gen6));
   memset(expected_gen7, 0, sizeof(expected_gen7));
   expected_gen6[1] = 0x1c;
   expected_gen7[1] = 0x70;
   for (unsigned i = 4; i < sizeof(expected_gen6); ++i) {
      expected_gen6[i] = (uint8_t)(i - 4);
      expected_gen7[i] = (uint8_t)(i - 4);
   }

   CHECK(
      d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G15G, &fields, &encoded));
   CHECK(encoded.length == sizeof(expected_gen6));
   CHECK(memcmp(encoded.bytes, expected_gen6, sizeof(expected_gen6)) == 0);

   CHECK(
      d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, &fields, &encoded));
   CHECK(encoded.length == sizeof(expected_gen7));
   CHECK(memcmp(encoded.bytes, expected_gen7, sizeof(expected_gen7)) == 0);
   return 0;
}

static int
check_every_serialized_bit(void)
{
   struct d12_agx3_ppp_viewport_fields fields;
   struct agx_ppp_viewport_payload_fields shared_fields;
   struct d12_agx3_ppp_viewport_payload baseline;
   init_fields(&fields, &shared_fields, 16);
   CHECK(
      d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, &fields, &baseline));
   CHECK(baseline.length == D12_AGX3_PPP_VIEWPORT_MAX_LENGTH);

   for (unsigned bit = 0; bit < baseline.length * 8u; ++bit) {
      uint8_t changed[D12_AGX3_PPP_VIEWPORT_MAX_LENGTH];
      struct d12_agx3_ppp_viewport_fields decoded;
      memcpy(changed, baseline.bytes, baseline.length);
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

      if (bit < 32) {
         CHECK(!d12_agx3_decode_ppp_viewport_payload(
            AGX_CHIP_G17S, changed, baseline.length, &decoded));
         if (check_zero_fields(&decoded))
            return 1;
      } else {
         struct d12_agx3_ppp_viewport_payload repacked;
         CHECK(d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, changed,
                                                    baseline.length, &decoded));
         CHECK(d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, &decoded,
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
   struct d12_agx3_ppp_viewport_fields fields;
   struct d12_agx3_ppp_viewport_fields decoded;
   struct agx_ppp_viewport_payload_fields shared_fields;
   struct d12_agx3_ppp_viewport_payload gen6;
   struct d12_agx3_ppp_viewport_payload gen7;

   init_fields(&fields, &shared_fields, 1);
   CHECK(d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G15G, &fields, &gen6));
   CHECK(d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, &fields, &gen7));
   CHECK(gen6.length == 40);
   CHECK(gen7.length == gen6.length);
   CHECK(memcmp(gen6.bytes, gen7.bytes, gen6.length) != 0);
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, gen6.bytes,
                                               gen6.length, &decoded));
   if (check_zero_fields(&decoded))
      return 1;
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G15G, gen7.bytes,
                                               gen7.length, &decoded));
   if (check_zero_fields(&decoded))
      return 1;

   for (unsigned count = 2; count <= 16; ++count) {
      init_fields(&fields, &shared_fields, count);
      CHECK(
         d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G15G, &fields, &gen6));
      CHECK(
         d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, &fields, &gen7));

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, gen6.bytes,
                                                  gen6.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G15G, gen7.bytes,
                                                  gen7.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   struct d12_agx3_ppp_viewport_fields fields;
   struct agx_ppp_viewport_payload_fields shared_fields;
   struct d12_agx3_ppp_viewport_payload witness;
   struct d12_agx3_ppp_viewport_payload encoded;
   init_fields(&fields, &shared_fields, 2);
   CHECK(
      d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, &fields, &witness));

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct d12_agx3_ppp_viewport_fields decoded;
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!d12_agx3_encode_ppp_viewport_payload(unsupported_chips[i], &fields,
                                                  &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_viewport_payload(
         unsupported_chips[i], witness.bytes, witness.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   static const uint8_t invalid_counts[] = {0, 17, UINT8_MAX};
   for (unsigned i = 0; i < sizeof(invalid_counts) / sizeof(invalid_counts[0]);
        ++i) {
      fields.viewport_count = invalid_counts[i];
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, &fields,
                                                  &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }
   init_fields(&fields, &shared_fields, 2);

   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, NULL, &encoded));
   if (check_zero_encoding(&encoded))
      return 1;
   CHECK(!d12_agx3_encode_ppp_viewport_payload(AGX_CHIP_G17S, &fields, NULL));

   for (uint32_t size = 0; size < witness.length; ++size) {
      struct d12_agx3_ppp_viewport_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, witness.bytes,
                                                  size, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   const uint32_t trailing_sizes[] = {
      witness.length + 1u,
      D12_AGX3_PPP_VIEWPORT_MAX_LENGTH,
      UINT32_MAX,
   };
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      struct d12_agx3_ppp_viewport_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, witness.bytes,
                                                  trailing_sizes[i], &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   uint8_t unaligned[D12_AGX3_PPP_VIEWPORT_MAX_LENGTH + 1];
   memcpy(&unaligned[1], witness.bytes, witness.length);
   struct d12_agx3_ppp_viewport_fields decoded;
   CHECK(d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, &unaligned[1],
                                              witness.length, &decoded));
   if (check_fields_equal(&decoded, &fields))
      return 1;

   uint8_t appended[D12_AGX3_PPP_VIEWPORT_MAX_LENGTH + 1];
   memcpy(appended, witness.bytes, witness.length);
   appended[witness.length] = 0xa5;
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, appended,
                                               witness.length + 1u, &decoded));
   if (check_zero_fields(&decoded))
      return 1;

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, NULL,
                                               witness.length, &decoded));
   if (check_zero_fields(&decoded))
      return 1;
   CHECK(!d12_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, witness.bytes,
                                               witness.length, NULL));
   return 0;
}

int
main(void)
{
   if (check_exact_shared_equality() || check_exact_generation_witnesses() ||
       check_every_serialized_bit() || check_generation_boundary() ||
       check_rejections_and_clearing())
      return 1;

   printf("d12_agx3_ppp_viewport_payload_core: %u checks passed\n", checks);
   return 0;
}
