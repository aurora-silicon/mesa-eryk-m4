/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_attachment_view_core.h"

#include <limits.h>

static uint32_t
d12_attachment_minify(uint32_t value, uint32_t level)
{
   if (level >= 32)
      return 1;
   value >>= level;
   return value ? value : 1;
}

bool
d12_attachment_view_resolve(
   const struct d12_attachment_view_request *request,
   struct d12_attachment_view *view)
{
   if (!request || !view || !request->width || !request->height ||
       !request->levels || !request->layers || request->mip >= request->levels)
      return false;

   const uint32_t available_layers =
      request->mipmapped_layers
         ? d12_attachment_minify(request->layers, request->mip)
         : request->layers;
   if (request->first_layer >= available_layers)
      return false;

   const uint32_t layer_count =
      request->layer_count == UINT32_MAX
         ? available_layers - request->first_layer
         : request->layer_count;
   if (!layer_count ||
       layer_count > available_layers - request->first_layer)
      return false;

   *view = (struct d12_attachment_view){
      .width = d12_attachment_minify(request->width, request->mip),
      .height = d12_attachment_minify(request->height, request->mip),
      .mip = request->mip,
      .first_layer = request->first_layer,
      .layer_count = layer_count,
   };
   return true;
}
