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

static int
test_ioto_profile(void)
{
   const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint16_t values[] = {0, 1, 0x00ff, 0xff00, UINT16_MAX};

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool hal300 = targets[t] == AGX_CHIP_G17G ||
                          targets[t] == AGX_CHIP_G17S ||
                          targets[t] == AGX_CHIP_G17C;
      for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
         struct d12_agx3_ioto_profile_encoding encoded;
         CHECK(d12_agx3_encode_ioto_profile(
            targets[t], values[v], hal300 ? (uint16_t)~values[v] : 0,
            &encoded));
         CHECK(encoded.length == (hal300 ? 6 : 4));
         CHECK(encoded.bytes[0] == (hal300 ? 0x07 : 0xf7));
         CHECK(encoded.bytes[1] == (hal300 ? 0x00 : 0x03));

         uint16_t payload0, payload1;
         CHECK(d12_agx3_decode_ioto_profile(
            targets[t], encoded.bytes, encoded.length, &payload0, &payload1));
         CHECK(payload0 == values[v]);
         CHECK(payload1 == (hal300 ? (uint16_t)~values[v] : 0));
         CHECK(!d12_agx3_decode_ioto_profile(targets[t], encoded.bytes,
                                             encoded.length + 1, &payload0,
                                             &payload1));
         CHECK(payload0 == 0 && payload1 == 0);
      }
   }

   const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G14X, AGX_CHIP_G15P, AGX_CHIP_G16P,
      AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      struct d12_agx3_ioto_profile_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      CHECK(!d12_agx3_encode_ioto_profile(rejected[i], 0, 0, &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }

   struct d12_agx3_ioto_profile_encoding encoded;
   CHECK(!d12_agx3_encode_ioto_profile(AGX_CHIP_G16G, 0, 1, &encoded));
   CHECK(!d12_agx3_encode_ioto_profile(AGX_CHIP_G16G, 0, 0, NULL));
   CHECK(!d12_agx3_decode_ioto_profile(AGX_CHIP_G16G, NULL, 4, NULL, NULL));
   return 0;
}

