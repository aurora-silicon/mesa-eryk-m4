/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_vdm_index_list_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      checks++;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);               \
         return 1;                                                             \
      }                                                                        \
   } while (0)

static const enum agx_chip supported_chips[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static const enum agx_chip hal300_chips[] = {
   AGX_CHIP_G17G,
   AGX_CHIP_G17S,
   AGX_CHIP_G17C,
};

static const enum agx_chip pre_hal300_chips[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
};

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
   AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   (enum agx_chip)999,
};

static const uint8_t primitives[] = {0, 1, 3, 5, 6, 9, 11, 13, 14, 15};

static bool
primitive_supported(uint8_t primitive)
{
   for (unsigned i = 0; i < sizeof(primitives); ++i) {
      if (primitives[i] == primitive)
         return true;
   }

   return false;
}

static bool
is_hal300(enum agx_chip chip)
{
   return chip == AGX_CHIP_G17G || chip == AGX_CHIP_G17S ||
          chip == AGX_CHIP_G17C;
}

static int
check_zero_nonindexed(
   const struct agx_vdm_index_list_direct_nonindexed *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_zero_indexed(const struct agx_vdm_index_list_direct_indexed *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_zero_nonindexed_info(
   const struct agx_vdm_index_list_direct_nonindexed_info *info)
{
   const struct agx_vdm_index_list_direct_nonindexed_info zero = {0};
   CHECK(memcmp(info, &zero, sizeof(*info)) == 0);
   return 0;
}

static int
check_zero_indexed_info(
   const struct agx_vdm_index_list_direct_indexed_info *info)
{
   const struct agx_vdm_index_list_direct_indexed_info zero = {0};
   CHECK(memcmp(info, &zero, sizeof(*info)) == 0);
   return 0;
}

static int
check_nonindexed_info_equal(
   const struct agx_vdm_index_list_direct_nonindexed_info *actual,
   const struct agx_vdm_index_list_direct_nonindexed_info *expected)
{
   CHECK(actual->primitive == expected->primitive);
   CHECK(actual->count == expected->count);
   CHECK(actual->instances == expected->instances);
   CHECK(actual->start == expected->start);
   return 0;
}

static int
check_indexed_info_equal(
   const struct agx_vdm_index_list_direct_indexed_info *actual,
   const struct agx_vdm_index_list_direct_indexed_info *expected)
{
   CHECK(actual->primitive == expected->primitive);
   CHECK(actual->restart_enable == expected->restart_enable);
   CHECK(actual->index_size == expected->index_size);
   CHECK(actual->index_buffer == expected->index_buffer);
   CHECK(actual->count == expected->count);
   CHECK(actual->instances == expected->instances);
   CHECK(actual->start == expected->start);
   CHECK(actual->index_buffer_size_word == expected->index_buffer_size_word);
   return 0;
}

static int
check_exact_witnesses(void)
{
   static const uint8_t nonindexed_g15[] = {
      0x00, 0x06, 0xc4, 0x61, 0x44, 0x33, 0x22, 0x11,
      0x02, 0x00, 0x00, 0x00, 0xdd, 0xcc, 0xbb, 0xaa,
   };
   static const uint8_t nonindexed_hal300[] = {
      0x00, 0x06, 0xc4, 0x69, 0x44, 0x33, 0x22, 0x11,
      0x02, 0x00, 0x00, 0x00, 0xdd, 0xcc, 0xbb, 0xaa,
      0x07, 0x00, 0x00, 0x00,
   };
   static const uint8_t indexed_g15[] = {
      0x34, 0x09, 0xf5, 0x61, 0xbc, 0x9a, 0x78, 0x56,
      0x04, 0x03, 0x02, 0x01, 0x03, 0x00, 0x00, 0x00,
      0xf9, 0xff, 0xff, 0xff, 0x40, 0x30, 0x20, 0x10,
      0x12, 0x00, 0x00, 0x00,
   };
   static const uint8_t indexed_hal300[] = {
      0x34, 0x09, 0xf5, 0x69, 0xbc, 0x9a, 0x78, 0x56,
      0x04, 0x03, 0x02, 0x01, 0x03, 0x00, 0x00, 0x00,
      0xf9, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00,
      0x40, 0x30, 0x20, 0x10, 0x12, 0x00, 0x00, 0x00,
   };
   const struct agx_vdm_index_list_direct_nonindexed_info nonindexed_info = {
      .primitive = 6,
      .count = UINT32_C(0x11223344),
      .instances = 2,
      .start = UINT32_C(0xaabbccdd),
   };
   const struct agx_vdm_index_list_direct_indexed_info indexed_info = {
      .primitive = 9,
      .restart_enable = true,
      .index_size = AGX_VDM_INDEX_LIST_INDEX_SIZE_U32,
      .index_buffer = UINT64_C(0x123456789abc),
      .count = UINT32_C(0x01020304),
      .instances = 3,
      .start = UINT32_C(0xfffffff9),
      .index_buffer_size_word = UINT32_C(0x10203040),
   };

   for (unsigned i = 0; i < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++i) {
      struct agx_vdm_index_list_direct_nonindexed nonindexed;
      struct agx_vdm_index_list_direct_indexed indexed;
      struct agx_vdm_index_list_direct_nonindexed_info decoded_nonindexed;
      struct agx_vdm_index_list_direct_indexed_info decoded_indexed;
      const uint8_t *expected_nonindexed =
         is_hal300(supported_chips[i]) ? nonindexed_hal300 : nonindexed_g15;
      const uint8_t *expected_indexed =
         is_hal300(supported_chips[i]) ? indexed_hal300 : indexed_g15;
      const size_t nonindexed_size =
         is_hal300(supported_chips[i]) ? sizeof(nonindexed_hal300)
                                       : sizeof(nonindexed_g15);
      const size_t indexed_size =
         is_hal300(supported_chips[i]) ? sizeof(indexed_hal300)
                                       : sizeof(indexed_g15);

      CHECK(agx_vdm_index_list_direct_target_supported(supported_chips[i]));
      CHECK(agx_vdm_index_list_direct_nonindexed_pack(
         supported_chips[i], &nonindexed_info, &nonindexed));
      CHECK(nonindexed.length == nonindexed_size);
      CHECK(memcmp(nonindexed.bytes, expected_nonindexed, nonindexed_size) == 0);
      CHECK(agx_vdm_index_list_direct_nonindexed_unpack(
         supported_chips[i], expected_nonindexed, nonindexed_size,
         &decoded_nonindexed));
      if (check_nonindexed_info_equal(&decoded_nonindexed, &nonindexed_info))
         return 1;

      CHECK(agx_vdm_index_list_direct_indexed_pack(
         supported_chips[i], &indexed_info, &indexed));
      CHECK(indexed.length == indexed_size);
      CHECK(memcmp(indexed.bytes, expected_indexed, indexed_size) == 0);
      CHECK(agx_vdm_index_list_direct_indexed_unpack(
         supported_chips[i], expected_indexed, indexed_size, &decoded_indexed));
      if (check_indexed_info_equal(&decoded_indexed, &indexed_info))
         return 1;
   }

   return 0;
}

static int
check_full_domains(void)
{
   for (unsigned c = 0; c < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++c) {
      for (unsigned p = 0; p < sizeof(primitives); ++p) {
         struct agx_vdm_index_list_direct_nonindexed_info nonindexed_info = {
            .primitive = primitives[p],
            .count = UINT32_MAX - p,
            .instances = p + 1,
            .start = UINT32_C(0x80000000) | p,
         };
         struct agx_vdm_index_list_direct_nonindexed_info decoded_nonindexed;
         struct agx_vdm_index_list_direct_nonindexed nonindexed;

         CHECK(agx_vdm_index_list_direct_nonindexed_pack(
            supported_chips[c], &nonindexed_info, &nonindexed));
         CHECK(agx_vdm_index_list_direct_nonindexed_unpack(
            supported_chips[c], nonindexed.bytes, nonindexed.length,
            &decoded_nonindexed));
         if (check_nonindexed_info_equal(&decoded_nonindexed,
                                         &nonindexed_info))
            return 1;

         for (unsigned index_size = 1; index_size <= 2; ++index_size) {
            for (unsigned restart = 0; restart <= 1; ++restart) {
               struct agx_vdm_index_list_direct_indexed_info indexed_info = {
                  .primitive = primitives[p],
                  .restart_enable = restart != 0,
                  .index_size =
                     (enum agx_vdm_index_list_index_size)index_size,
                  .index_buffer = UINT64_C(0xabcdef123456),
                  .count = UINT32_MAX - p,
                  .instances = p + 1,
                  .start = UINT32_C(0x80000000) | p,
                  .index_buffer_size_word = UINT32_C(0xfedcba98),
               };
               struct agx_vdm_index_list_direct_indexed_info decoded_indexed;
               struct agx_vdm_index_list_direct_indexed indexed;

               CHECK(agx_vdm_index_list_direct_indexed_pack(
                  supported_chips[c], &indexed_info, &indexed));
               CHECK(agx_vdm_index_list_direct_indexed_unpack(
                  supported_chips[c], indexed.bytes, indexed.length,
                  &decoded_indexed));
               if (check_indexed_info_equal(&decoded_indexed, &indexed_info))
                  return 1;
            }
         }
      }
   }

   for (unsigned bit = 0; bit < 48; ++bit) {
      struct agx_vdm_index_list_direct_indexed_info info = {
         .primitive = 6,
         .index_size = AGX_VDM_INDEX_LIST_INDEX_SIZE_U16,
         .index_buffer = UINT64_C(1) << bit,
         .instances = 1,
      };
      struct agx_vdm_index_list_direct_indexed_info decoded;
      struct agx_vdm_index_list_direct_indexed encoded;
      CHECK(agx_vdm_index_list_direct_indexed_pack(AGX_CHIP_G17S, &info,
                                                    &encoded));
      CHECK(agx_vdm_index_list_direct_indexed_unpack(
         AGX_CHIP_G17S, encoded.bytes, encoded.length, &decoded));
      CHECK(decoded.index_buffer == info.index_buffer);
   }

   return 0;
}

static int
check_corruption_classification(void)
{
   const struct agx_vdm_index_list_direct_nonindexed_info nonindexed_info = {
      .primitive = 6,
      .count = UINT32_C(0x11223344),
      .instances = UINT32_C(0x55667788),
      .start = UINT32_C(0xaabbccdd),
   };
   const struct agx_vdm_index_list_direct_indexed_info indexed_info = {
      .primitive = 9,
      .restart_enable = true,
      .index_size = AGX_VDM_INDEX_LIST_INDEX_SIZE_U32,
      .index_buffer = UINT64_C(0x123456789abc),
      .count = UINT32_C(0x01020304),
      .instances = UINT32_C(0x55667788),
      .start = UINT32_C(0xfffffff9),
      .index_buffer_size_word = UINT32_C(0x10203040),
   };

   for (unsigned g = 0; g < 2; ++g) {
      const enum agx_chip chip = g ? AGX_CHIP_G17S : AGX_CHIP_G15G;
      struct agx_vdm_index_list_direct_nonindexed baseline_nonindexed;
      struct agx_vdm_index_list_direct_indexed baseline_indexed;
      CHECK(agx_vdm_index_list_direct_nonindexed_pack(
         chip, &nonindexed_info, &baseline_nonindexed));
      CHECK(agx_vdm_index_list_direct_indexed_pack(chip, &indexed_info,
                                                   &baseline_indexed));

      for (unsigned bit = 0; bit < baseline_nonindexed.length * 8u; ++bit) {
         uint8_t changed[AGX_VDM_INDEX_LIST_DIRECT_NONINDEXED_MAX_LENGTH];
         struct agx_vdm_index_list_direct_nonindexed_info decoded;
         struct agx_vdm_index_list_direct_nonindexed repacked;
         memcpy(changed, baseline_nonindexed.bytes, baseline_nonindexed.length);
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
         memset(&decoded, 0xa5, sizeof(decoded));
         const bool accepted = agx_vdm_index_list_direct_nonindexed_unpack(
            chip, changed, baseline_nonindexed.length, &decoded);
         if (accepted) {
            CHECK(primitive_supported(decoded.primitive));
            CHECK(decoded.instances != 0);
            CHECK(agx_vdm_index_list_direct_nonindexed_pack(chip, &decoded,
                                                             &repacked));
            CHECK(memcmp(changed, repacked.bytes, repacked.length) == 0);
         } else if (check_zero_nonindexed_info(&decoded)) {
            return 1;
         }
      }

      for (unsigned bit = 0; bit < baseline_indexed.length * 8u; ++bit) {
         uint8_t changed[AGX_VDM_INDEX_LIST_DIRECT_INDEXED_MAX_LENGTH];
         struct agx_vdm_index_list_direct_indexed_info decoded;
         struct agx_vdm_index_list_direct_indexed repacked;
         memcpy(changed, baseline_indexed.bytes, baseline_indexed.length);
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
         memset(&decoded, 0xa5, sizeof(decoded));
         const bool accepted = agx_vdm_index_list_direct_indexed_unpack(
            chip, changed, baseline_indexed.length, &decoded);
         if (accepted) {
            CHECK(primitive_supported(decoded.primitive));
            CHECK(decoded.index_size == AGX_VDM_INDEX_LIST_INDEX_SIZE_U16 ||
                  decoded.index_size == AGX_VDM_INDEX_LIST_INDEX_SIZE_U32);
            CHECK(decoded.instances != 0);
            CHECK(agx_vdm_index_list_direct_indexed_pack(chip, &decoded,
                                                          &repacked));
            CHECK(memcmp(changed, repacked.bytes, repacked.length) == 0);
         } else if (check_zero_indexed_info(&decoded)) {
            return 1;
         }
      }
   }

   return 0;
}

static int
check_rejections(void)
{
   const struct agx_vdm_index_list_direct_nonindexed_info nonindexed_info = {
      .primitive = 6, .count = 1, .instances = 1, .start = 2,
   };
   const struct agx_vdm_index_list_direct_indexed_info indexed_info = {
      .primitive = 6,
      .index_size = AGX_VDM_INDEX_LIST_INDEX_SIZE_U16,
      .index_buffer = UINT64_C(0x123456789abc),
      .count = 1,
      .instances = 1,
      .start = 2,
      .index_buffer_size_word = 3,
   };
   struct agx_vdm_index_list_direct_nonindexed nonindexed_g15;
   struct agx_vdm_index_list_direct_nonindexed nonindexed_hal300;
   struct agx_vdm_index_list_direct_indexed indexed_g15;
   struct agx_vdm_index_list_direct_indexed indexed_hal300;

   CHECK(agx_vdm_index_list_direct_nonindexed_pack(
      AGX_CHIP_G15G, &nonindexed_info, &nonindexed_g15));
   CHECK(agx_vdm_index_list_direct_nonindexed_pack(
      AGX_CHIP_G17S, &nonindexed_info, &nonindexed_hal300));
   CHECK(agx_vdm_index_list_direct_indexed_pack(AGX_CHIP_G15G, &indexed_info,
                                                &indexed_g15));
   CHECK(agx_vdm_index_list_direct_indexed_pack(AGX_CHIP_G17S, &indexed_info,
                                                &indexed_hal300));

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct agx_vdm_index_list_direct_nonindexed encoded_nonindexed;
      struct agx_vdm_index_list_direct_indexed encoded_indexed;
      struct agx_vdm_index_list_direct_nonindexed_info decoded_nonindexed;
      struct agx_vdm_index_list_direct_indexed_info decoded_indexed;

      CHECK(!agx_vdm_index_list_direct_target_supported(unsupported_chips[i]));
      memset(&encoded_nonindexed, 0xa5, sizeof(encoded_nonindexed));
      CHECK(!agx_vdm_index_list_direct_nonindexed_pack(
         unsupported_chips[i], &nonindexed_info, &encoded_nonindexed));
      if (check_zero_nonindexed(&encoded_nonindexed))
         return 1;
      memset(&encoded_indexed, 0xa5, sizeof(encoded_indexed));
      CHECK(!agx_vdm_index_list_direct_indexed_pack(
         unsupported_chips[i], &indexed_info, &encoded_indexed));
      if (check_zero_indexed(&encoded_indexed))
         return 1;

      memset(&decoded_nonindexed, 0xa5, sizeof(decoded_nonindexed));
      CHECK(!agx_vdm_index_list_direct_nonindexed_unpack(
         unsupported_chips[i], nonindexed_g15.bytes, nonindexed_g15.length,
         &decoded_nonindexed));
      if (check_zero_nonindexed_info(&decoded_nonindexed))
         return 1;
      memset(&decoded_indexed, 0xa5, sizeof(decoded_indexed));
      CHECK(!agx_vdm_index_list_direct_indexed_unpack(
         unsupported_chips[i], indexed_g15.bytes, indexed_g15.length,
         &decoded_indexed));
      if (check_zero_indexed_info(&decoded_indexed))
         return 1;
   }

   /* HAL300 and pre-HAL300 encodings have different exact sizes and headers. */
   for (unsigned i = 0; i < sizeof(pre_hal300_chips) / sizeof(pre_hal300_chips[0]);
        ++i) {
      struct agx_vdm_index_list_direct_nonindexed_info decoded_nonindexed;
      struct agx_vdm_index_list_direct_indexed_info decoded_indexed;
      CHECK(!agx_vdm_index_list_direct_nonindexed_unpack(
         pre_hal300_chips[i], nonindexed_hal300.bytes,
         nonindexed_hal300.length, &decoded_nonindexed));
      CHECK(!agx_vdm_index_list_direct_indexed_unpack(
         pre_hal300_chips[i], indexed_hal300.bytes, indexed_hal300.length,
         &decoded_indexed));
   }
   for (unsigned i = 0; i < sizeof(hal300_chips) / sizeof(hal300_chips[0]);
        ++i) {
      struct agx_vdm_index_list_direct_nonindexed_info decoded_nonindexed;
      struct agx_vdm_index_list_direct_indexed_info decoded_indexed;
      CHECK(!agx_vdm_index_list_direct_nonindexed_unpack(
         hal300_chips[i], nonindexed_g15.bytes, nonindexed_g15.length,
         &decoded_nonindexed));
      CHECK(!agx_vdm_index_list_direct_indexed_unpack(
         hal300_chips[i], indexed_g15.bytes, indexed_g15.length,
         &decoded_indexed));
   }

   for (unsigned primitive = 0; primitive <= UINT8_MAX; ++primitive) {
      if (primitive_supported((uint8_t)primitive))
         continue;

      struct agx_vdm_index_list_direct_nonindexed_info bad_nonindexed =
         nonindexed_info;
      struct agx_vdm_index_list_direct_indexed_info bad_indexed = indexed_info;
      struct agx_vdm_index_list_direct_nonindexed encoded_nonindexed;
      struct agx_vdm_index_list_direct_indexed encoded_indexed;
      bad_nonindexed.primitive = (uint8_t)primitive;
      bad_indexed.primitive = (uint8_t)primitive;
      CHECK(!agx_vdm_index_list_direct_nonindexed_pack(
         AGX_CHIP_G15G, &bad_nonindexed, &encoded_nonindexed));
      CHECK(!agx_vdm_index_list_direct_indexed_pack(
         AGX_CHIP_G17S, &bad_indexed, &encoded_indexed));
   }

   for (unsigned index_size = 0; index_size < 8; ++index_size) {
      if (index_size == 1 || index_size == 2)
         continue;
      struct agx_vdm_index_list_direct_indexed_info bad = indexed_info;
      struct agx_vdm_index_list_direct_indexed encoded;
      bad.index_size = (enum agx_vdm_index_list_index_size)index_size;
      CHECK(!agx_vdm_index_list_direct_indexed_pack(AGX_CHIP_G17S, &bad,
                                                    &encoded));
   }

   {
      struct agx_vdm_index_list_direct_nonindexed_info bad = nonindexed_info;
      struct agx_vdm_index_list_direct_nonindexed encoded;
      bad.instances = 0;
      CHECK(!agx_vdm_index_list_direct_nonindexed_pack(AGX_CHIP_G15G, &bad,
                                                       &encoded));
   }
   {
      struct agx_vdm_index_list_direct_indexed_info bad = indexed_info;
      struct agx_vdm_index_list_direct_indexed encoded;
      bad.instances = 0;
      CHECK(!agx_vdm_index_list_direct_indexed_pack(AGX_CHIP_G15G, &bad,
                                                    &encoded));
      bad = indexed_info;
      bad.index_buffer = UINT64_C(1) << 48;
      CHECK(!agx_vdm_index_list_direct_indexed_pack(AGX_CHIP_G15G, &bad,
                                                    &encoded));
      bad.index_buffer = UINT64_MAX;
      CHECK(!agx_vdm_index_list_direct_indexed_pack(AGX_CHIP_G17S, &bad,
                                                    &encoded));
   }

   {
      struct agx_vdm_index_list_direct_nonindexed temporary;
      struct agx_vdm_index_list_direct_indexed indexed_temporary;
      CHECK(!agx_vdm_index_list_direct_nonindexed_pack(
         AGX_CHIP_G15G, NULL, &temporary));
      if (check_zero_nonindexed(&temporary))
         return 1;
      CHECK(!agx_vdm_index_list_direct_indexed_pack(
         AGX_CHIP_G15G, NULL, &indexed_temporary));
      if (check_zero_indexed(&indexed_temporary))
         return 1;
   }
   CHECK(!agx_vdm_index_list_direct_nonindexed_pack(
      AGX_CHIP_G15G, &nonindexed_info, NULL));
   CHECK(!agx_vdm_index_list_direct_indexed_pack(AGX_CHIP_G15G, &indexed_info,
                                                 NULL));

   for (size_t size = 0; size < nonindexed_g15.length; ++size) {
      struct agx_vdm_index_list_direct_nonindexed_info decoded;
      CHECK(!agx_vdm_index_list_direct_nonindexed_unpack(
         AGX_CHIP_G15G, nonindexed_g15.bytes, size, &decoded));
      if (check_zero_nonindexed_info(&decoded))
         return 1;
   }
   for (size_t size = 0; size < nonindexed_hal300.length; ++size) {
      struct agx_vdm_index_list_direct_nonindexed_info decoded;
      CHECK(!agx_vdm_index_list_direct_nonindexed_unpack(
         AGX_CHIP_G17S, nonindexed_hal300.bytes, size, &decoded));
      if (check_zero_nonindexed_info(&decoded))
         return 1;
   }
   for (size_t size = 0; size < indexed_g15.length; ++size) {
      struct agx_vdm_index_list_direct_indexed_info decoded;
      CHECK(!agx_vdm_index_list_direct_indexed_unpack(
         AGX_CHIP_G15G, indexed_g15.bytes, size, &decoded));
      if (check_zero_indexed_info(&decoded))
         return 1;
   }
   for (size_t size = 0; size < indexed_hal300.length; ++size) {
      struct agx_vdm_index_list_direct_indexed_info decoded;
      CHECK(!agx_vdm_index_list_direct_indexed_unpack(
         AGX_CHIP_G17S, indexed_hal300.bytes, size, &decoded));
      if (check_zero_indexed_info(&decoded))
         return 1;
   }

   {
      struct agx_vdm_index_list_direct_nonindexed_info decoded_nonindexed;
      struct agx_vdm_index_list_direct_indexed_info decoded_indexed;
      uint8_t unaligned_nonindexed[21];
      uint8_t unaligned_indexed[33];
      memcpy(&unaligned_nonindexed[1], nonindexed_hal300.bytes,
             nonindexed_hal300.length);
      memcpy(&unaligned_indexed[1], indexed_hal300.bytes,
             indexed_hal300.length);
      CHECK(agx_vdm_index_list_direct_nonindexed_unpack(
         AGX_CHIP_G17S, &unaligned_nonindexed[1], nonindexed_hal300.length,
         &decoded_nonindexed));
      CHECK(agx_vdm_index_list_direct_indexed_unpack(
         AGX_CHIP_G17S, &unaligned_indexed[1], indexed_hal300.length,
         &decoded_indexed));
      CHECK(!agx_vdm_index_list_direct_nonindexed_unpack(
         AGX_CHIP_G17S, nonindexed_hal300.bytes,
         nonindexed_hal300.length + 1, &decoded_nonindexed));
      CHECK(!agx_vdm_index_list_direct_indexed_unpack(
         AGX_CHIP_G17S, indexed_hal300.bytes, indexed_hal300.length + 1,
         &decoded_indexed));
      CHECK(!agx_vdm_index_list_direct_nonindexed_unpack(
         AGX_CHIP_G17S, NULL, nonindexed_hal300.length, &decoded_nonindexed));
      CHECK(!agx_vdm_index_list_direct_nonindexed_unpack(
         AGX_CHIP_G17S, nonindexed_hal300.bytes, nonindexed_hal300.length,
         NULL));
      CHECK(!agx_vdm_index_list_direct_indexed_unpack(
         AGX_CHIP_G17S, NULL, indexed_hal300.length, &decoded_indexed));
      CHECK(!agx_vdm_index_list_direct_indexed_unpack(
         AGX_CHIP_G17S, indexed_hal300.bytes, indexed_hal300.length, NULL));
   }

   return 0;
}

int
main(void)
{
   if (check_exact_witnesses() || check_full_domains() ||
       check_corruption_classification() || check_rejections())
      return 1;

   printf("agx_vdm_index_list_core: %u checks passed\n", checks);
   return 0;
}
