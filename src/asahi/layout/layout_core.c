/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "layout_core.h"
#include "util/macros.h"

/* Every maximum tile occupies one 16 KiB GPU page. */
struct ail_tile
ail_get_max_tile_size(unsigned blocksize_B)
{
   /* clang-format off */
   switch (blocksize_B) {
   case   1: return (struct ail_tile) { 128, 128 };
   case   2: return (struct ail_tile) { 128,  64 };
   case   4: return (struct ail_tile) {  64,  64 };
   case   8: return (struct ail_tile) {  64,  32 };
   case  16: return (struct ail_tile) {  32,  32 };
   case  32: return (struct ail_tile) {  32,  16 };
   case  64: return (struct ail_tile) {  16,  16 };
   case 128: return (struct ail_tile) {  16,   8 };
   case 256: return (struct ail_tile) {   8,   8 };
   default: UNREACHABLE("Invalid blocksize");
   }
   /* clang-format on */
}
