/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "../agx_ppp_decode_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                               \
      if (!(x)) {                                                             \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);               \
         return 1;                                                            \
      }                                                                       \
   } while (0)

static const enum agx_chip all_targets[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static bool
record_clear(const struct agx_ppp_decoded_record *record)
{
   static const struct agx_ppp_decoded_record zero;
   return memcmp(record, &zero, sizeof(*record)) == 0;
}

static int
check_rejected_sizes(enum agx_chip chip, const uint8_t *bytes, size_t size)
{
   struct agx_ppp_decoded_record decoded;
   for (size_t truncated = 0; truncated < size; ++truncated) {
      memset(&decoded, 0xff, sizeof(decoded));
      CHECK(!agx_ppp_decode_one(chip, bytes, truncated, &decoded));
      CHECK(record_clear(&decoded));
   }

   uint8_t appended[AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH + 1];
   CHECK(size < sizeof(appended));
   memcpy(appended, bytes, size);
   appended[size] = 0xa5;
   memset(&decoded, 0xff, sizeof(decoded));
   CHECK(!agx_ppp_decode_one(chip, appended, size + 1, &decoded));
   CHECK(record_clear(&decoded));
   return 0;
}

static int
check_header_mutations(enum agx_chip chip, const uint8_t *bytes, size_t size)
{
   for (unsigned bit = 0; bit < 32; ++bit) {
      uint8_t mutated[AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH];
      memcpy(mutated, bytes, size);
      mutated[bit / 8] ^= (uint8_t)(1u << (bit % 8));
      struct agx_ppp_decoded_record decoded;
      memset(&decoded, 0xff, sizeof(decoded));
      CHECK(!agx_ppp_decode_one(chip, mutated, size, &decoded));
      CHECK(record_clear(&decoded));
   }
   return 0;
}

static int
check_common_forms(void)
{
   for (size_t c = 0; c < sizeof(all_targets) / sizeof(all_targets[0]); ++c) {
      const enum agx_chip chip = all_targets[c];
      const struct agx_ppp_w_clamp_cull2_fields wc = {
         .w_clamp_raw = UINT32_C(0x7fc12345),
         .cull2_raw = UINT32_C(0xa5c37e91),
      };
      struct agx_ppp_w_clamp_cull2_core packed_wc;
      CHECK(agx_ppp_w_clamp_cull2_core_pack(chip, &wc, &packed_wc));

      struct agx_ppp_decoded_record decoded;
      CHECK(agx_ppp_decode_one(chip, packed_wc.bytes, packed_wc.length,
                               &decoded));
      CHECK(decoded.kind == AGX_PPP_DECODED_RECORD_W_CLAMP_CULL2);
      CHECK(decoded.length == packed_wc.length);
      CHECK(decoded.data.w_clamp_cull2.w_clamp_raw == wc.w_clamp_raw);
      CHECK(decoded.data.w_clamp_cull2.cull2_raw == wc.cull2_raw);
      if (check_rejected_sizes(chip, packed_wc.bytes, packed_wc.length) ||
          check_header_mutations(chip, packed_wc.bytes, packed_wc.length))
         return 1;

      for (unsigned count = 1; count <= 16; ++count) {
         struct agx_ppp_viewport_payload_fields viewport = {
            .viewport_count = (uint8_t)count,
            .viewport_control_raw = UINT32_C(0xdeadbeef) ^ count,
         };
         for (unsigned i = 0; i < count; ++i) {
            viewport.region_clip_raw[i] =
               UINT64_C(0x0102030405060708) ^ ((uint64_t)i << 33) ^ count;
            viewport.viewports[i] = (struct agx_ppp_viewport_transform_raw){
               .translate_x = UINT32_C(0x10000000) + i,
               .scale_x = UINT32_C(0x20000000) + i,
               .translate_y = UINT32_C(0x30000000) + i,
               .scale_y = UINT32_C(0x40000000) + i,
               .translate_z = UINT32_C(0x50000000) + i,
               .scale_z = UINT32_C(0x60000000) + i,
            };
         }

         struct agx_ppp_viewport_payload_core packed_viewport;
         CHECK(agx_ppp_viewport_payload_core_pack(chip, &viewport,
                                                   &packed_viewport));
         CHECK(agx_ppp_decode_one(chip, packed_viewport.bytes,
                                  packed_viewport.length, &decoded));
         CHECK(decoded.kind == AGX_PPP_DECODED_RECORD_VIEWPORT);
         CHECK(decoded.length == packed_viewport.length);
         CHECK(decoded.data.viewport.viewport_count == count);
         CHECK(decoded.data.viewport.viewport_control_raw ==
               viewport.viewport_control_raw);
         CHECK(memcmp(decoded.data.viewport.region_clip_raw,
                      viewport.region_clip_raw,
                      sizeof(viewport.region_clip_raw)) == 0);
         CHECK(memcmp(decoded.data.viewport.viewports, viewport.viewports,
                      sizeof(viewport.viewports)) == 0);
      }
   }
   return 0;
}

static int
check_hal300_forms(void)
{
   static const enum agx_chip hal300[] = {
      AGX_CHIP_G17G,
      AGX_CHIP_G17S,
      AGX_CHIP_G17C,
   };
   for (size_t c = 0; c < sizeof(hal300) / sizeof(hal300[0]); ++c) {
      const enum agx_chip chip = hal300[c];
      const struct agx_ppp_hal300_depth_stencil_fields ds = {
         .fragment_control_raw = UINT32_C(0x01020304),
         .fragment_front_face_raw = UINT32_C(0x11223344),
         .fragment_back_face_raw = UINT32_C(0x55667788),
         .fragment_front_stencil_raw = UINT32_C(0x99aabbcc),
         .fragment_back_stencil_raw = UINT32_C(0xddeeff00),
      };
      struct agx_ppp_hal300_depth_stencil_core packed_ds;
      CHECK(agx_ppp_hal300_depth_stencil_core_pack(chip, &ds, &packed_ds));
      struct agx_ppp_decoded_record decoded;
      CHECK(agx_ppp_decode_one(chip, packed_ds.bytes, packed_ds.length,
                               &decoded));
      CHECK(decoded.kind == AGX_PPP_DECODED_RECORD_HAL300_DEPTH_STENCIL);
      CHECK(memcmp(&decoded.data.hal300_depth_stencil, &ds, sizeof(ds)) == 0);
      if (check_rejected_sizes(chip, packed_ds.bytes, packed_ds.length) ||
          check_header_mutations(chip, packed_ds.bytes, packed_ds.length))
         return 1;

      const struct agx_ppp_hal300_depth_bias_scissor_bases_fields bases = {
         .depth_bias_base = UINT64_C(0x123456789ab0),
         .scissor_base = UINT64_C(0x0fedcba98760),
      };
      struct agx_ppp_hal300_depth_bias_scissor_bases packed_bases;
      CHECK(agx_ppp_hal300_depth_bias_scissor_bases_pack(
         chip, &bases, &packed_bases));
      CHECK(agx_ppp_decode_one(chip, packed_bases.bytes, packed_bases.length,
                               &decoded));
      CHECK(decoded.kind ==
            AGX_PPP_DECODED_RECORD_HAL300_DEPTH_BIAS_SCISSOR_BASES);
      CHECK(decoded.data.hal300_depth_bias_scissor_bases.depth_bias_base ==
            bases.depth_bias_base);
      CHECK(decoded.data.hal300_depth_bias_scissor_bases.scissor_base ==
            bases.scissor_base);
      if (check_rejected_sizes(chip, packed_bases.bytes, packed_bases.length) ||
          check_header_mutations(chip, packed_bases.bytes,
                                 packed_bases.length))
         return 1;

      CHECK(!agx_ppp_decode_one(AGX_CHIP_G17P, packed_ds.bytes,
                                packed_ds.length, &decoded));
      CHECK(record_clear(&decoded));
      CHECK(!agx_ppp_decode_one(AGX_CHIP_G16G, packed_bases.bytes,
                                packed_bases.length, &decoded));
      CHECK(record_clear(&decoded));
   }
   return 0;
}

static int
check_failures(void)
{
   uint8_t unknown[AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH + 1];
   memset(unknown, 0xa5, sizeof(unknown));
   struct agx_ppp_decoded_record decoded;
   memset(&decoded, 0xff, sizeof(decoded));
   CHECK(!agx_ppp_decode_one(AGX_CHIP_G15G, unknown, sizeof(unknown),
                             &decoded));
   CHECK(record_clear(&decoded));
   CHECK(!agx_ppp_decode_one(AGX_CHIP_G15G, NULL, 12, &decoded));
   CHECK(record_clear(&decoded));
   CHECK(!agx_ppp_decode_one(AGX_CHIP_G15G, unknown, 12, NULL));
   CHECK(!agx_ppp_decode_one(AGX_CHIP_G15P, unknown, 12, &decoded));
   CHECK(record_clear(&decoded));
   return 0;
}

int
main(void)
{
   if (check_common_forms() || check_hal300_forms() || check_failures())
      return 1;

   printf("agx_ppp_decode_core: %u checks passed\n", checks);
   return 0;
}
