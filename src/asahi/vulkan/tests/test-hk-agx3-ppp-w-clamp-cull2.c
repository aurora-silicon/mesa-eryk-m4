/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ppp_w_clamp_cull2.h"

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

static int
check_fields(const struct agx_ppp_w_clamp_cull2_fields *fields,
             uint32_t w_clamp_raw, uint32_t cull2_raw)
{
   CHECK(fields->w_clamp_raw == w_clamp_raw);
   CHECK(fields->cull2_raw == cull2_raw);
   return 0;
}

static int
check_zero_fields(const struct agx_ppp_w_clamp_cull2_fields *fields)
{
   return check_fields(fields, 0, 0);
}

static int
check_shared_byte_equality(void)
{
   static const uint32_t patterns[] = {
      0,
      UINT32_MAX,
      UINT32_C(0x80000000),
      UINT32_C(0x7fc00001),
      UINT32_C(0xdeadbeef),
      UINT32_C(0x01234567),
   };

   for (unsigned target = 0;
        target < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++target) {
      for (unsigned w = 0; w < sizeof(patterns) / sizeof(patterns[0]); ++w) {
         for (unsigned c = 0; c < sizeof(patterns) / sizeof(patterns[0]); ++c) {
            const struct agx_ppp_w_clamp_cull2_fields fields = {
               .w_clamp_raw = patterns[w],
               .cull2_raw = patterns[c],
            };
            struct agx_ppp_w_clamp_cull2_fields decoded;
            struct agx_ppp_w_clamp_cull2_core packed;
            struct agx_ppp_w_clamp_cull2_core repacked;

            CHECK(agx_ppp_w_clamp_cull2_core_pack(
               supported_chips[target], &fields, &packed));
            CHECK(hk_agx3_decode_ppp_w_clamp_cull2(
               supported_chips[target], packed.bytes, packed.length, &decoded));
            if (check_fields(&decoded, patterns[w], patterns[c]))
               return 1;
            CHECK(agx_ppp_w_clamp_cull2_core_pack(
               supported_chips[target], &decoded, &repacked));
            CHECK(memcmp(repacked.bytes, packed.bytes, packed.length) == 0);
         }
      }
   }

   return 0;
}

static int
check_every_bit_and_generation_boundary(void)
{
   const struct agx_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = UINT32_C(0x11223344),
      .cull2_raw = UINT32_C(0x55667788),
   };
   struct agx_ppp_w_clamp_cull2_core gen6;
   struct agx_ppp_w_clamp_cull2_core gen7;
   CHECK(agx_ppp_w_clamp_cull2_core_pack(AGX_CHIP_G15G, &fields, &gen6));
   CHECK(agx_ppp_w_clamp_cull2_core_pack(AGX_CHIP_G17S, &fields, &gen7));

   for (unsigned target = 0;
        target < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++target) {
      const struct agx_ppp_w_clamp_cull2_core *baseline =
         target < 8 ? &gen6 : &gen7;
      for (unsigned bit = 0; bit < baseline->length * 8u; ++bit) {
         uint8_t changed[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH];
         struct agx_ppp_w_clamp_cull2_fields decoded;
         memcpy(changed, baseline->bytes, sizeof(changed));
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

         if (bit < 32) {
            memset(&decoded, 0xa5, sizeof(decoded));
            CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(
               supported_chips[target], changed, sizeof(changed), &decoded));
            if (check_zero_fields(&decoded))
               return 1;
         } else {
            struct agx_ppp_w_clamp_cull2_core repacked;
            CHECK(hk_agx3_decode_ppp_w_clamp_cull2(
               supported_chips[target], changed, sizeof(changed), &decoded));
            CHECK(agx_ppp_w_clamp_cull2_core_pack(
               supported_chips[target], &decoded, &repacked));
            CHECK(memcmp(repacked.bytes, changed, sizeof(changed)) == 0);
         }
      }
   }

   for (unsigned old = 0; old < 8; ++old) {
      for (unsigned newer = 8;
           newer < sizeof(supported_chips) / sizeof(supported_chips[0]);
           ++newer) {
         struct agx_ppp_w_clamp_cull2_fields decoded;
         memset(&decoded, 0xa5, sizeof(decoded));
         CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(
            supported_chips[old], gen7.bytes, gen7.length, &decoded));
         if (check_zero_fields(&decoded))
            return 1;

         memset(&decoded, 0xa5, sizeof(decoded));
         CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(
            supported_chips[newer], gen6.bytes, gen6.length, &decoded));
         if (check_zero_fields(&decoded))
            return 1;
      }
   }

   return 0;
}

static int
check_rejections_and_unaligned(void)
{
   const struct agx_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = UINT32_C(0x11223344),
      .cull2_raw = UINT32_C(0x55667788),
   };
   struct agx_ppp_w_clamp_cull2_core gen6;
   struct agx_ppp_w_clamp_cull2_core gen7;
   CHECK(agx_ppp_w_clamp_cull2_core_pack(AGX_CHIP_G15G, &fields, &gen6));
   CHECK(agx_ppp_w_clamp_cull2_core_pack(AGX_CHIP_G17S, &fields, &gen7));

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct agx_ppp_w_clamp_cull2_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(
         unsupported_chips[i], gen6.bytes, gen6.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(
         unsupported_chips[i], gen7.bytes, gen7.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   for (size_t size = 0; size < gen7.length; ++size) {
      struct agx_ppp_w_clamp_cull2_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(AGX_CHIP_G17S, gen7.bytes, size,
                                              &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   const size_t trailing_sizes[] = {gen7.length + 1u, 64u, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      struct agx_ppp_w_clamp_cull2_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(
         AGX_CHIP_G17S, gen7.bytes, trailing_sizes[i], &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   uint8_t unaligned[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH + 1];
   memcpy(&unaligned[1], gen7.bytes, gen7.length);
   struct agx_ppp_w_clamp_cull2_fields decoded;
   CHECK(hk_agx3_decode_ppp_w_clamp_cull2(
      AGX_CHIP_G17S, &unaligned[1], gen7.length, &decoded));
   if (check_fields(&decoded, fields.w_clamp_raw, fields.cull2_raw))
      return 1;

   uint8_t appended[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH + 1];
   memcpy(appended, gen7.bytes, gen7.length);
   appended[gen7.length] = 0xa5;
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(
      AGX_CHIP_G17S, appended, sizeof(appended), &decoded));
   if (check_zero_fields(&decoded))
      return 1;

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(AGX_CHIP_G17S, NULL, gen7.length,
                                           &decoded));
   if (check_zero_fields(&decoded))
      return 1;
   CHECK(!hk_agx3_decode_ppp_w_clamp_cull2(
      AGX_CHIP_G17S, gen7.bytes, gen7.length, NULL));
   return 0;
}

int
main(void)
{
   if (check_shared_byte_equality() ||
       check_every_bit_and_generation_boundary() ||
       check_rejections_and_unaligned())
      return 1;

   printf("hk_agx3_ppp_w_clamp_cull2: %u checks passed\n", checks);
   return 0;
}
