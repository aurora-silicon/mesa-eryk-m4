/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "hk_agx3_ioto_preshader_core.h"

#include "asahi/lib/agx_ioto_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                \
         return 1;                                                             \
      }                                                                        \
   } while (0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const enum agx_chip targets[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static bool
is_gpu1(enum agx_chip chip)
{
   return chip == AGX_CHIP_G17G || chip == AGX_CHIP_G17S ||
          chip == AGX_CHIP_G17C;
}

static int
test_program_size(void)
{
   size_t size = 0xa5a5;
   CHECK(hk_agx3_ioto_preshader_program_size(0x100, 0x180, 0, &size));
   CHECK(size == 0x80);
   CHECK(hk_agx3_ioto_preshader_program_size(0x100, 0x100, 0x345, &size));
   CHECK(size == 0x245);
   size = 0xa5a5;
   CHECK(!hk_agx3_ioto_preshader_program_size(0x100, 0x80, 0xff, &size));
   CHECK(size == 0xa5a5);
   CHECK(!hk_agx3_ioto_preshader_program_size(0, 1, 0, NULL));
   return 0;
}

static int
test_target_modes(void)
{
   static const uint8_t inline_code[] = {
      0x1b, 0x0c, 0x06, 0x00, 0x06, 0x00,
   };
   static const uint8_t all_nops[] = {0x06, 0x00, 0x06, 0x00};

   for (size_t i = 0; i < ARRAY_SIZE(targets); ++i) {
      const enum agx_chip chip = targets[i];
      struct agx_ioto_preshader_desc desc = {
         .code = inline_code,
         .code_size = sizeof(inline_code),
         .external_va = AGX_IOTO_PRESHADER_NO_VA,
      };
      struct hk_agx3_ioto_preshader_encoding output;
      memset(&output, 0xa5, sizeof(output));
      CHECK(hk_agx3_ioto_preshader_generate(chip, &desc, &output));
      CHECK(output.mode == AGX_IOTO_PRESHADER_MODE_INLINE);
      CHECK(output.length == 2);
      CHECK(output.branch_offset == AGX_IOTO_PRESHADER_NO_BRANCH);
      CHECK(memcmp(output.bytes, inline_code, output.length) == 0);
      CHECK(output.bytes[output.length] == 0);

      desc.code = all_nops;
      desc.code_size = sizeof(all_nops);
      CHECK(hk_agx3_ioto_preshader_generate(chip, &desc, &output));
      CHECK(output.mode == AGX_IOTO_PRESHADER_MODE_ABSENT);
      CHECK(output.length == AGX_IOTO_PRESHADER_ABSENT_SIZE);
      CHECK(memcmp(output.bytes, "\x0e\x00\x00\x00", 4) == 0);

      desc.code = NULL;
      desc.code_size = 0;
      desc.force_external = true;
      desc.external_va = UINT64_C(0x123456789abc);
      desc.request_prelude = true;
      CHECK(hk_agx3_ioto_preshader_generate(chip, &desc, &output));
      CHECK(output.mode == AGX_IOTO_PRESHADER_MODE_EXTERNAL);
      CHECK(output.length == AGX_IOTO_PRESHADER_PRELUDE_SIZE +
                                AGX_IOTO_PRESHADER_BRANCH_SIZE);
      CHECK(output.branch_offset == AGX_IOTO_PRESHADER_PRELUDE_SIZE);
      CHECK(output.bytes[0] == (is_gpu1(chip) ? 0x04 : 0x0c));
      CHECK(output.bytes[1] == 0x10);
      CHECK(output.bytes[0] != 0x0e);

      struct agx_ioto_record record;
      size_t consumed;
      CHECK(agx_ioto_validate_kind(
         chip, is_gpu1(chip) ? AGX_IOTO_KIND_B_EP1 : AGX_IOTO_KIND_B_EP0,
         output.bytes + output.branch_offset,
         output.length - output.branch_offset, &record, &consumed));
      CHECK(consumed == AGX_IOTO_PRESHADER_BRANCH_SIZE);
   }

   return 0;
}

static int
test_failure_preserves_output(void)
{
   static const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G14X, AGX_CHIP_G15P,
      AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t code[] = {0x06, 0x00};
   struct agx_ioto_preshader_desc desc = {
      .code = code,
      .code_size = sizeof(code),
      .external_va = AGX_IOTO_PRESHADER_NO_VA,
   };
   struct hk_agx3_ioto_preshader_encoding output;
   memset(&output, 0xa5, sizeof(output));
   const struct hk_agx3_ioto_preshader_encoding before = output;

   for (size_t i = 0; i < ARRAY_SIZE(rejected); ++i) {
      CHECK(!hk_agx3_ioto_preshader_generate(rejected[i], &desc, &output));
      CHECK(memcmp(&output, &before, sizeof(output)) == 0);
   }

   desc.code_size = 1;
   CHECK(!hk_agx3_ioto_preshader_generate(AGX_CHIP_G15G, &desc, &output));
   CHECK(memcmp(&output, &before, sizeof(output)) == 0);
   CHECK(!hk_agx3_ioto_preshader_generate(AGX_CHIP_G15G, NULL, &output));
   CHECK(memcmp(&output, &before, sizeof(output)) == 0);
   CHECK(!hk_agx3_ioto_preshader_generate(AGX_CHIP_G15G, &desc, NULL));
   return 0;
}

static int
test_alias(void)
{
   struct hk_agx3_ioto_preshader_encoding output;
   memset(&output, 0, sizeof(output));
   output.bytes[0] = 0x1b;
   output.bytes[1] = 0x0c;
   output.bytes[2] = 0x06;
   output.bytes[3] = 0x00;
   const struct agx_ioto_preshader_desc desc = {
      .code = output.bytes,
      .code_size = 4,
      .external_va = AGX_IOTO_PRESHADER_NO_VA,
   };
   CHECK(hk_agx3_ioto_preshader_generate(AGX_CHIP_G17G, &desc, &output));
   CHECK(output.mode == AGX_IOTO_PRESHADER_MODE_INLINE);
   CHECK(output.length == 2);
   CHECK(output.bytes[0] == 0x1b && output.bytes[1] == 0x0c);
   return 0;
}

int
main(void)
{
   if (test_program_size() || test_target_modes() ||
       test_failure_preserves_output() || test_alias())
      return 1;

   printf("Honeykrisp AGX3 Ioto preshader core: %u checks passed\n", checks);
   return 0;
}
