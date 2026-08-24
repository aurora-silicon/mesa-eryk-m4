/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "layout_core.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;

#define CHECK_EQ(actual, expected)                                             \
   do {                                                                        \
      unsigned long long a_ = (actual);                                         \
      unsigned long long e_ = (expected);                                       \
      ++checks;                                                                 \
      if (a_ != e_) {                                                           \
         fprintf(stderr, "%s:%d: got %llu, expected %llu\n", __FILE__,       \
                 __LINE__, a_, e_);                                             \
         exit(1);                                                               \
      }                                                                         \
   } while (0)

int
main(void)
{
   CHECK_EQ(ail_effective_width_sa(37, 1), 37);
   CHECK_EQ(ail_effective_height_sa(19, 1), 19);
   CHECK_EQ(ail_effective_width_sa(37, 2), 37);
   CHECK_EQ(ail_effective_height_sa(19, 2), 38);
   CHECK_EQ(ail_effective_width_sa(37, 4), 74);
   CHECK_EQ(ail_effective_height_sa(19, 4), 38);
   CHECK_EQ(ail_effective_width_sa(37, 8), 74);
   CHECK_EQ(ail_effective_height_sa(19, 8), 76);

   struct ail_tile rgba16f = ail_get_max_tile_size(128);
   CHECK_EQ(rgba16f.width_el, 16);
   CHECK_EQ(rgba16f.height_el, 8);
   CHECK_EQ(rgba16f.width_el * rgba16f.height_el * 128, 0x4000);

   struct ail_tile rgba32f = ail_get_max_tile_size(256);
   CHECK_EQ(rgba32f.width_el, 8);
   CHECK_EQ(rgba32f.height_el, 8);
   CHECK_EQ(rgba32f.width_el * rgba32f.height_el * 256, 0x4000);

   printf("agx3 layout: all %u checks passed\n", checks);
   return 0;
}
