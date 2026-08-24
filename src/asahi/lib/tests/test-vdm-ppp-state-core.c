/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_vdm_ppp_state_core.h"

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
check_zero_encoding(const struct agx_vdm_ppp_state_core *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_witness(void)
{
   static const uint8_t expected[AGX_VDM_PPP_STATE_CORE_LENGTH] = {
      0x12, 0x82, 0x00, 0x00, 0x98, 0x78, 0x56, 0x34,
   };

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      struct agx_vdm_ppp_state_core encoded;
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;

      CHECK(agx_vdm_ppp_state_core_supported(supported_chips[i]));
      CHECK(agx_vdm_ppp_state_core_pack(
         supported_chips[i], UINT64_C(0x1234567898), 0x82, &encoded));
      CHECK(encoded.length == sizeof(expected));
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(agx_vdm_ppp_state_core_unpack(supported_chips[i], expected,
                                          sizeof(expected), &pointer,
                                          &size_words));
      CHECK(pointer == UINT64_C(0x1234567898));
      CHECK(size_words == 0x82);
   }

   return 0;
}

static int
check_field_domains(void)
{
   static const uint64_t pointers[] = {
      0,
      4,
      UINT64_C(0x1234567898),
      UINT64_C(0xfffffffffc),
   };
   static const uint32_t sizes[] = {0, 1, 10, 130, 255};

   for (unsigned c = 0;
        c < sizeof(supported_chips) / sizeof(supported_chips[0]); ++c) {
      for (unsigned p = 0; p < sizeof(pointers) / sizeof(pointers[0]); ++p) {
         for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
            struct agx_vdm_ppp_state_core encoded;
            uint64_t pointer = UINT64_MAX;
            uint32_t size_words = UINT32_MAX;

            CHECK(agx_vdm_ppp_state_core_pack(supported_chips[c], pointers[p],
                                              sizes[s], &encoded));
            CHECK(encoded.length == AGX_VDM_PPP_STATE_CORE_LENGTH);
            CHECK(agx_vdm_ppp_state_core_unpack(supported_chips[c],
                                                encoded.bytes, encoded.length,
                                                &pointer, &size_words));
            CHECK(pointer == pointers[p]);
            CHECK(size_words == sizes[s]);
         }
      }

      /* The pinned emitter leaves pointer bits 0..1 zero and models 2..39. */
      for (unsigned bit = 2; bit < 40; ++bit) {
         struct agx_vdm_ppp_state_core encoded;
         uint64_t pointer = UINT64_MAX;
         uint32_t size_words = UINT32_MAX;
         const uint64_t expected = UINT64_C(1) << bit;

         CHECK(agx_vdm_ppp_state_core_pack(supported_chips[c], expected, 0,
                                           &encoded));
         CHECK(agx_vdm_ppp_state_core_unpack(supported_chips[c], encoded.bytes,
                                             encoded.length, &pointer,
                                             &size_words));
         CHECK(pointer == expected);
         CHECK(size_words == 0);
      }

      for (unsigned bit = 0; bit < 8; ++bit) {
         struct agx_vdm_ppp_state_core encoded;
         uint64_t pointer = UINT64_MAX;
         uint32_t size_words = UINT32_MAX;
         const uint32_t expected = UINT32_C(1) << bit;

         CHECK(agx_vdm_ppp_state_core_pack(supported_chips[c], 0, expected,
                                           &encoded));
         CHECK(agx_vdm_ppp_state_core_unpack(supported_chips[c], encoded.bytes,
                                             encoded.length, &pointer,
                                             &size_words));
         CHECK(pointer == 0);
         CHECK(size_words == expected);
      }
   }

   return 0;
}

