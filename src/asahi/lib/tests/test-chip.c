/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_chip.h"

#include <stdio.h>

static unsigned failures;

#define CHECK_EQ(actual, expected)                                             \
   do {                                                                        \
      enum agx_chip value = (actual);                                          \
      if (value != (expected)) {                                               \
         fprintf(stderr, "%s:%d: got %u, expected %u\n", __FILE__, __LINE__, \
                 (unsigned)value, (unsigned)(expected));                       \
         failures++;                                                           \
      }                                                                        \
   } while (0)

int
main(void)
{
   struct drm_asahi_params_global params = {0};

   params.gpu_generation = 13;
   params.num_clusters_total = 1;
   CHECK_EQ(agx_chip_from_params(&params), AGX_CHIP_G13G);
   params.num_clusters_total = 2;
   CHECK_EQ(agx_chip_from_params(&params), AGX_CHIP_G13X);

   params.gpu_generation = 14;
   params.num_clusters_total = 1;
   CHECK_EQ(agx_chip_from_params(&params), AGX_CHIP_G14G);
   params.num_clusters_total = DRM_ASAHI_MAX_CLUSTERS;
   CHECK_EQ(agx_chip_from_params(&params), AGX_CHIP_G14X);

   params.gpu_generation = 15;
   CHECK_EQ(agx_chip_from_params(&params), AGX_CHIP_UNKNOWN);
   params.gpu_generation = 12;
   CHECK_EQ(agx_chip_from_params(&params), AGX_CHIP_UNKNOWN);
   params.gpu_generation = 13;
   params.num_clusters_total = 0;
   CHECK_EQ(agx_chip_from_params(&params), AGX_CHIP_UNKNOWN);
   params.num_clusters_total = DRM_ASAHI_MAX_CLUSTERS + 1;
   CHECK_EQ(agx_chip_from_params(&params), AGX_CHIP_UNKNOWN);

   if (failures)
      return 1;

   puts("agx_chip: all checks passed");
   return 0;
}
