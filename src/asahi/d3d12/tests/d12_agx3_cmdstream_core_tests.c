/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "../d12_agx3_cmdstream_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);               \
         return 1;                                                             \
      }                                                                        \
   } while (0)

int
main(void)
{
   const enum agx_chip launch_targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint8_t launch_bytes[AGX3_CDM_LAUNCH_SIZE] = {
      0x67, 0x45, 0x23, 0x01, 0xde, 0xbc, 0x0a, 0x01,
      0x23, 0x01, 0xef, 0x8d, 0xef, 0xbe, 0xad, 0xde,
      0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
      0x07, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
   };
   struct agx3_cdm_launch_info launch = {
      .word0_payload = 0x01234567,
      .word1_payload = 0x000abcde,
      .pipeline = UINT64_C(0x1237bc048c0),
      .opaque_word3 = 0xdeadbeef,
      .global_size = {3, 5, 7},
      .local_size = {8, 16, 32},
   };

   for (unsigned i = 0;
        i < sizeof(launch_targets) / sizeof(launch_targets[0]); ++i) {
      launch.chip = launch_targets[i];
      struct d12_agx3_cdm_launch_encoding encoded;
      CHECK(d12_agx3_encode_cdm_launch(&launch, &encoded));
      CHECK(encoded.length == AGX3_CDM_LAUNCH_SIZE);
      CHECK(memcmp(encoded.bytes, launch_bytes, sizeof(launch_bytes)) == 0);

      struct agx3_cdm_launch_info decoded;
      CHECK(d12_agx3_decode_cdm_launch(
         launch.chip, encoded.bytes, encoded.length, &decoded));
      CHECK(decoded.chip == launch.chip);
      CHECK(decoded.pipeline == launch.pipeline);
      CHECK(decoded.word0_payload == launch.word0_payload);
      CHECK(decoded.word1_payload == launch.word1_payload);
      CHECK(decoded.opaque_word3 == launch.opaque_word3);
      CHECK(memcmp(decoded.global_size, launch.global_size,
                   sizeof(launch.global_size)) == 0);
      CHECK(memcmp(decoded.local_size, launch.local_size,
                   sizeof(launch.local_size)) == 0);
      CHECK(!d12_agx3_decode_cdm_launch(
         launch.chip, encoded.bytes, encoded.length - 1, &decoded));
      CHECK(decoded.pipeline == 0);
      CHECK(!d12_agx3_decode_cdm_launch(
         launch.chip, encoded.bytes, encoded.length + 1, &decoded));
   }

   const enum agx_chip rejected_launch[] = {
      AGX_CHIP_G13G, AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A,
      AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0;
        i < sizeof(rejected_launch) / sizeof(rejected_launch[0]); ++i) {
      struct d12_agx3_cdm_launch_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      launch.chip = rejected_launch[i];
      CHECK(!d12_agx3_encode_cdm_launch(&launch, &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }
   CHECK(!d12_agx3_encode_cdm_launch(&launch, NULL));

   const enum agx_chip link_targets[] = {
      AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D, AGX_CHIP_G16S,
      AGX_CHIP_G16C, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint8_t link_bytes[AGX3_CDM_STREAM_LINK_SIZE] = {
      0xbc, 0x9a, 0x00, 0x30, 0x78, 0x56, 0x34, 0x12,
   };
   struct agx3_cdm_stream_link_info link = {
      .target = UINT64_C(0x9abc12345678),
      .with_return = true,
   };
   for (unsigned i = 0; i < sizeof(link_targets) / sizeof(link_targets[0]);
        ++i) {
      link.chip = link_targets[i];
      struct d12_agx3_cdm_stream_link_encoding encoded;
      CHECK(d12_agx3_encode_cdm_stream_link(&link, &encoded));
      CHECK(encoded.length == AGX3_CDM_STREAM_LINK_SIZE);
      CHECK(memcmp(encoded.bytes, link_bytes, sizeof(link_bytes)) == 0);

      struct agx3_cdm_stream_link_info decoded;
      CHECK(d12_agx3_decode_cdm_stream_link(
         link.chip, encoded.bytes, encoded.length, &decoded));
      CHECK(decoded.chip == link.chip);
      CHECK(decoded.target == link.target);
      CHECK(decoded.with_return == link.with_return);
      CHECK(!d12_agx3_decode_cdm_stream_link(
         link.chip, encoded.bytes, encoded.length - 1, &decoded));
      CHECK(decoded.target == 0);
      CHECK(!d12_agx3_decode_cdm_stream_link(
         link.chip, encoded.bytes, encoded.length + 1, &decoded));
   }

   const enum agx_chip rejected_link[] = {
      AGX_CHIP_G15G, AGX_CHIP_G16G, AGX_CHIP_G16P, AGX_CHIP_G17P,
      AGX_CHIP_G17A, AGX_CHIP_G17G, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected_link) / sizeof(rejected_link[0]);
        ++i) {
      struct d12_agx3_cdm_stream_link_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      link.chip = rejected_link[i];
      CHECK(!d12_agx3_encode_cdm_stream_link(&link, &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }
   CHECK(!d12_agx3_encode_cdm_stream_link(&link, NULL));

   printf("d12 AGX3 command stream: %u checks passed\n", checks);
   return 0;
}
