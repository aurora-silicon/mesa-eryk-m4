/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral presentation-blit classification.  DXGI and the public
 * frontend use different object ABIs, but both need identical decisions about
 * whether a blit is an exact copy/resolve or requires the one-pass shader path.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum d12_present_rotation {
   D12_PRESENT_ROTATION_UNSPECIFIED = 0,
   D12_PRESENT_ROTATION_IDENTITY = 1,
   D12_PRESENT_ROTATION_CCW_90 = 2,
   D12_PRESENT_ROTATION_CCW_180 = 3,
   D12_PRESENT_ROTATION_CCW_270 = 4,
};

struct d12_present_blt_geometry {
   uint32_t destination_width;
   uint32_t destination_height;
   uint32_t destination_left;
   uint32_t destination_top;
   uint32_t destination_right;
   uint32_t destination_bottom;
   uint32_t source_width;
   uint32_t source_height;
   uint32_t source_left;
   uint32_t source_top;
   uint32_t source_right;
   uint32_t source_bottom;
   uint32_t destination_format;
   uint32_t source_format;
   uint32_t rotation;
   bool resolve;
};

struct d12_present_blt_classification {
   uint32_t rotated_source_width;
   uint32_t rotated_source_height;
   bool format_conversion;
   bool stretch;
   bool rotation;
   bool exact_resolve;
   bool shader_blt;
};

bool d12_present_blt_classify(
   const struct d12_present_blt_geometry *geometry,
   struct d12_present_blt_classification *classification);
