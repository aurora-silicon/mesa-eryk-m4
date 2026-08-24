/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_vdm_state_update_core.h"

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
   (enum agx_chip)999,
};

static const struct agx_vdm_state_update_info witness_info = {
   .raw_vertex_shader_word0 = UINT32_C(0x11223344),
   .pipeline = UINT64_C(0x123456789a80),
   .raw_pipeline_template = UINT32_C(0x2abcde),
   .raw_vertex_outputs = {UINT32_C(0x55667788), UINT32_C(0x99aabbcc)},
   .raw_vertex_unknown = UINT32_C(0xddeeff00),
};

static const uint8_t witness[AGX_VDM_STATE_UPDATE_LENGTH] = {
   0x2e, 0x00, 0x00, 0x40, 0x44, 0x33, 0x22, 0x11,
   0xde, 0xbc, 0x2a, 0x12, 0x6a, 0xe2, 0x59, 0xd1,
   0x88, 0x77, 0x66, 0x55, 0xcc, 0xbb, 0xaa, 0x99,
   0x00, 0xff, 0xee, 0xdd,
};

static int
check_info_equal(const struct agx_vdm_state_update_info *actual,
                 const struct agx_vdm_state_update_info *expected)
{
   CHECK(actual->raw_vertex_shader_word0 ==
         expected->raw_vertex_shader_word0);
   CHECK(actual->pipeline == expected->pipeline);
   CHECK(actual->raw_pipeline_template == expected->raw_pipeline_template);
   CHECK(actual->raw_vertex_outputs[0] == expected->raw_vertex_outputs[0]);
   CHECK(actual->raw_vertex_outputs[1] == expected->raw_vertex_outputs[1]);
   CHECK(actual->raw_vertex_unknown == expected->raw_vertex_unknown);
   return 0;
}

static int
check_zero_info(const struct agx_vdm_state_update_info *info)
{
   const struct agx_vdm_state_update_info zero = {0};
   return check_info_equal(info, &zero);
}

