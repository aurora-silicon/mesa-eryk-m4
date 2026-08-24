/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_tessellate_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(condition)                                                       \
   do {                                                                        \
      ++checks;                                                                \
      if (!(condition)) {                                                      \
         fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,  \
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
   AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
   AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   (enum agx_chip)999,
};

static const struct agx_vdm_tessellate_direct_info witness = {
   .factor_buffer = UINT64_C(0xabcdef123456),
   .patch_count = UINT32_C(0x10203040),
   .instance_count = UINT32_C(0x50607080),
   .base_patch = UINT32_C(0x90a0b0c0),
   .base_instance = UINT32_C(0xd0e0f001),
   .instance_stride_word = UINT32_C(0x7ffff),
   .factor_buffer_size_minus_one = UINT32_C(0xfedcba98),
};

static bool
all_zero(const void *value, size_t size)
{
   const uint8_t *raw = value;
   for (size_t i = 0; i < size; ++i) {
      if (raw[i] != 0)
         return false;
   }
   return true;
}

static int
check_info(const struct agx_vdm_tessellate_direct_info *actual,
           const struct agx_vdm_tessellate_direct_info *expected)
{
   CHECK(actual->factor_buffer == expected->factor_buffer);
   CHECK(actual->patch_count == expected->patch_count);
   CHECK(actual->instance_count == expected->instance_count);
   CHECK(actual->base_patch == expected->base_patch);
   CHECK(actual->base_instance == expected->base_instance);
   CHECK(actual->instance_stride_word == expected->instance_stride_word);
   CHECK(actual->factor_buffer_size_minus_one ==
         expected->factor_buffer_size_minus_one);
   return 0;
}

static int
test_shared_parity(void)
{
   for (size_t i = 0; i < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++i) {
      struct d12_agx3_vdm_tessellate_direct encoded;
      struct agx_vdm_tessellate_direct shared;
      struct agx_vdm_tessellate_direct_info decoded;
      CHECK(d12_agx3_vdm_tessellate_direct_encode(supported_chips[i],
                                                   &witness, &encoded));
      CHECK(agx_vdm_tessellate_direct_pack(supported_chips[i], &witness,
                                            &shared));
      CHECK(encoded.length == D12_AGX3_VDM_TESSELLATE_DIRECT_LENGTH);
      CHECK(encoded.length == shared.length);
      CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
      CHECK(d12_agx3_vdm_tessellate_direct_decode(
         supported_chips[i], encoded.bytes, encoded.length, &decoded));
      if (check_info(&decoded, &witness))
         return 1;
   }

   return 0;
}

static int
test_rejection(void)
{
   struct d12_agx3_vdm_tessellate_direct encoded;
   CHECK(d12_agx3_vdm_tessellate_direct_encode(AGX_CHIP_G17S, &witness,
                                                &encoded));

   for (size_t i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct d12_agx3_vdm_tessellate_direct rejected;
      struct agx_vdm_tessellate_direct_info decoded;
      memset(&rejected, 0xa5, sizeof(rejected));
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_vdm_tessellate_direct_encode(
         unsupported_chips[i], &witness, &rejected));
      CHECK(all_zero(&rejected, sizeof(rejected)));
      CHECK(!d12_agx3_vdm_tessellate_direct_decode(
         unsupported_chips[i], encoded.bytes, encoded.length, &decoded));
      CHECK(all_zero(&decoded, sizeof(decoded)));
   }

   for (uint32_t size = 0; size < encoded.length; ++size) {
      struct agx_vdm_tessellate_direct_info decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_vdm_tessellate_direct_decode(
         AGX_CHIP_G17S, encoded.bytes, size, &decoded));
      CHECK(all_zero(&decoded, sizeof(decoded)));
   }

   uint8_t changed[D12_AGX3_VDM_TESSELLATE_DIRECT_LENGTH];
   for (unsigned bit = 0; bit < sizeof(changed) * 8u; ++bit) {
      struct agx_vdm_tessellate_direct_info shared_decoded;
      struct agx_vdm_tessellate_direct_info d12_decoded;
      memcpy(changed, encoded.bytes, sizeof(changed));
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
      memset(&shared_decoded, 0xa5, sizeof(shared_decoded));
      memset(&d12_decoded, 0xa5, sizeof(d12_decoded));
      const bool shared_accepts = agx_vdm_tessellate_direct_unpack(
         AGX_CHIP_G17S, changed, sizeof(changed), &shared_decoded);
      CHECK(d12_agx3_vdm_tessellate_direct_decode(
               AGX_CHIP_G17S, changed, sizeof(changed), &d12_decoded) ==
            shared_accepts);
      if (shared_accepts)
         CHECK(memcmp(&d12_decoded, &shared_decoded, sizeof(d12_decoded)) == 0);
      else
         CHECK(all_zero(&d12_decoded, sizeof(d12_decoded)));
   }

   struct agx_vdm_tessellate_direct_info invalid = witness;
   invalid.factor_buffer = UINT64_C(1) << 48;
   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!d12_agx3_vdm_tessellate_direct_encode(AGX_CHIP_G17S, &invalid,
                                                 &encoded));
   CHECK(all_zero(&encoded, sizeof(encoded)));
   CHECK(!d12_agx3_vdm_tessellate_direct_encode(AGX_CHIP_G17S, NULL,
                                                 &encoded));
   CHECK(all_zero(&encoded, sizeof(encoded)));
   CHECK(!d12_agx3_vdm_tessellate_direct_encode(AGX_CHIP_G17S, &witness,
                                                 NULL));
   CHECK(!d12_agx3_vdm_tessellate_direct_decode(
      AGX_CHIP_G17S, NULL, D12_AGX3_VDM_TESSELLATE_DIRECT_LENGTH, &invalid));
   CHECK(all_zero(&invalid, sizeof(invalid)));
   CHECK(!d12_agx3_vdm_tessellate_direct_decode(
      AGX_CHIP_G17S, changed, sizeof(changed), NULL));
   return 0;
}

int
main(void)
{
   CHECK(test_shared_parity() == 0);
   CHECK(test_rejection() == 0);
   printf("D3D12 AGX3 VDM Tessellate bridge: %u checks passed\n", checks);
   return 0;
}
