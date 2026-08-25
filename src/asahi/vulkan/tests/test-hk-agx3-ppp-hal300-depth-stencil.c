/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ppp_hal300_depth_stencil.h"

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

static int
check_fields(const struct agx_ppp_hal300_depth_stencil_fields *actual,
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
check_zero_encoding(const struct agx_ppp_hal300_depth_stencil_core *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_shared_bridge(void)
{
   const struct agx_ppp_hal300_depth_stencil_fields fields = {
      .fragment_control_raw = UINT32_C(0x000007aa),
      .fragment_front_face_raw = UINT32_C(0xe2e00000),
      .fragment_back_face_raw = UINT32_C(0xf0c00000),
      .fragment_front_stencil_raw = UINT32_C(0x02000062),
      .fragment_back_stencil_raw = UINT32_C(0x00200200),
   };
   static const uint8_t expected[] = {
      0x01, 0x00, 0x00, 0x00, 0xaa, 0x07, 0x00, 0x00,
      0x00, 0x00, 0xe0, 0xe2, 0x00, 0x00, 0xc0, 0xf0,
      0x62, 0x00, 0x00, 0x02, 0x00, 0x02, 0x20, 0x00,
   };

   for (unsigned i = 0; i < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++i) {
      struct agx_ppp_hal300_depth_stencil_core direct;
      struct agx_ppp_hal300_depth_stencil_core bridged;
      struct agx_ppp_hal300_depth_stencil_fields decoded;
      CHECK(agx_ppp_hal300_depth_stencil_core_pack(supported_chips[i], &fields,
                                                   &direct));
      CHECK(hk_agx3_encode_ppp_hal300_depth_stencil(
         supported_chips[i], &fields, &bridged));
      CHECK(bridged.length == sizeof(expected));
      CHECK(memcmp(bridged.bytes, expected, sizeof(expected)) == 0);
      CHECK(memcmp(bridged.bytes, direct.bytes, direct.length) == 0);
      CHECK(hk_agx3_decode_ppp_hal300_depth_stencil(
         supported_chips[i], expected, sizeof(expected), &decoded));
      if (check_fields(&decoded, &fields))
         return 1;
   }
   return 0;
}

static int
check_every_payload_bit(void)
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
      CHECK(hk_agx3_encode_ppp_hal300_depth_stencil(
         supported_chips[target], &fields, &baseline));
      for (unsigned bit = 0; bit < baseline.length * 8u; ++bit) {
         uint8_t changed[AGX_PPP_HAL300_DEPTH_STENCIL_CORE_LENGTH];
         struct agx_ppp_hal300_depth_stencil_fields decoded;
         memcpy(changed, baseline.bytes, sizeof(changed));
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
         if (bit < 32) {
            memset(&decoded, 0xa5, sizeof(decoded));
            CHECK(!hk_agx3_decode_ppp_hal300_depth_stencil(
               supported_chips[target], changed, sizeof(changed), &decoded));
            if (check_zero_fields(&decoded))
               return 1;
         } else {
            struct agx_ppp_hal300_depth_stencil_core repacked;
            CHECK(hk_agx3_decode_ppp_hal300_depth_stencil(
               supported_chips[target], changed, sizeof(changed), &decoded));
            CHECK(hk_agx3_encode_ppp_hal300_depth_stencil(
               supported_chips[target], &decoded, &repacked));
            CHECK(memcmp(repacked.bytes, changed, sizeof(changed)) == 0);
         }
      }
   }
   return 0;
}

static int
check_transactional_rejections(void)
{
   const struct agx_ppp_hal300_depth_stencil_fields fields = {
      .fragment_control_raw = UINT32_C(0x11223344),
      .fragment_front_face_raw = UINT32_C(0x55667788),
      .fragment_back_face_raw = UINT32_C(0x99aabbcc),
      .fragment_front_stencil_raw = UINT32_C(0xddeeff00),
      .fragment_back_stencil_raw = UINT32_C(0x13579bdf),
   };
   struct agx_ppp_hal300_depth_stencil_core witness;
   struct agx_ppp_hal300_depth_stencil_core encoded;
   CHECK(hk_agx3_encode_ppp_hal300_depth_stencil(AGX_CHIP_G17C, &fields,
                                                 &witness));

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct agx_ppp_hal300_depth_stencil_fields decoded;
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!hk_agx3_encode_ppp_hal300_depth_stencil(
         unsupported_chips[i], &fields, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_hal300_depth_stencil(
         unsupported_chips[i], witness.bytes, witness.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!hk_agx3_encode_ppp_hal300_depth_stencil(AGX_CHIP_G17G, NULL,
                                                  &encoded));
   if (check_zero_encoding(&encoded))
      return 1;
   CHECK(!hk_agx3_encode_ppp_hal300_depth_stencil(AGX_CHIP_G17G, &fields,
                                                  NULL));

   for (size_t size = 0; size < witness.length; ++size) {
      struct agx_ppp_hal300_depth_stencil_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_hal300_depth_stencil(
         AGX_CHIP_G17C, witness.bytes, size, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   const size_t trailing_sizes[] = {witness.length + 1u, 64u, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      struct agx_ppp_hal300_depth_stencil_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_hal300_depth_stencil(
         AGX_CHIP_G17C, witness.bytes, trailing_sizes[i], &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   uint8_t unaligned[AGX_PPP_HAL300_DEPTH_STENCIL_CORE_LENGTH + 1];
   struct agx_ppp_hal300_depth_stencil_fields decoded;
   memcpy(&unaligned[1], witness.bytes, witness.length);
   CHECK(hk_agx3_decode_ppp_hal300_depth_stencil(
      AGX_CHIP_G17C, &unaligned[1], witness.length, &decoded));
   if (check_fields(&decoded, &fields))
      return 1;

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!hk_agx3_decode_ppp_hal300_depth_stencil(
      AGX_CHIP_G17C, NULL, witness.length, &decoded));
   if (check_zero_fields(&decoded))
      return 1;
   CHECK(!hk_agx3_decode_ppp_hal300_depth_stencil(
      AGX_CHIP_G17C, witness.bytes, witness.length, NULL));
   return 0;
}

int
main(void)
{
   if (check_exact_shared_bridge() || check_every_payload_bit() ||
       check_transactional_rejections())
      return 1;

   printf("hk_agx3_ppp_hal300_depth_stencil: %u checks passed\n", checks);
   return 0;
}
