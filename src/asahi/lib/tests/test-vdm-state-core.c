/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_vdm_state_core.h"

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
check_zero_encoding(const struct agx_vdm_state_vertex_pipeline_pair *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_witnesses(void)
{
   static const uint8_t expected[] = {
      0xde, 0xbc, 0x2a, 0x12, 0x6a, 0xe2, 0x59, 0xd1,
   };

   for (unsigned i = 0; i < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++i) {
      struct agx_vdm_state_vertex_pipeline_pair encoded;
      uint64_t pipeline = 0;
      uint32_t raw_template = 0;

      CHECK(agx_vdm_state_vertex_pipeline_pair_supported(supported_chips[i]));
      CHECK(agx_vdm_state_vertex_pipeline_pair_pack(
         supported_chips[i], UINT64_C(0x123456789a80), UINT32_C(0x2abcde),
         &encoded));
      CHECK(encoded.length == sizeof(expected));
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(agx_vdm_state_vertex_pipeline_pair_unpack(
         supported_chips[i], expected, sizeof(expected), &pipeline,
         &raw_template));
      CHECK(pipeline == UINT64_C(0x123456789a80));
      CHECK(raw_template == UINT32_C(0x2abcde));
   }

   return 0;
}

static int
check_domains(void)
{
   static const uint64_t pipelines[] = {
      0,
      UINT64_C(0x40),
      UINT64_C(0x123456789a80),
      UINT64_C(0xffffffffffc0),
   };
   static const uint32_t templates[] = {
      0,
      1,
      UINT32_C(0x2abcde),
      AGX_VDM_STATE_VERTEX_PIPELINE_TEMPLATE_MASK,
   };

   for (unsigned c = 0; c < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++c) {
      for (unsigned p = 0; p < sizeof(pipelines) / sizeof(pipelines[0]); ++p) {
         for (unsigned t = 0; t < sizeof(templates) / sizeof(templates[0]);
              ++t) {
            struct agx_vdm_state_vertex_pipeline_pair encoded;
            uint64_t decoded_pipeline = UINT64_MAX;
            uint32_t decoded_template = UINT32_MAX;

            CHECK(agx_vdm_state_vertex_pipeline_pair_pack(
               supported_chips[c], pipelines[p], templates[t], &encoded));
            CHECK(encoded.length ==
                  AGX_VDM_STATE_VERTEX_PIPELINE_PAIR_LENGTH);
            CHECK(agx_vdm_state_vertex_pipeline_pair_unpack(
               supported_chips[c], encoded.bytes, encoded.length,
               &decoded_pipeline, &decoded_template));
            CHECK(decoded_pipeline == pipelines[p]);
            CHECK(decoded_template == templates[t]);
         }
      }
   }

   /* Exercise every represented address and raw-template bit independently. */
   for (unsigned bit = 6; bit < 48; ++bit) {
      struct agx_vdm_state_vertex_pipeline_pair encoded;
      uint64_t decoded_pipeline = 0;
      uint32_t decoded_template = 0;
      const uint64_t pipeline = UINT64_C(1) << bit;

      CHECK(agx_vdm_state_vertex_pipeline_pair_pack(
         AGX_CHIP_G17S, pipeline, 0, &encoded));
      CHECK(agx_vdm_state_vertex_pipeline_pair_unpack(
         AGX_CHIP_G17S, encoded.bytes, encoded.length, &decoded_pipeline,
         &decoded_template));
      CHECK(decoded_pipeline == pipeline);
      CHECK(decoded_template == 0);
   }

   for (unsigned bit = 0; bit < 22; ++bit) {
      struct agx_vdm_state_vertex_pipeline_pair encoded;
      uint64_t decoded_pipeline = UINT64_MAX;
      uint32_t decoded_template = 0;
      const uint32_t raw_template = UINT32_C(1) << bit;

      CHECK(agx_vdm_state_vertex_pipeline_pair_pack(
         AGX_CHIP_G15G, 0, raw_template, &encoded));
      CHECK(agx_vdm_state_vertex_pipeline_pair_unpack(
         AGX_CHIP_G15G, encoded.bytes, encoded.length, &decoded_pipeline,
         &decoded_template));
      CHECK(decoded_pipeline == 0);
      CHECK(decoded_template == raw_template);
   }

   return 0;
}

static int
check_lossless_bit_mapping(void)
{
   struct agx_vdm_state_vertex_pipeline_pair baseline;
   CHECK(agx_vdm_state_vertex_pipeline_pair_pack(
      AGX_CHIP_G17S, UINT64_C(0x123456789a80), UINT32_C(0x2abcde),
      &baseline));

   /* Every one of the 64 bits is assigned either to the 42-bit address or to
    * the opaque 22-bit template.  A corruption cannot be rejected in
    * isolation, so prove that decode/repack preserves each changed bit. */
   for (unsigned bit = 0; bit < 64; ++bit) {
      uint8_t changed[AGX_VDM_STATE_VERTEX_PIPELINE_PAIR_LENGTH];
      struct agx_vdm_state_vertex_pipeline_pair repacked;
      uint64_t pipeline = 0;
      uint32_t raw_template = 0;

      memcpy(changed, baseline.bytes, sizeof(changed));
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
      CHECK(agx_vdm_state_vertex_pipeline_pair_unpack(
         AGX_CHIP_G17S, changed, sizeof(changed), &pipeline, &raw_template));
      CHECK(agx_vdm_state_vertex_pipeline_pair_pack(
         AGX_CHIP_G17S, pipeline, raw_template, &repacked));
      CHECK(memcmp(changed, repacked.bytes, sizeof(changed)) == 0);
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   struct agx_vdm_state_vertex_pipeline_pair encoded;
   static const uint8_t witness[AGX_VDM_STATE_VERTEX_PIPELINE_PAIR_LENGTH] = {
      0xde, 0xbc, 0x2a, 0x12, 0x6a, 0xe2, 0x59, 0xd1,
   };

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      uint64_t pipeline = UINT64_MAX;
      uint32_t raw_template = UINT32_MAX;

      CHECK(!agx_vdm_state_vertex_pipeline_pair_supported(
         unsupported_chips[i]));
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_state_vertex_pipeline_pair_pack(
         unsupported_chips[i], UINT64_C(0x123456789a80),
         UINT32_C(0x2abcde), &encoded));
      if (check_zero_encoding(&encoded))
         return 1;

      CHECK(!agx_vdm_state_vertex_pipeline_pair_unpack(
         unsupported_chips[i], witness, sizeof(witness), &pipeline,
         &raw_template));
      CHECK(pipeline == 0);
      CHECK(raw_template == 0);
   }

   static const uint64_t invalid_pipelines[] = {
      1,
      63,
      UINT64_C(0x123456789a81),
      UINT64_C(1) << 48,
      UINT64_MAX,
   };
   for (unsigned i = 0;
        i < sizeof(invalid_pipelines) / sizeof(invalid_pipelines[0]); ++i) {
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_state_vertex_pipeline_pair_pack(
         AGX_CHIP_G17S, invalid_pipelines[i], 0, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   static const uint32_t invalid_templates[] = {
      UINT32_C(1) << 22,
      UINT32_C(0x00400001),
      UINT32_MAX,
   };
   for (unsigned i = 0;
        i < sizeof(invalid_templates) / sizeof(invalid_templates[0]); ++i) {
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_state_vertex_pipeline_pair_pack(
         AGX_CHIP_G15G, 0, invalid_templates[i], &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   CHECK(!agx_vdm_state_vertex_pipeline_pair_pack(
      AGX_CHIP_G15G, 0, 0, NULL));

   for (size_t size = 0; size < sizeof(witness); ++size) {
      uint64_t pipeline = UINT64_MAX;
      uint32_t raw_template = UINT32_MAX;
      CHECK(!agx_vdm_state_vertex_pipeline_pair_unpack(
         AGX_CHIP_G15G, witness, size, &pipeline, &raw_template));
      CHECK(pipeline == 0);
      CHECK(raw_template == 0);
   }

   const size_t trailing_sizes[] = {9, 16, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      uint64_t pipeline = UINT64_MAX;
      uint32_t raw_template = UINT32_MAX;
      CHECK(!agx_vdm_state_vertex_pipeline_pair_unpack(
         AGX_CHIP_G15G, witness, trailing_sizes[i], &pipeline,
         &raw_template));
      CHECK(pipeline == 0);
      CHECK(raw_template == 0);
   }

   uint64_t pipeline = UINT64_MAX;
   uint32_t raw_template = UINT32_MAX;
   CHECK(!agx_vdm_state_vertex_pipeline_pair_unpack(
      AGX_CHIP_G15G, NULL, sizeof(witness), &pipeline, &raw_template));
   CHECK(pipeline == 0);
   CHECK(raw_template == 0);

   raw_template = UINT32_MAX;
   CHECK(!agx_vdm_state_vertex_pipeline_pair_unpack(
      AGX_CHIP_G15G, witness, sizeof(witness), NULL, &raw_template));
   CHECK(raw_template == 0);

   pipeline = UINT64_MAX;
   CHECK(!agx_vdm_state_vertex_pipeline_pair_unpack(
      AGX_CHIP_G15G, witness, sizeof(witness), &pipeline, NULL));
   CHECK(pipeline == 0);

   return 0;
}

static int
check_unaligned_and_cross_target(void)
{
   uint8_t storage[AGX_VDM_STATE_VERTEX_PIPELINE_PAIR_LENGTH + 1] = {0};
   struct agx_vdm_state_vertex_pipeline_pair encoded;
   uint64_t pipeline = 0;
   uint32_t raw_template = 0;

   CHECK(agx_vdm_state_vertex_pipeline_pair_pack(
      AGX_CHIP_G15G, UINT64_C(0x123456789a80), UINT32_C(0x2abcde),
      &encoded));
   memcpy(&storage[1], encoded.bytes, sizeof(encoded.bytes));
   CHECK(agx_vdm_state_vertex_pipeline_pair_unpack(
      AGX_CHIP_G17C, &storage[1], sizeof(encoded.bytes), &pipeline,
      &raw_template));
   CHECK(pipeline == UINT64_C(0x123456789a80));
   CHECK(raw_template == UINT32_C(0x2abcde));

   return 0;
}

int
main(void)
{
   if (check_exact_witnesses() || check_domains() ||
       check_lossless_bit_mapping() || check_rejections_and_clearing() ||
       check_unaligned_and_cross_target())
      return 1;

   printf("agx_vdm_state_core: %u checks passed\n", checks);
   return 0;
}
