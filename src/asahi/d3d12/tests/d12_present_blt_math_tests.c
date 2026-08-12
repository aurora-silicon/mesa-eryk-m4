/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_present_blt_math.h"

#include <stdio.h>
#include <stdlib.h>

static void
require_result(bool condition, const char *message)
{
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

static struct d12_present_blt_geometry
base_geometry(void)
{
   return (struct d12_present_blt_geometry){
      .destination_width = 1920,
      .destination_height = 1080,
      .destination_right = 1920,
      .destination_bottom = 1080,
      .source_width = 1920,
      .source_height = 1080,
      .source_right = 1920,
      .source_bottom = 1080,
      .destination_format = 28,
      .source_format = 28,
      .rotation = D12_PRESENT_ROTATION_IDENTITY,
   };
}

int
main(void)
{
   struct d12_present_blt_geometry geometry = base_geometry();
   struct d12_present_blt_classification result;

   require_result(d12_present_blt_classify(&geometry, &result) &&
                     !result.format_conversion && !result.stretch &&
                     !result.rotation && !result.exact_resolve &&
                     !result.shader_blt,
                  "exact copy retains raw fast path");

   geometry.resolve = true;
   require_result(d12_present_blt_classify(&geometry, &result) &&
                     result.exact_resolve && !result.shader_blt,
                  "full same-format resolve retains resolve fast path");

   geometry.source_left = 16;
   geometry.source_right = 1904;
   geometry.destination_left = 16;
   geometry.destination_right = 1904;
   require_result(d12_present_blt_classify(&geometry, &result) &&
                     !result.stretch && !result.exact_resolve &&
                     result.shader_blt,
                  "partial resolve uses one-pass shader path");

   geometry = base_geometry();
   geometry.destination_width = 1080;
   geometry.destination_height = 1920;
   geometry.destination_right = 1080;
   geometry.destination_bottom = 1920;
   geometry.rotation = D12_PRESENT_ROTATION_CCW_90;
   require_result(d12_present_blt_classify(&geometry, &result) &&
                     result.rotated_source_width == 1080 &&
                     result.rotated_source_height == 1920 &&
                     result.rotation && !result.stretch && result.shader_blt,
                  "quarter-turn swaps source extent without stretching");

   geometry.destination_right = 720;
   require_result(d12_present_blt_classify(&geometry, &result) &&
                     result.stretch && result.shader_blt,
                  "rotated scale is classified as compound shader blit");

   geometry = base_geometry();
   geometry.destination_format = 87;
   require_result(d12_present_blt_classify(&geometry, &result) &&
                     result.format_conversion && result.shader_blt,
                  "format conversion uses shader blit");

   geometry = base_geometry();
   geometry.source_right = geometry.source_width + 1;
   require_result(!d12_present_blt_classify(&geometry, &result),
                  "out-of-bounds source rectangle is rejected");
   geometry = base_geometry();
   geometry.rotation = D12_PRESENT_ROTATION_CCW_270 + 1;
   require_result(!d12_present_blt_classify(&geometry, &result),
                  "reserved rotation is rejected");
   require_result(!d12_present_blt_classify(NULL, &result) &&
                     !d12_present_blt_classify(&geometry, NULL),
                  "null classification arguments are rejected");

   puts("d12_present_blt_math_tests: PASS");
   return 0;
}
