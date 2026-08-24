/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_agx3_vdm_ppp_state_core.h"

#include "asahi/lib/agx_vdm_ppp_state_core.h"

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
check_zero_encoding(const struct d12_agx3_vdm_ppp_state *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_and_shared_equality(void)
{
   static const uint8_t expected[D12_AGX3_VDM_PPP_STATE_LENGTH] = {
      0x12, 0x82, 0x00, 0x00, 0x98, 0x78, 0x56, 0x34,
   };

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      struct d12_agx3_vdm_ppp_state encoded;
      struct agx_vdm_ppp_state_core shared;
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;

      CHECK(d12_agx3_encode_vdm_ppp_state(
         supported_chips[i], UINT64_C(0x1234567898), 0x82, &encoded));
      CHECK(agx_vdm_ppp_state_core_pack(supported_chips[i],
                                        UINT64_C(0x1234567898), 0x82, &shared));
      CHECK(encoded.length == sizeof(expected));
      CHECK(encoded.length == shared.length);
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(memcmp(encoded.bytes, shared.bytes, sizeof(expected)) == 0);
      CHECK(d12_agx3_decode_vdm_ppp_state(supported_chips[i], expected,
                                          sizeof(expected), &pointer,
                                          &size_words));
      CHECK(pointer == UINT64_C(0x1234567898));
      CHECK(size_words == 0x82);
   }

   return 0;
}

static int
check_all_fields_on_all_targets(void)
{
   for (unsigned c = 0;
        c < sizeof(supported_chips) / sizeof(supported_chips[0]); ++c) {
      for (unsigned bit = 2; bit < 40; ++bit) {
         struct d12_agx3_vdm_ppp_state encoded;
         struct agx_vdm_ppp_state_core shared;
         uint64_t pointer = UINT64_C(1) << bit;
         uint64_t decoded_pointer = UINT64_MAX;
         uint32_t decoded_size = UINT32_MAX;

         CHECK(d12_agx3_encode_vdm_ppp_state(supported_chips[c], pointer, 0,
                                             &encoded));
         CHECK(agx_vdm_ppp_state_core_pack(supported_chips[c], pointer, 0,
                                           &shared));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, shared.bytes, sizeof(encoded.bytes)) == 0);
         CHECK(d12_agx3_decode_vdm_ppp_state(supported_chips[c], encoded.bytes,
                                             encoded.length, &decoded_pointer,
                                             &decoded_size));
         CHECK(decoded_pointer == pointer);
         CHECK(decoded_size == 0);
      }

      for (unsigned bit = 0; bit < 8; ++bit) {
         struct d12_agx3_vdm_ppp_state encoded;
         struct agx_vdm_ppp_state_core shared;
         uint64_t decoded_pointer = UINT64_MAX;
         uint32_t decoded_size = UINT32_MAX;
         const uint32_t size_words = UINT32_C(1) << bit;

         CHECK(d12_agx3_encode_vdm_ppp_state(supported_chips[c], 0, size_words,
                                             &encoded));
         CHECK(agx_vdm_ppp_state_core_pack(supported_chips[c], 0, size_words,
                                           &shared));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, shared.bytes, sizeof(encoded.bytes)) == 0);
         CHECK(d12_agx3_decode_vdm_ppp_state(supported_chips[c], encoded.bytes,
                                             encoded.length, &decoded_pointer,
                                             &decoded_size));
         CHECK(decoded_pointer == 0);
         CHECK(decoded_size == size_words);
      }
   }

   return 0;
}

