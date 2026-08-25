/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_ppp_hal300_depth_stencil_core.h"

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
   AGX_CHIP_G17G,
   AGX_CHIP_G17S,
   AGX_CHIP_G17C,
};

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
   AGX_CHIP_G15P, AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C,
   AGX_CHIP_G15D, AGX_CHIP_G16P, AGX_CHIP_G16G, AGX_CHIP_G16S,
   AGX_CHIP_G16C, AGX_CHIP_G17P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   (enum agx_chip)999,
};

static void
store_le32(uint8_t *dst, uint32_t value)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static void
make_witness(
   const struct agx_ppp_hal300_depth_stencil_fields *fields,
   uint8_t bytes[AGX_PPP_HAL300_DEPTH_STENCIL_CORE_LENGTH])
{
   store_le32(&bytes[0], AGX_PPP_HAL300_DEPTH_STENCIL_HEADER);
   store_le32(&bytes[4], fields->fragment_control_raw);
   store_le32(&bytes[8], fields->fragment_front_face_raw);
   store_le32(&bytes[12], fields->fragment_back_face_raw);
   store_le32(&bytes[16], fields->fragment_front_stencil_raw);
   store_le32(&bytes[20], fields->fragment_back_stencil_raw);
}

static int
check_fields(
   const struct agx_ppp_hal300_depth_stencil_fields *actual,
   const struct agx_ppp_hal300_depth_stencil_fields *expected)
{
   CHECK(actual->fragment_control_raw == expected->fragment_control_raw);
   CHECK(actual->fragment_front_face_raw == expected->fragment_front_face_raw);
   CHECK(actual->fragment_back_face_raw == expected->fragment_back_face_raw);
   CHECK(actual->fragment_front_stencil_raw ==
         expected->fragment_front_stencil_raw);
   CHECK(actual->fragment_back_stencil_raw ==
         expected->fragment_back_stencil_raw);
   return 0;
}

static int
check_zero_fields(const struct agx_ppp_hal300_depth_stencil_fields *fields)
{
   const struct agx_ppp_hal300_depth_stencil_fields zero = {0};
   return check_fields(fields, &zero);
}

