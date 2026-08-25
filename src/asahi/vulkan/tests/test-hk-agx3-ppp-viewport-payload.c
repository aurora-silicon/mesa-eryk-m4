/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ppp_viewport_payload.h"

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

static void
init_fields(struct agx_ppp_viewport_payload_fields *fields, unsigned count,
            unsigned salt)
{
   memset(fields, 0, sizeof(*fields));
   fields->viewport_count = (uint8_t)count;
   fields->viewport_control_raw = UINT32_C(0x91a2b3c4) ^ salt;

   for (unsigned i = 0; i < count; ++i) {
      fields->region_clip_raw[i] =
         UINT64_C(0x1020304050607080) ^ ((uint64_t)salt << 32) ^ i;
      for (unsigned word = 0; word < 6; ++word) {
         *viewport_word(&fields->viewports[i], word) =
            UINT32_C(0x3f000000) ^ (salt << 16) ^ (i << 8) ^ word;
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
      for (unsigned word = 0; word < 6; ++word) {
         CHECK(*viewport_const_word(&actual->viewports[i], word) ==
               *viewport_const_word(&expected->viewports[i], word));
      }
   }

   return 0;
}

static int
check_fields_cleared(const struct agx_ppp_viewport_payload_fields *fields)
{
   const uint8_t *bytes = (const uint8_t *)fields;
   for (size_t i = 0; i < sizeof(*fields); ++i)
      CHECK(bytes[i] == 0);
   return 0;
}

static int
check_all_counts_targets_and_shared_bytes(void)
{
   for (unsigned target = 0;
        target < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++target) {
      for (unsigned count = 1; count <= AGX_PPP_VIEWPORT_PAYLOAD_MAX_VIEWPORTS;
           ++count) {
         struct agx_ppp_viewport_payload_fields fields;
         struct agx_ppp_viewport_payload_fields decoded;
         struct agx_ppp_viewport_payload_core shared;
         struct agx_ppp_viewport_payload_core repacked;
         init_fields(&fields, count, target);

         CHECK(agx_ppp_viewport_payload_core_pack(supported_chips[target],
                                                  &fields, &shared));
         CHECK(hk_agx3_decode_ppp_viewport_payload(
            supported_chips[target], shared.bytes, shared.length, &decoded));
         if (check_fields_equal(&decoded, &fields))
            return 1;

         CHECK(agx_ppp_viewport_payload_core_pack(supported_chips[target],
                                                  &decoded, &repacked));
         CHECK(repacked.length == shared.length);
         CHECK(memcmp(repacked.bytes, shared.bytes, shared.length) == 0);
      }
   }

   return 0;
}

static int
check_generation_boundaries(void)
{
   struct agx_ppp_viewport_payload_fields fields;
   struct agx_ppp_viewport_payload_fields decoded;
   struct agx_ppp_viewport_payload_core gen6;
   struct agx_ppp_viewport_payload_core gen7;

   init_fields(&fields, 1, 0);
   CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G15G, &fields, &gen6));
   CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, &gen7));
   CHECK(gen6.length == AGX_PPP_VIEWPORT_PAYLOAD_MIN_LENGTH);
   CHECK(gen6.length == gen7.length);
   CHECK(memcmp(gen6.bytes, gen7.bytes, gen6.length) != 0);

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G15G, gen7.bytes,
                                              gen7.length, &decoded));
   if (check_fields_cleared(&decoded))
      return 1;
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, gen6.bytes,
                                              gen6.length, &decoded));
   if (check_fields_cleared(&decoded))
      return 1;

   for (unsigned count = 2; count <= AGX_PPP_VIEWPORT_PAYLOAD_MAX_VIEWPORTS;
        ++count) {
      init_fields(&fields, count, 0);
      CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G15G, &fields, &gen6));
      CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, &gen7));

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, gen6.bytes,
                                                 gen6.length, &decoded));
      if (check_fields_cleared(&decoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G15G, gen7.bytes,
                                                 gen7.length, &decoded));
      if (check_fields_cleared(&decoded))
         return 1;
   }

   return 0;
}

static int
check_identity_and_shape_rejection(void)
{
   struct agx_ppp_viewport_payload_fields fields;
   struct agx_ppp_viewport_payload_core gen6;
   struct agx_ppp_viewport_payload_core gen7;
   init_fields(&fields, 16, 3);
   CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G15G, &fields, &gen6));
   CHECK(agx_ppp_viewport_payload_core_pack(AGX_CHIP_G17S, &fields, &gen7));

   for (unsigned target = 0;
        target < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]);
        ++target) {
      struct agx_ppp_viewport_payload_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_viewport_payload(
         unsupported_chips[target], gen6.bytes, gen6.length, &decoded));
      if (check_fields_cleared(&decoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_viewport_payload(
         unsupported_chips[target], gen7.bytes, gen7.length, &decoded));
      if (check_fields_cleared(&decoded))
         return 1;
   }

   for (size_t size = 0; size < gen7.length; ++size) {
      struct agx_ppp_viewport_payload_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, gen7.bytes,
                                                 size, &decoded));
      if (check_fields_cleared(&decoded))
         return 1;
   }

   const size_t trailing_sizes[] = {gen7.length + 1u, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      struct agx_ppp_viewport_payload_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, gen7.bytes,
                                                 trailing_sizes[i], &decoded));
      if (check_fields_cleared(&decoded))
         return 1;
   }

   for (unsigned bit = 0; bit < 32; ++bit) {
      uint8_t corrupted[AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH];
      struct agx_ppp_viewport_payload_fields decoded;
      memcpy(corrupted, gen7.bytes, gen7.length);
      corrupted[bit / 8] ^= (uint8_t)(1u << (bit % 8));
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, corrupted,
                                                 gen7.length, &decoded));
      if (check_fields_cleared(&decoded))
         return 1;
   }

   uint8_t unaligned[AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH + 1];
   memcpy(&unaligned[1], gen7.bytes, gen7.length);
   struct agx_ppp_viewport_payload_fields decoded;
   CHECK(hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, &unaligned[1],
                                             gen7.length, &decoded));
   if (check_fields_equal(&decoded, &fields))
      return 1;

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, NULL, gen7.length,
                                              &decoded));
   if (check_fields_cleared(&decoded))
      return 1;
   CHECK(!hk_agx3_decode_ppp_viewport_payload(AGX_CHIP_G17S, gen7.bytes,
                                              gen7.length, NULL));
   return 0;
}

int
main(void)
{
   if (check_all_counts_targets_and_shared_bytes() ||
       check_generation_boundaries() || check_identity_and_shape_rejection())
      return 1;

   printf("Honeykrisp AGX3 PPP viewport payload: %u checks passed\n", checks);
   return 0;
}