static int
check_every_record_bit(void)
{
   struct d12_agx3_vdm_ppp_state baseline;
   CHECK(d12_agx3_encode_vdm_ppp_state(AGX_CHIP_G15G, 0, 0, &baseline));

   for (unsigned bit = 0; bit < 64; ++bit) {
      uint8_t changed[D12_AGX3_VDM_PPP_STATE_LENGTH];
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;

      memcpy(changed, baseline.bytes, sizeof(changed));
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

      if (bit < 16 || bit >= 34) {
         struct d12_agx3_vdm_ppp_state repacked;
         const uint64_t expected_pointer = bit < 8 ? UINT64_C(1) << (bit + 32)
                                           : bit >= 34
                                              ? UINT64_C(1) << (bit - 32)
                                              : 0;
         const uint32_t expected_size =
            bit >= 8 && bit < 16 ? UINT32_C(1) << (bit - 8) : 0;

         CHECK(d12_agx3_decode_vdm_ppp_state(
            AGX_CHIP_G15G, changed, sizeof(changed), &pointer, &size_words));
         CHECK(pointer == expected_pointer);
         CHECK(size_words == expected_size);
         CHECK(d12_agx3_encode_vdm_ppp_state(AGX_CHIP_G15G, pointer, size_words,
                                             &repacked));
         CHECK(memcmp(changed, repacked.bytes, sizeof(changed)) == 0);
      } else {
         CHECK(!d12_agx3_decode_vdm_ppp_state(
            AGX_CHIP_G15G, changed, sizeof(changed), &pointer, &size_words));
         CHECK(pointer == 0);
         CHECK(size_words == 0);
      }
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   static const uint8_t witness[D12_AGX3_VDM_PPP_STATE_LENGTH] = {
      0x12, 0x82, 0x00, 0x00, 0x98, 0x78, 0x56, 0x34,
   };
   struct d12_agx3_vdm_ppp_state encoded;

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;

      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!d12_agx3_encode_vdm_ppp_state(
         unsupported_chips[i], UINT64_C(0x1234567898), 0x82, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      CHECK(!d12_agx3_decode_vdm_ppp_state(unsupported_chips[i], witness,
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
      CHECK(!d12_agx3_encode_vdm_ppp_state(AGX_CHIP_G17S, invalid_pointers[i],
                                           0, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   static const uint32_t invalid_sizes[] = {256, 257, UINT32_MAX};
   for (unsigned i = 0; i < sizeof(invalid_sizes) / sizeof(invalid_sizes[0]);
        ++i) {
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!d12_agx3_encode_vdm_ppp_state(AGX_CHIP_G17S, 0, invalid_sizes[i],
                                           &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   CHECK(!d12_agx3_encode_vdm_ppp_state(AGX_CHIP_G17S, 0, 0, NULL));

   for (uint32_t size = 0; size < sizeof(witness); ++size) {
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;
      CHECK(!d12_agx3_decode_vdm_ppp_state(AGX_CHIP_G17S, witness, size,
                                           &pointer, &size_words));
      CHECK(pointer == 0);
      CHECK(size_words == 0);
   }

   const uint32_t trailing_sizes[] = {9, 16, UINT32_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      uint64_t pointer = UINT64_MAX;
      uint32_t size_words = UINT32_MAX;
      CHECK(!d12_agx3_decode_vdm_ppp_state(
         AGX_CHIP_G17S, witness, trailing_sizes[i], &pointer, &size_words));
      CHECK(pointer == 0);
      CHECK(size_words == 0);
   }

   uint8_t unaligned[D12_AGX3_VDM_PPP_STATE_LENGTH + 1];
   memcpy(&unaligned[1], witness, sizeof(witness));
   uint64_t pointer = UINT64_MAX;
   uint32_t size_words = UINT32_MAX;
   CHECK(d12_agx3_decode_vdm_ppp_state(AGX_CHIP_G17S, &unaligned[1],
                                       sizeof(witness), &pointer, &size_words));
   CHECK(pointer == UINT64_C(0x1234567898));
   CHECK(size_words == 0x82);

   uint8_t appended[D12_AGX3_VDM_PPP_STATE_LENGTH + 1];
   memcpy(appended, witness, sizeof(witness));
   appended[sizeof(witness)] = 0xa5;
   pointer = UINT64_MAX;
   size_words = UINT32_MAX;
   CHECK(!d12_agx3_decode_vdm_ppp_state(
      AGX_CHIP_G17S, appended, sizeof(appended), &pointer, &size_words));
   CHECK(pointer == 0);
   CHECK(size_words == 0);

   pointer = UINT64_MAX;
   size_words = UINT32_MAX;
   CHECK(!d12_agx3_decode_vdm_ppp_state(AGX_CHIP_G17S, NULL, sizeof(witness),
                                        &pointer, &size_words));
   CHECK(pointer == 0);
   CHECK(size_words == 0);

   size_words = UINT32_MAX;
   CHECK(!d12_agx3_decode_vdm_ppp_state(AGX_CHIP_G17S, witness, sizeof(witness),
                                        NULL, &size_words));
   CHECK(size_words == 0);

   pointer = UINT64_MAX;
   CHECK(!d12_agx3_decode_vdm_ppp_state(AGX_CHIP_G17S, witness, sizeof(witness),
                                        &pointer, NULL));
   CHECK(pointer == 0);

   return 0;
}

int
main(void)
{
   if (check_exact_and_shared_equality() || check_all_fields_on_all_targets() ||
       check_every_record_bit() || check_rejections_and_clearing())
      return 1;

   printf("d12_agx3_vdm_ppp_state_core: %u checks passed\n", checks);
   return 0;
}
