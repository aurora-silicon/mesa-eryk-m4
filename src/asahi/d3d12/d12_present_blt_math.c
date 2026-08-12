/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_present_blt_math.h"

bool
d12_present_blt_classify(
   const struct d12_present_blt_geometry *geometry,
   struct d12_present_blt_classification *classification)
{
   if (!geometry || !classification || !geometry->destination_width ||
       !geometry->destination_height || !geometry->source_width ||
       !geometry->source_height ||
       geometry->destination_right <= geometry->destination_left ||
       geometry->destination_bottom <= geometry->destination_top ||
       geometry->destination_right > geometry->destination_width ||
       geometry->destination_bottom > geometry->destination_height ||
       geometry->source_right <= geometry->source_left ||
       geometry->source_bottom <= geometry->source_top ||
       geometry->source_right > geometry->source_width ||
       geometry->source_bottom > geometry->source_height ||
       geometry->rotation > D12_PRESENT_ROTATION_CCW_270)
      return false;

   const uint32_t destination_rect_width =
      geometry->destination_right - geometry->destination_left;
   const uint32_t destination_rect_height =
      geometry->destination_bottom - geometry->destination_top;
   const uint32_t source_rect_width =
      geometry->source_right - geometry->source_left;
   const uint32_t source_rect_height =
      geometry->source_bottom - geometry->source_top;
   const bool quarter_turn =
      geometry->rotation == D12_PRESENT_ROTATION_CCW_90 ||
      geometry->rotation == D12_PRESENT_ROTATION_CCW_270;
   const bool rotation =
      geometry->rotation != D12_PRESENT_ROTATION_UNSPECIFIED &&
      geometry->rotation != D12_PRESENT_ROTATION_IDENTITY;
   const bool format_conversion =
      geometry->destination_format != geometry->source_format;
   const uint32_t rotated_source_width =
      quarter_turn ? source_rect_height : source_rect_width;
   const uint32_t rotated_source_height =
      quarter_turn ? source_rect_width : source_rect_height;
   const bool stretch = destination_rect_width != rotated_source_width ||
                        destination_rect_height != rotated_source_height;
   const bool exact_resolve =
      geometry->resolve && !format_conversion && !stretch && !rotation &&
      geometry->destination_left == 0 && geometry->destination_top == 0 &&
      geometry->destination_right == geometry->destination_width &&
      geometry->destination_bottom == geometry->destination_height &&
      geometry->source_left == 0 && geometry->source_top == 0 &&
      geometry->source_right == geometry->source_width &&
      geometry->source_bottom == geometry->source_height;

   *classification = (struct d12_present_blt_classification){
      .rotated_source_width = rotated_source_width,
      .rotated_source_height = rotated_source_height,
      .format_conversion = format_conversion,
      .stretch = stretch,
      .rotation = rotation,
      .exact_resolve = exact_resolve,
      .shader_blt = format_conversion || stretch || rotation ||
                    (geometry->resolve && !exact_resolve),
   };
   return true;
}
