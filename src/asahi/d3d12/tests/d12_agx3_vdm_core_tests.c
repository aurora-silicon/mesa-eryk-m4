/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_agx3_vdm_core.h"

#include "asahi/lib/agx_vdm_state_core.h"

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

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
   AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
};

static int
check_zero_encoding(const struct d12_agx3_vdm_vertex_pipeline_pair *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_all_supported_targets(void)
{
   static const uint8_t expected[D12_AGX3_VDM_VERTEX_PIPELINE_PAIR_LENGTH] = {
      0xde, 0xbc, 0x2a, 0x12, 0x6a, 0xe2, 0x59, 0xd1,
   };

   for (unsigned i = 0; i < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++i) {
      struct d12_agx3_vdm_vertex_pipeline_pair encoded;
      struct agx_vdm_state_vertex_pipeline_pair shared;
      uint64_t pipeline = 0;
      uint32_t raw_template = 0;

      CHECK(d12_agx3_vdm_encode_vertex_pipeline_pair(
         supported_chips[i], UINT64_C(0x123456789a80), UINT32_C(0x2abcde),
         &encoded));
      CHECK(agx_vdm_state_vertex_pipeline_pair_pack(
         supported_chips[i], UINT64_C(0x123456789a80), UINT32_C(0x2abcde),
         &shared));
      CHECK(encoded.length == sizeof(expected));
      CHECK(encoded.length == shared.length);
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(memcmp(encoded.bytes, shared.bytes, sizeof(expected)) == 0);
      CHECK(d12_agx3_vdm_decode_vertex_pipeline_pair(
         supported_chips[i], encoded.bytes, encoded.length, &pipeline,
         &raw_template));
      CHECK(pipeline == UINT64_C(0x123456789a80));
      CHECK(raw_template == UINT32_C(0x2abcde));
   }

   return 0;
}

static int
check_boundaries_and_bits(void)
{
   static const struct {
      uint64_t pipeline;
      uint32_t raw_template;
      uint8_t expected[D12_AGX3_VDM_VERTEX_PIPELINE_PAIR_LENGTH];
   } vectors[] = {
      {0, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {UINT64_C(0x40), 0,
       {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
      {UINT64_C(0xffffffffffc0), UINT32_C(0x003fffff),
       {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
   };

   for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
      struct d12_agx3_vdm_vertex_pipeline_pair encoded;
      uint64_t pipeline = UINT64_MAX;
      uint32_t raw_template = UINT32_MAX;

      CHECK(d12_agx3_vdm_encode_vertex_pipeline_pair(
         AGX_CHIP_G17S, vectors[i].pipeline, vectors[i].raw_template,
         &encoded));
      CHECK(memcmp(encoded.bytes, vectors[i].expected,
                   sizeof(vectors[i].expected)) == 0);
      CHECK(d12_agx3_vdm_decode_vertex_pipeline_pair(
         AGX_CHIP_G17S, encoded.bytes, encoded.length, &pipeline,
         &raw_template));
      CHECK(pipeline == vectors[i].pipeline);
      CHECK(raw_template == vectors[i].raw_template);
   }

   for (unsigned bit = 6; bit < 48; ++bit) {
      struct d12_agx3_vdm_vertex_pipeline_pair encoded;
      uint64_t pipeline = 0;
      uint32_t raw_template = UINT32_MAX;

      CHECK(d12_agx3_vdm_encode_vertex_pipeline_pair(
         AGX_CHIP_G15D, UINT64_C(1) << bit, 0, &encoded));
      CHECK(d12_agx3_vdm_decode_vertex_pipeline_pair(
         AGX_CHIP_G15D, encoded.bytes, encoded.length, &pipeline,
         &raw_template));
      CHECK(pipeline == (UINT64_C(1) << bit));
      CHECK(raw_template == 0);
   }

   for (unsigned bit = 0; bit < 22; ++bit) {
      struct d12_agx3_vdm_vertex_pipeline_pair encoded;
      uint64_t pipeline = UINT64_MAX;
      uint32_t raw_template = 0;

      CHECK(d12_agx3_vdm_encode_vertex_pipeline_pair(
         AGX_CHIP_G16C, 0, UINT32_C(1) << bit, &encoded));
      CHECK(d12_agx3_vdm_decode_vertex_pipeline_pair(
         AGX_CHIP_G16C, encoded.bytes, encoded.length, &pipeline,
         &raw_template));
      CHECK(pipeline == 0);
      CHECK(raw_template == (UINT32_C(1) << bit));
   }

   return 0;
}

static int
check_lossless_corruptions(void)
{
   struct d12_agx3_vdm_vertex_pipeline_pair baseline;
   CHECK(d12_agx3_vdm_encode_vertex_pipeline_pair(
      AGX_CHIP_G17G, UINT64_C(0x123456789a80), UINT32_C(0x2abcde),
      &baseline));

   for (unsigned bit = 0; bit < 64; ++bit) {
      uint8_t changed[D12_AGX3_VDM_VERTEX_PIPELINE_PAIR_LENGTH];
      struct d12_agx3_vdm_vertex_pipeline_pair repacked;
      uint64_t pipeline = 0;
      uint32_t raw_template = 0;

      memcpy(changed, baseline.bytes, sizeof(changed));
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
      CHECK(d12_agx3_vdm_decode_vertex_pipeline_pair(
         AGX_CHIP_G17G, changed, sizeof(changed), &pipeline, &raw_template));
      CHECK(d12_agx3_vdm_encode_vertex_pipeline_pair(
         AGX_CHIP_G17G, pipeline, raw_template, &repacked));
      CHECK(memcmp(changed, repacked.bytes, sizeof(changed)) == 0);
   }

   return 0;
}

static int
check_rejections(void)
{
   static const uint8_t witness[D12_AGX3_VDM_VERTEX_PIPELINE_PAIR_LENGTH] = {
      0xde, 0xbc, 0x2a, 0x12, 0x6a, 0xe2, 0x59, 0xd1,
   };
   struct d12_agx3_vdm_vertex_pipeline_pair encoded;

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      uint64_t pipeline = UINT64_MAX;
      uint32_t raw_template = UINT32_MAX;

      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!d12_agx3_vdm_encode_vertex_pipeline_pair(
         unsupported_chips[i], UINT64_C(0x123456789a80),
         UINT32_C(0x2abcde), &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      CHECK(!d12_agx3_vdm_decode_vertex_pipeline_pair(
         unsupported_chips[i], witness, sizeof(witness), &pipeline,
         &raw_template));
      CHECK(pipeline == 0);
      CHECK(raw_template == 0);
   }

   const uint64_t invalid_pipelines[] = {
      1, 63, UINT64_C(0x123456789a81), UINT64_C(1) << 48, UINT64_MAX,
   };
   for (unsigned i = 0;
        i < sizeof(invalid_pipelines) / sizeof(invalid_pipelines[0]); ++i) {
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!d12_agx3_vdm_encode_vertex_pipeline_pair(
         AGX_CHIP_G17S, invalid_pipelines[i], 0, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   const uint32_t invalid_templates[] = {
      UINT32_C(1) << 22, UINT32_C(0x00400001), UINT32_MAX,
   };
   for (unsigned i = 0;
        i < sizeof(invalid_templates) / sizeof(invalid_templates[0]); ++i) {
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!d12_agx3_vdm_encode_vertex_pipeline_pair(
         AGX_CHIP_G15G, 0, invalid_templates[i], &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   CHECK(!d12_agx3_vdm_encode_vertex_pipeline_pair(
      AGX_CHIP_G15G, 0, 0, NULL));

   for (uint32_t size = 0; size < sizeof(witness); ++size) {
      uint64_t pipeline = UINT64_MAX;
      uint32_t raw_template = UINT32_MAX;
      CHECK(!d12_agx3_vdm_decode_vertex_pipeline_pair(
         AGX_CHIP_G15G, witness, size, &pipeline, &raw_template));
      CHECK(pipeline == 0);
      CHECK(raw_template == 0);
   }

   const uint32_t trailing_sizes[] = {9, 16, UINT32_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      uint64_t pipeline = UINT64_MAX;
      uint32_t raw_template = UINT32_MAX;
      CHECK(!d12_agx3_vdm_decode_vertex_pipeline_pair(
         AGX_CHIP_G15G, witness, trailing_sizes[i], &pipeline,
         &raw_template));
      CHECK(pipeline == 0);
      CHECK(raw_template == 0);
   }

   uint64_t pipeline = UINT64_MAX;
   uint32_t raw_template = UINT32_MAX;
   CHECK(!d12_agx3_vdm_decode_vertex_pipeline_pair(
      AGX_CHIP_G15G, NULL, sizeof(witness), &pipeline, &raw_template));
   CHECK(pipeline == 0);
   CHECK(raw_template == 0);

   raw_template = UINT32_MAX;
   CHECK(!d12_agx3_vdm_decode_vertex_pipeline_pair(
      AGX_CHIP_G15G, witness, sizeof(witness), NULL, &raw_template));
   CHECK(raw_template == 0);

   pipeline = UINT64_MAX;
   CHECK(!d12_agx3_vdm_decode_vertex_pipeline_pair(
      AGX_CHIP_G15G, witness, sizeof(witness), &pipeline, NULL));
   CHECK(pipeline == 0);

   return 0;
}

static int
check_unaligned(void)
{
   uint8_t storage[D12_AGX3_VDM_VERTEX_PIPELINE_PAIR_LENGTH + 1] = {0};
   struct d12_agx3_vdm_vertex_pipeline_pair encoded;
   uint64_t pipeline = 0;
   uint32_t raw_template = 0;

   CHECK(d12_agx3_vdm_encode_vertex_pipeline_pair(
      AGX_CHIP_G17C, UINT64_C(0x123456789a80), UINT32_C(0x2abcde),
      &encoded));
   memcpy(&storage[1], encoded.bytes, sizeof(encoded.bytes));
   CHECK(d12_agx3_vdm_decode_vertex_pipeline_pair(
      AGX_CHIP_G15S, &storage[1], sizeof(encoded.bytes), &pipeline,
      &raw_template));
   CHECK(pipeline == UINT64_C(0x123456789a80));
   CHECK(raw_template == UINT32_C(0x2abcde));
   return 0;
}

int
main(void)
{
   if (check_all_supported_targets() || check_boundaries_and_bits() ||
       check_lossless_corruptions() || check_rejections() ||
       check_unaligned())
      return 1;

   printf("d12_agx3_vdm_core: %u checks passed\n", checks);
   return 0;
}