static int
check_every_record_bit(void)
{
   struct agx_vdm_ppp_state_core baseline;
   CHECK(agx_vdm_ppp_state_core_pack(AGX_CHIP_G17S, 0, 0, &baseline));

   for (unsigned bit = 0; bit < 64; ++bit) {
      uint8_t changed[AGX_VDM_PPP_STATE_CORE_LENGTH];
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;

      memcpy(changed, baseline.bytes, sizeof(changed));
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

      if (bit < 16 || bit >= 34) {
         struct agx_vdm_ppp_state_core repacked;
         const uint64_t expected_pointer = bit < 8 ? UINT64_C(1) << (bit + 32)
                                           : bit >= 34
                                              ? UINT64_C(1) << (bit - 32)
                                              : 0;
         const uint32_t expected_size =
            bit >= 8 && bit < 16 ? UINT32_C(1) << (bit - 8) : 0;

         CHECK(agx_vdm_ppp_state_core_unpack(
            AGX_CHIP_G17S, changed, sizeof(changed), &pointer, &size_words));
         CHECK(pointer == expected_pointer);
         CHECK(size_words == expected_size);
         CHECK(agx_vdm_ppp_state_core_pack(AGX_CHIP_G17S, pointer, size_words,
                                           &repacked));
         CHECK(memcmp(changed, repacked.bytes, sizeof(changed)) == 0);
      } else {
         /* Bits 16..31 are reserved/block type; pointer bits 0..1 violate the
          * directly grounded four-byte alignment requirement. */
         CHECK(!agx_vdm_ppp_state_core_unpack(
            AGX_CHIP_G17S, changed, sizeof(changed), &pointer, &size_words));
         CHECK(pointer == 0);
         CHECK(size_words == 0);
      }
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   static const uint8_t witness[AGX_VDM_PPP_STATE_CORE_LENGTH] = {
      0x12, 0x82, 0x00, 0x00, 0x98, 0x78, 0x56, 0x34,
   };
   struct agx_vdm_ppp_state_core encoded;

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;

      CHECK(!agx_vdm_ppp_state_core_supported(unsupported_chips[i]));
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_ppp_state_core_pack(
         unsupported_chips[i], UINT64_C(0x1234567898), 0x82, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      CHECK(!agx_vdm_ppp_state_core_unpack(unsupported_chips[i], witness,
                                           sizeof(witness), &pointer,
                                           &size_words));
      CHECK(pointer == 0);
      CHECK(size_words == 0);
   }

   static const uint64_t invalid_pointers[] = {
      1,
      2,
      3,
      UINT64_C(0x1234567899),
      UINT64_C(0x123456789a),
      UINT64_C(0x123456789b),
      UINT64_C(1) << 40,
      (UINT64_C(1) << 40) | 4,
      UINT64_MAX,
   };
   for (unsigned i = 0;
        i < sizeof(invalid_pointers) / sizeof(invalid_pointers[0]); ++i) {
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_ppp_state_core_pack(AGX_CHIP_G15G, invalid_pointers[i], 0,
                                         &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   static const uint32_t invalid_sizes[] = {256, 257, UINT32_MAX};
   for (unsigned i = 0; i < sizeof(invalid_sizes) / sizeof(invalid_sizes[0]);
        ++i) {
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_ppp_state_core_pack(AGX_CHIP_G15G, 0, invalid_sizes[i],
                                         &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   CHECK(!agx_vdm_ppp_state_core_pack(AGX_CHIP_G15G, 0, 0, NULL));

   for (size_t size = 0; size < sizeof(witness); ++size) {
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;
      CHECK(!agx_vdm_ppp_state_core_unpack(AGX_CHIP_G15G, witness, size,
                                           &pointer, &size_words));
      CHECK(pointer == 0);
      CHECK(size_words == 0);
   }

   const size_t trailing_sizes[] = {9, 16, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;
      CHECK(!agx_vdm_ppp_state_core_unpack(
         AGX_CHIP_G15G, witness, trailing_sizes[i], &pointer, &size_words));
      CHECK(pointer == 0);
      CHECK(size_words == 0);
   }

   uint8_t unaligned[AGX_VDM_PPP_STATE_CORE_LENGTH + 1];
   memcpy(&unaligned[1], witness, sizeof(witness));
   uint64_t pointer = UINT64_MAX;
   uint32_t size_words = UINT32_MAX;
   CHECK(agx_vdm_ppp_state_core_unpack(AGX_CHIP_G15G, &unaligned[1],
                                       sizeof(witness), &pointer, &size_words));
   CHECK(pointer == UINT64_C(0x1234567898));
   CHECK(size_words == 0x82);

   uint8_t appended[AGX_VDM_PPP_STATE_CORE_LENGTH + 1];
   memcpy(appended, witness, sizeof(witness));
   appended[sizeof(witness)] = 0xa5;
   pointer = UINT64_MAX;
   size_words = UINT32_MAX;
   CHECK(!agx_vdm_ppp_state_core_unpack(
      AGX_CHIP_G15G, appended, sizeof(appended), &pointer, &size_words));
   CHECK(pointer == 0);
   CHECK(size_words == 0);

   pointer = UINT64_MAX;
   size_words = UINT32_MAX;
   CHECK(!agx_vdm_ppp_state_core_unpack(AGX_CHIP_G15G, NULL, sizeof(witness),
                                        &pointer, &size_words));
   CHECK(pointer == 0);
   CHECK(size_words == 0);

   size_words = UINT32_MAX;
   CHECK(!agx_vdm_ppp_state_core_unpack(AGX_CHIP_G15G, witness, sizeof(witness),
                                        NULL, &size_words));
   CHECK(size_words == 0);

   pointer = UINT64_MAX;
   CHECK(!agx_vdm_ppp_state_core_unpack(AGX_CHIP_G15G, witness, sizeof(witness),
                                        &pointer, NULL));
   CHECK(pointer == 0);

   return 0;
}

int
main(void)
{
   if (check_exact_witness() || check_field_domains() ||
       check_every_record_bit() || check_rejections_and_clearing())
      return 1;

   printf("agx_vdm_ppp_state_core: %u checks passed\n", checks);
   return 0;
}
