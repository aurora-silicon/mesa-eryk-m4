/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_format_caps_core.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                    \
   do {                                                                      \
      if (!(condition)) {                                                    \
         fprintf(stderr,                                                     \
                 "d12_format_caps_core_tests: failed at line %d\n",         \
                 __LINE__);                                                  \
         exit(1);                                                            \
      }                                                                      \
   } while (0)

int
main(void)
{
   static const uint32_t required[] = {
      3, 7, 12, 17, 25, 30, 36, 42, 50, 57, 62,
   };
   uint32_t supported_count = 0;

   CHECK(sizeof(required) / sizeof(required[0]) ==
         D12_FORMAT_CORE_LOGIC_OP_COUNT);
   for (uint32_t format = 0; format < 128; ++format) {
      bool expected = false;

      for (uint32_t index = 0;
           index < sizeof(required) / sizeof(required[0]); ++index) {
         if (format == required[index]) {
            expected = true;
            break;
         }
      }

      CHECK(d12_format_core_supports_output_merger_logic_op(format) ==
            expected);
      if (expected)
         ++supported_count;
   }

   CHECK(supported_count == D12_FORMAT_CORE_LOGIC_OP_COUNT);
   CHECK(!d12_format_core_supports_output_merger_logic_op(UINT32_MAX));

   puts("d12_format_caps_core_tests: PASS");

   /* Typed UAV loads. The baseline three are required of every D3D12 device;
    * the additional fifteen are exactly what TypedUAVLoadAdditionalFormats
    * promises, so the list is asserted literally rather than derived. */
   {
      static const uint32_t baseline[] = {41, 42, 43};
      static const uint32_t additional[] = {
         2, 3, 4, 10, 12, 14, 28, 30, 32, 54, 57, 59, 61, 62, 64,
      };

      CHECK(sizeof(baseline) / sizeof(baseline[0]) ==
            D12_FORMAT_CORE_TYPED_UAV_LOAD_BASELINE_COUNT);
      CHECK(sizeof(additional) / sizeof(additional[0]) ==
            D12_FORMAT_CORE_TYPED_UAV_LOAD_ADDITIONAL_COUNT);

      for (unsigned i = 0; i < sizeof(baseline) / sizeof(baseline[0]); i++) {
         CHECK(d12_format_core_requires_typed_uav_load(baseline[i], false));
         CHECK(d12_format_core_requires_typed_uav_load(baseline[i], true));
      }

      for (unsigned i = 0;
           i < sizeof(additional) / sizeof(additional[0]); i++) {
         /* Without the cap these are not promised... */
         CHECK(!d12_format_core_requires_typed_uav_load(additional[i], false));
         /* ...and with it, every one of them is. */
         CHECK(d12_format_core_requires_typed_uav_load(additional[i], true));
      }

      /* Formats outside both lists are never required, with or without the
       * cap: typeless, sRGB, block-compressed and depth all belong here. */
      static const uint32_t never[] = {
         0,   /* UNKNOWN */
         1,   /* R32G32B32A32_TYPELESS */
         29,  /* R8G8B8A8_UNORM_SRGB */
         40,  /* D32_FLOAT */
         71,  /* BC1_UNORM */
         87,  /* B8G8R8A8_UNORM */
      };
      for (unsigned i = 0; i < sizeof(never) / sizeof(never[0]); i++) {
         CHECK(!d12_format_core_requires_typed_uav_load(never[i], false));
         CHECK(!d12_format_core_requires_typed_uav_load(never[i], true));
      }
   }

   return 0;
}