static int
check_zero_encoding(const struct agx_vdm_state_update *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_witnesses(void)
{
   for (unsigned i = 0; i < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++i) {
      struct agx_vdm_state_update encoded;
      struct agx_vdm_state_update_info decoded;

      CHECK(agx_vdm_state_update_supported(supported_chips[i]));
      CHECK(agx_vdm_state_update_pack(supported_chips[i], &witness_info,
                                      &encoded));
      CHECK(encoded.length == sizeof(witness));
      CHECK(memcmp(encoded.bytes, witness, sizeof(witness)) == 0);
      CHECK(agx_vdm_state_update_unpack(supported_chips[i], witness,
                                        sizeof(witness), &decoded));
      if (check_info_equal(&decoded, &witness_info))
         return 1;
   }

   return 0;
}

static int
check_field_domains(void)
{
   for (unsigned bit = 6; bit < 48; ++bit) {
      struct agx_vdm_state_update_info info = {0};
      struct agx_vdm_state_update_info decoded;
      struct agx_vdm_state_update encoded;
      info.pipeline = UINT64_C(1) << bit;
      CHECK(agx_vdm_state_update_pack(AGX_CHIP_G17S, &info, &encoded));
      CHECK(agx_vdm_state_update_unpack(AGX_CHIP_G17S, encoded.bytes,
                                        encoded.length, &decoded));
      if (check_info_equal(&decoded, &info))
         return 1;
   }

   for (unsigned bit = 0; bit < 22; ++bit) {
      struct agx_vdm_state_update_info info = {0};
      struct agx_vdm_state_update_info decoded;
      struct agx_vdm_state_update encoded;
      info.raw_pipeline_template = UINT32_C(1) << bit;
      CHECK(agx_vdm_state_update_pack(AGX_CHIP_G15G, &info, &encoded));
      CHECK(agx_vdm_state_update_unpack(AGX_CHIP_G15G, encoded.bytes,
                                        encoded.length, &decoded));
      if (check_info_equal(&decoded, &info))
         return 1;
   }

   /* Exercise every bit of all four completely raw payload words. */
   for (unsigned word = 0; word < 4; ++word) {
      for (unsigned bit = 0; bit < 32; ++bit) {
         struct agx_vdm_state_update_info info = {0};
         struct agx_vdm_state_update_info decoded;
         struct agx_vdm_state_update encoded;
         uint32_t *raw_word = NULL;

         switch (word) {
         case 0:
            raw_word = &info.raw_vertex_shader_word0;
            break;
         case 1:
         case 2:
            raw_word = &info.raw_vertex_outputs[word - 1];
            break;
         case 3:
            raw_word = &info.raw_vertex_unknown;
            break;
         default:
            CHECK(false);
         }

         *raw_word = UINT32_C(1) << bit;
         CHECK(agx_vdm_state_update_pack(AGX_CHIP_G16C, &info, &encoded));
         CHECK(agx_vdm_state_update_unpack(AGX_CHIP_G16C, encoded.bytes,
                                           encoded.length, &decoded));
         if (check_info_equal(&decoded, &info))
            return 1;
      }
   }

   return 0;
}

static int
check_every_bit_classification(void)
{
   for (unsigned bit = 0; bit < AGX_VDM_STATE_UPDATE_LENGTH * 8u; ++bit) {
      uint8_t changed[AGX_VDM_STATE_UPDATE_LENGTH];
      struct agx_vdm_state_update_info decoded;
      struct agx_vdm_state_update repacked;
      memcpy(changed, witness, sizeof(changed));
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
      memset(&decoded, 0xa5, sizeof(decoded));

      const bool accepted = agx_vdm_state_update_unpack(
         AGX_CHIP_G17S, changed, sizeof(changed), &decoded);
      if (bit < 32) {
         CHECK(!accepted);
         if (check_zero_info(&decoded))
            return 1;
      } else {
         CHECK(accepted);
         CHECK(agx_vdm_state_update_pack(AGX_CHIP_G17S, &decoded, &repacked));
         CHECK(memcmp(changed, repacked.bytes, sizeof(changed)) == 0);
      }
   }

   return 0;
}

static int
check_rejections(void)
{
   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct agx_vdm_state_update encoded;
      struct agx_vdm_state_update_info decoded;
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_state_update_supported(unsupported_chips[i]));
      CHECK(!agx_vdm_state_update_pack(unsupported_chips[i], &witness_info,
                                       &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_vdm_state_update_unpack(unsupported_chips[i], witness,
                                         sizeof(witness), &decoded));
      if (check_zero_info(&decoded))
         return 1;
   }

   static const uint64_t invalid_pipelines[] = {
      1, 63, UINT64_C(0x123456789a81), UINT64_C(1) << 48, UINT64_MAX,
   };
   for (unsigned i = 0;
        i < sizeof(invalid_pipelines) / sizeof(invalid_pipelines[0]); ++i) {
      struct agx_vdm_state_update_info info = witness_info;
      struct agx_vdm_state_update encoded;
      info.pipeline = invalid_pipelines[i];
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_state_update_pack(AGX_CHIP_G17S, &info, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   static const uint32_t invalid_templates[] = {
      UINT32_C(1) << 22, UINT32_C(0x00400001), UINT32_MAX,
   };
   for (unsigned i = 0;
        i < sizeof(invalid_templates) / sizeof(invalid_templates[0]); ++i) {
      struct agx_vdm_state_update_info info = witness_info;
      struct agx_vdm_state_update encoded;
      info.raw_pipeline_template = invalid_templates[i];
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_state_update_pack(AGX_CHIP_G15G, &info, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   {
      struct agx_vdm_state_update encoded;
      CHECK(!agx_vdm_state_update_pack(AGX_CHIP_G15G, NULL, &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      CHECK(!agx_vdm_state_update_pack(AGX_CHIP_G15G, &witness_info, NULL));
   }

   for (size_t size = 0; size < sizeof(witness); ++size) {
      struct agx_vdm_state_update_info decoded;
      CHECK(!agx_vdm_state_update_unpack(AGX_CHIP_G17S, witness, size,
                                         &decoded));
      if (check_zero_info(&decoded))
         return 1;
   }

   {
      struct agx_vdm_state_update_info decoded;
      uint8_t unaligned[AGX_VDM_STATE_UPDATE_LENGTH + 1];
      memcpy(&unaligned[1], witness, sizeof(witness));
      CHECK(agx_vdm_state_update_unpack(AGX_CHIP_G17S, &unaligned[1],
                                        sizeof(witness), &decoded));
      if (check_info_equal(&decoded, &witness_info))
         return 1;
      CHECK(!agx_vdm_state_update_unpack(AGX_CHIP_G17S, witness,
                                         sizeof(witness) + 1, &decoded));
      if (check_zero_info(&decoded))
         return 1;
      CHECK(!agx_vdm_state_update_unpack(AGX_CHIP_G17S, NULL,
                                         sizeof(witness), &decoded));
      if (check_zero_info(&decoded))
         return 1;
      CHECK(!agx_vdm_state_update_unpack(AGX_CHIP_G17S, witness,
                                         sizeof(witness), NULL));
   }

   return 0;
}

int
main(void)
{
   if (check_exact_witnesses() || check_field_domains() ||
       check_every_bit_classification() || check_rejections())
      return 1;

   printf("agx_vdm_state_update_core: %u checks passed\n", checks);
   return 0;
}
