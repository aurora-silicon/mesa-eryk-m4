/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_attachment_view_core.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

int
main(void)
{
   struct d12_attachment_view_request request = {
      .width = 1024,
      .height = 256,
      .levels = 11,
      .layers = 8,
      .mip = 3,
      .first_layer = 2,
      .layer_count = 4,
   };
   struct d12_attachment_view view;

   assert(d12_attachment_view_resolve(&request, &view));
   assert(view.width == 128 && view.height == 32 && view.mip == 3);
   assert(view.first_layer == 2 && view.layer_count == 4);

   request.layer_count = UINT32_MAX;
   assert(d12_attachment_view_resolve(&request, &view));
   assert(view.layer_count == 6);

   request.layers = 16;
   request.mip = 2;
   request.first_layer = 1;
   request.mipmapped_layers = true;
   assert(d12_attachment_view_resolve(&request, &view));
   assert(view.width == 256 && view.height == 64);
   assert(view.first_layer == 1 && view.layer_count == 3);

   request = (struct d12_attachment_view_request){
      .width = 512,
      .height = 1,
      .levels = 10,
      .layers = 4,
      .mip = 4,
      .first_layer = 1,
      .layer_count = 2,
   };
   assert(d12_attachment_view_resolve(&request, &view));
   assert(view.width == 32 && view.height == 1);
   assert(view.first_layer == 1 && view.layer_count == 2);

   request.layer_count = 4;
   assert(!d12_attachment_view_resolve(&request, &view));
   request.layer_count = 0;
   assert(!d12_attachment_view_resolve(&request, &view));
   request.layer_count = 1;
   request.first_layer = 4;
   assert(!d12_attachment_view_resolve(&request, &view));
   request.first_layer = 0;
   request.mip = request.levels;
   assert(!d12_attachment_view_resolve(&request, &view));
   request.mip = 0;
   request.width = 0;
   assert(!d12_attachment_view_resolve(&request, &view));
   assert(!d12_attachment_view_resolve(NULL, &view));
   assert(!d12_attachment_view_resolve(&request, NULL));

   puts("d12_attachment_view_core_tests: PASS");
   return 0;
}