int
main(void)
{
   if (test_ioto_profile())
      return 1;

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

      struct agx3_cdm_decoded_record record;
      CHECK(d12_agx3_decode_cdm_record(launch.chip, encoded.bytes,
                                       encoded.length, &record));
      CHECK(record.kind == AGX3_CDM_RECORD_LAUNCH);
      CHECK(record.length == encoded.length);
      CHECK(record.chip == launch.chip);
      CHECK(record.data.launch.pipeline == launch.pipeline);
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

   for (unsigned i = 0;
        i < sizeof(launch_targets) / sizeof(launch_targets[0]); ++i) {
      for (uint8_t count = 0; count < 8; ++count) {
         struct agx3_cdm_link_padding_info padding = {
            .chip = launch_targets[i],
            .count = count,
         };
         struct d12_agx3_cdm_link_padding_encoding encoded;
         struct agx3_cdm_link_padding_info decoded;
         CHECK(d12_agx3_encode_cdm_link_padding(&padding, &encoded));
         CHECK(encoded.length == AGX3_CDM_LINK_PADDING_SIZE);
         CHECK(agx3_cmdstream_load_le32(encoded.bytes) ==
               (UINT32_C(0xa0000000) | count));
         CHECK(d12_agx3_decode_cdm_link_padding(
            padding.chip, encoded.bytes, encoded.length, &decoded));
         CHECK(decoded.count == count);
         CHECK(!d12_agx3_decode_cdm_link_padding(
            padding.chip, encoded.bytes, encoded.length + 1, &decoded));
         CHECK(decoded.count == 0);
      }
   }

   struct agx3_cdm_link_padding_info rejected_padding = {
      .chip = AGX_CHIP_G17A,
      .count = 0,
   };
   struct d12_agx3_cdm_link_padding_encoding padding_encoding;
   memset(&padding_encoding, 0xff, sizeof(padding_encoding));
   CHECK(!d12_agx3_encode_cdm_link_padding(&rejected_padding,
                                           &padding_encoding));
   CHECK(padding_encoding.length == 0);
   for (unsigned b = 0; b < sizeof(padding_encoding.bytes); ++b)
      CHECK(padding_encoding.bytes[b] == 0);
   CHECK(!d12_agx3_encode_cdm_link_padding(&rejected_padding, NULL));

   const enum agx_chip barrier_targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   for (unsigned i = 0;
        i < sizeof(barrier_targets) / sizeof(barrier_targets[0]); ++i) {
      const bool gen7 = barrier_targets[i] == AGX_CHIP_G17G ||
                        barrier_targets[i] == AGX_CHIP_G17S ||
                        barrier_targets[i] == AGX_CHIP_G17C;
      const uint8_t end = gen7 ? 16 : 6;
      for (uint8_t flags = 0; flags < end; ++flags) {
         if (!gen7 && flags != 0 && flags != 5)
            continue;
         struct agx3_cdm_barrier_info barrier = {
            .chip = barrier_targets[i],
            .flags = flags,
         };
         struct d12_agx3_cdm_barrier_encoding encoded;
         struct agx3_cdm_barrier_info decoded;
         CHECK(d12_agx3_encode_cdm_barrier(&barrier, &encoded));
         CHECK(encoded.length == AGX3_CDM_BARRIER_SIZE);
         CHECK(agx3_cmdstream_load_le32(encoded.bytes) ==
               agx3_cdm_barrier_word(flags));
         CHECK(d12_agx3_decode_cdm_barrier(
            barrier.chip, encoded.bytes, encoded.length, &decoded));
         CHECK(decoded.flags == flags);
         CHECK(!d12_agx3_decode_cdm_barrier(
            barrier.chip, encoded.bytes, encoded.length + 1, &decoded));
         CHECK(decoded.flags == 0);
      }
   }

   struct agx3_cdm_barrier_info rejected_barrier = {
      .chip = AGX_CHIP_G17A,
      .flags = 0,
   };
   struct d12_agx3_cdm_barrier_encoding barrier_encoding;
   memset(&barrier_encoding, 0xff, sizeof(barrier_encoding));
   CHECK(!d12_agx3_encode_cdm_barrier(&rejected_barrier,
                                      &barrier_encoding));
   CHECK(barrier_encoding.length == 0);
   for (unsigned b = 0; b < sizeof(barrier_encoding.bytes); ++b)
      CHECK(barrier_encoding.bytes[b] == 0);
   CHECK(!d12_agx3_encode_cdm_barrier(&rejected_barrier, NULL));

   for (unsigned i = 0;
        i < sizeof(barrier_targets) / sizeof(barrier_targets[0]); ++i) {
      const bool gen7 = barrier_targets[i] == AGX_CHIP_G17G ||
                        barrier_targets[i] == AGX_CHIP_G17S ||
                        barrier_targets[i] == AGX_CHIP_G17C;
      const uint8_t flags[] = {0, 5, 0xf};
      const uint8_t substreams[] = {0, 1, 0x7f, 0xfe};
      for (unsigned f = 0; f < sizeof(flags) / sizeof(flags[0]); ++f) {
         if (!gen7 && flags[f] == 0xf)
            continue;
         for (unsigned s = 0;
              s < sizeof(substreams) / sizeof(substreams[0]); ++s) {
            struct agx3_cdm_substream_barrier_info barrier = {
               .chip = barrier_targets[i],
               .flags = flags[f],
               .substream = substreams[s],
            };
            struct d12_agx3_cdm_barrier_encoding encoded;
            struct agx3_cdm_substream_barrier_info decoded;
            CHECK(d12_agx3_encode_cdm_substream_barrier(&barrier, &encoded));
            CHECK(encoded.length == AGX3_CDM_BARRIER_SIZE);
            CHECK(d12_agx3_decode_cdm_substream_barrier(
               barrier.chip, encoded.bytes, encoded.length, &decoded));
            CHECK(decoded.flags == barrier.flags);
            CHECK(decoded.substream == barrier.substream);
            CHECK(!d12_agx3_decode_cdm_substream_barrier(
               barrier.chip, encoded.bytes, encoded.length + 1, &decoded));
            CHECK(decoded.substream == 0);
         }
      }
   }

   struct agx3_cdm_substream_barrier_info rejected_substream = {
      .chip = AGX_CHIP_G17G,
      .flags = 0,
      .substream = UINT8_MAX,
   };
   memset(&barrier_encoding, 0xff, sizeof(barrier_encoding));
   CHECK(!d12_agx3_encode_cdm_substream_barrier(&rejected_substream,
                                                 &barrier_encoding));
   CHECK(barrier_encoding.length == 0);
   for (unsigned b = 0; b < sizeof(barrier_encoding.bytes); ++b)
      CHECK(barrier_encoding.bytes[b] == 0);

   const enum agx_chip secondary_targets[] = {
      AGX_CHIP_G17G,
      AGX_CHIP_G17S,
      AGX_CHIP_G17C,
   };
   for (unsigned i = 0;
        i < sizeof(secondary_targets) / sizeof(secondary_targets[0]); ++i) {
      struct d12_agx3_cdm_secondary_barrier_encoding encoded;
      CHECK(d12_agx3_encode_cdm_secondary_barrier(secondary_targets[i],
                                                   &encoded));
      CHECK(encoded.length == AGX3_CDM_SECONDARY_BARRIER_SIZE);
      CHECK(agx3_cmdstream_load_le32(encoded.bytes) == UINT32_C(0x60000502));
      CHECK(d12_agx3_validate_cdm_secondary_barrier(
         secondary_targets[i], encoded.bytes, encoded.length));
      CHECK(!d12_agx3_validate_cdm_secondary_barrier(
         secondary_targets[i], encoded.bytes, encoded.length + 1));
   }

   struct d12_agx3_cdm_secondary_barrier_encoding secondary;
   memset(&secondary, 0xff, sizeof(secondary));
   CHECK(!d12_agx3_encode_cdm_secondary_barrier(AGX_CHIP_G17P, &secondary));
   CHECK(secondary.length == 0);
   for (unsigned b = 0; b < sizeof(secondary.bytes); ++b)
      CHECK(secondary.bytes[b] == 0);

   for (unsigned i = 0;
        i < sizeof(launch_targets) / sizeof(launch_targets[0]); ++i) {
      struct d12_agx3_cdm_stream_terminate_encoding encoded;
      CHECK(d12_agx3_encode_cdm_stream_terminate(launch_targets[i],
                                                 &encoded));
      CHECK(encoded.length == AGX3_CDM_STREAM_TERMINATE_SIZE);
      CHECK(agx3_cmdstream_load_le32(encoded.bytes) == UINT32_C(0x40000000));
      CHECK(agx3_cmdstream_load_le32(encoded.bytes + 4) == 0);
      CHECK(d12_agx3_validate_cdm_stream_terminate(
         launch_targets[i], encoded.bytes, encoded.length));
      CHECK(!d12_agx3_validate_cdm_stream_terminate(
         launch_targets[i], encoded.bytes, encoded.length + 1));
   }

   struct d12_agx3_cdm_stream_terminate_encoding terminate;
   memset(&terminate, 0xff, sizeof(terminate));
   CHECK(!d12_agx3_encode_cdm_stream_terminate(AGX_CHIP_G16P, &terminate));
   CHECK(terminate.length == 0);
   for (unsigned b = 0; b < sizeof(terminate.bytes); ++b)
      CHECK(terminate.bytes[b] == 0);
   CHECK(!d12_agx3_encode_cdm_stream_terminate(AGX_CHIP_G17S, NULL));

   printf("d12 AGX3 command stream: %u checks passed\n", checks);
   return 0;
}