static int
check_zero_encoding(
   const struct agx_ppp_hal300_depth_stencil_core *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_plugin_witness(void)
{
   const struct agx_ppp_hal300_depth_stencil_fields fields = {
      .fragment_control_raw = UINT32_C(0x000007aa),
      .fragment_front_face_raw = UINT32_C(0xe2e00000),
      .fragment_back_face_raw = UINT32_C(0xf0c00000),
      .fragment_front_stencil_raw = UINT32_C(0x02000062),
      .fragment_back_stencil_raw = UINT32_C(0x00200200),
   };
   const uint8_t expected[] = {
      0x01, 0x00, 0x00, 0x00, 0xaa, 0x07, 0x00, 0x00,
      0x00, 0x00, 0xe0, 0xe2, 0x00, 0x00, 0xc0, 0xf0,
      0x62, 0x00, 0x00, 0x02, 0x00, 0x02, 0x20, 0x00,
   };

   for (unsigned i = 0; i < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++i) {
      struct agx_ppp_hal300_depth_stencil_core encoded;
      struct agx_ppp_hal300_depth_stencil_fields decoded;

      CHECK(agx_ppp_hal300_depth_stencil_core_supported(supported_chips[i]));
      CHECK(agx_ppp_hal300_depth_stencil_core_pack(supported_chips[i], &fields,
                                                   &encoded));
      CHECK(encoded.length == sizeof(expected));
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(agx_ppp_hal300_depth_stencil_core_unpack(
         supported_chips[i], expected, sizeof(expected), &decoded));
      if (check_fields(&decoded, &fields))
         return 1;
   }

   return 0;
}

static int
check_raw_payload_cross_product(void)
{
   static const uint32_t patterns[] = {
      0,
      UINT32_MAX,
      UINT32_C(0x80000000),
      UINT32_C(0x7fc00001),
      UINT32_C(0xdeadbeef),
   };

   for (unsigned target = 0;
        target < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++target) {
      for (unsigned a = 0; a < sizeof(patterns) / sizeof(patterns[0]); ++a) {
         for (unsigned b = 0; b < sizeof(patterns) / sizeof(patterns[0]); ++b) {
            for (unsigned c = 0; c < sizeof(patterns) / sizeof(patterns[0]);
                 ++c) {
               for (unsigned d = 0;
                    d < sizeof(patterns) / sizeof(patterns[0]); ++d) {
                  for (unsigned e = 0;
                       e < sizeof(patterns) / sizeof(patterns[0]); ++e) {
                     const struct agx_ppp_hal300_depth_stencil_fields fields = {
                        .fragment_control_raw = patterns[a],
                        .fragment_front_face_raw = patterns[b],
                        .fragment_back_face_raw = patterns[c],
                        .fragment_front_stencil_raw = patterns[d],
                        .fragment_back_stencil_raw = patterns[e],
                     };
                     struct agx_ppp_hal300_depth_stencil_fields decoded;
                     struct agx_ppp_hal300_depth_stencil_core encoded;
                     struct agx_ppp_hal300_depth_stencil_core repacked;

                     CHECK(agx_ppp_hal300_depth_stencil_core_pack(
                        supported_chips[target], &fields, &encoded));
                     CHECK(agx_ppp_hal300_depth_stencil_core_unpack(
                        supported_chips[target], encoded.bytes, encoded.length,
                        &decoded));
                     if (check_fields(&decoded, &fields))
                        return 1;
                     CHECK(agx_ppp_hal300_depth_stencil_core_pack(
                        supported_chips[target], &decoded, &repacked));
                     CHECK(repacked.length == encoded.length);
                     CHECK(memcmp(repacked.bytes, encoded.bytes,
                                  encoded.length) == 0);
                  }
               }
            }
         }
      }
   }

   return 0;
}

static int
check_every_serialized_bit(void)
{
   const struct agx_ppp_hal300_depth_stencil_fields fields = {
      .fragment_control_raw = UINT32_C(0x01234567),
      .fragment_front_face_raw = UINT32_C(0x89abcdef),
      .fragment_back_face_raw = UINT32_C(0x13579bdf),
      .fragment_front_stencil_raw = UINT32_C(0x2468ace0),
      .fragment_back_stencil_raw = UINT32_C(0xa55aa55a),
   };

   for (unsigned target = 0;
        target < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++target) {
      struct agx_ppp_hal300_depth_stencil_core baseline;
      CHECK(agx_ppp_hal300_depth_stencil_core_pack(
         supported_chips[target], &fields, &baseline));

      for (unsigned bit = 0; bit < baseline.length * 8u; ++bit) {
         uint8_t changed[AGX_PPP_HAL300_DEPTH_STENCIL_CORE_LENGTH];
         struct agx_ppp_hal300_depth_stencil_fields decoded;
         memcpy(changed, baseline.bytes, sizeof(changed));
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

         if (bit < 32) {
            memset(&decoded, 0xa5, sizeof(decoded));
            CHECK(!agx_ppp_hal300_depth_stencil_core_unpack(
               supported_chips[target], changed, sizeof(changed), &decoded));
            if (check_zero_fields(&decoded))
               return 1;
         } else {
            struct agx_ppp_hal300_depth_stencil_core repacked;
            CHECK(agx_ppp_hal300_depth_stencil_core_unpack(
               supported_chips[target], changed, sizeof(changed), &decoded));
            CHECK(agx_ppp_hal300_depth_stencil_core_pack(
               supported_chips[target], &decoded, &repacked));
            CHECK(memcmp(repacked.bytes, changed, sizeof(changed)) == 0);
         }
      }
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   const struct agx_ppp_hal300_depth_stencil_fields fields = {
      .fragment_control_raw = UINT32_C(0x01234567),
      .fragment_front_face_raw = UINT32_C(0x89abcdef),
      .fragment_back_face_raw = UINT32_C(0x13579bdf),
      .fragment_front_stencil_raw = UINT32_C(0x2468ace0),
      .fragment_back_stencil_raw = UINT32_C(0xa55aa55a),
   };
   uint8_t witness[AGX_PPP_HAL300_DEPTH_STENCIL_CORE_LENGTH];
   struct agx_ppp_hal300_depth_stencil_core encoded;
   make_witness(&fields, witness);

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct agx_ppp_hal300_depth_stencil_fields decoded;
      CHECK(!agx_ppp_hal300_depth_stencil_core_supported(unsupported_chips[i]));

      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_ppp_hal300_depth_stencil_core_pack(unsupported_chips[i],
                                                    &fields, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_hal300_depth_stencil_core_unpack(
         unsupported_chips[i], witness, sizeof(witness), &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!agx_ppp_hal300_depth_stencil_core_pack(AGX_CHIP_G17G, NULL,
                                                 &encoded));
   if (check_zero_encoding(&encoded))
      return 1;
   CHECK(!agx_ppp_hal300_depth_stencil_core_pack(AGX_CHIP_G17G, &fields,
                                                 NULL));

   for (size_t size = 0; size < sizeof(witness); ++size) {
      struct agx_ppp_hal300_depth_stencil_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_hal300_depth_stencil_core_unpack(
         AGX_CHIP_G17G, witness, size, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   const size_t trailing_sizes[] = {sizeof(witness) + 1u, 64u, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      struct agx_ppp_hal300_depth_stencil_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_hal300_depth_stencil_core_unpack(
         AGX_CHIP_G17G, witness, trailing_sizes[i], &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   uint8_t unaligned[AGX_PPP_HAL300_DEPTH_STENCIL_CORE_LENGTH + 1];
   memcpy(&unaligned[1], witness, sizeof(witness));
   struct agx_ppp_hal300_depth_stencil_fields decoded;
   CHECK(agx_ppp_hal300_depth_stencil_core_unpack(
      AGX_CHIP_G17C, &unaligned[1], sizeof(witness), &decoded));
   if (check_fields(&decoded, &fields))
      return 1;

   uint8_t appended[AGX_PPP_HAL300_DEPTH_STENCIL_CORE_LENGTH + 1];
   memcpy(appended, witness, sizeof(witness));
   appended[sizeof(witness)] = 0xa5;
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!agx_ppp_hal300_depth_stencil_core_unpack(
      AGX_CHIP_G17S, appended, sizeof(appended), &decoded));
   if (check_zero_fields(&decoded))
      return 1;

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!agx_ppp_hal300_depth_stencil_core_unpack(
      AGX_CHIP_G17S, NULL, sizeof(witness), &decoded));
   if (check_zero_fields(&decoded))
      return 1;
   CHECK(!agx_ppp_hal300_depth_stencil_core_unpack(
      AGX_CHIP_G17S, witness, sizeof(witness), NULL));
   return 0;
}

int
main(void)
{
   if (check_exact_plugin_witness() || check_raw_payload_cross_product() ||
       check_every_serialized_bit() || check_rejections_and_clearing())
      return 1;

   printf("agx_ppp_hal300_depth_stencil_core: %u checks passed\n", checks);
   return 0;
}
