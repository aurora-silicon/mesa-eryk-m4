/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_chip.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;
static unsigned checks;

#define CHECK_EQ(actual, expected)                                             \
   do {                                                                        \
      unsigned value = (unsigned)(actual);                                     \
      checks++;                                                                \
      if (value != (unsigned)(expected)) {                                     \
         fprintf(stderr, "%s:%d: got %u, expected %u\n", __FILE__, __LINE__,   \
                 value, (unsigned)(expected));                                 \
         failures++;                                                           \
      }                                                                        \
   } while (0)

#define CHECK(condition)                                                       \
   do {                                                                        \
      checks++;                                                                \
      if (!(condition)) {                                                      \
         fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);       \
         failures++;                                                           \
      }                                                                        \
   } while (0)

struct chip_case {
   uint32_t generation;
   uint32_t variant;
   uint32_t clusters;
   enum agx_chip chip;
   enum agx_isa isa;
   enum agx_isa_encoding encoding;
   const char *name;
   bool ray_tracing;
   bool scheduling_stalls;
};

/*
 * Exact driver-core identities recovered from Apple's readChipInfo table.
 * P/A are not Mac performance tiers: G15P is the A16 AGX2 part, while G17P
 * (A18 Pro) and G17A stay on the AGX3b/HAL200 side of the M5 boundary.
 */
static const struct chip_case chip_cases[] = {
   {13, 'G', 1, AGX_CHIP_G13G, AGX_ISA_AGX2, AGX_ISA_ENCODING_AGX2,
    "G13G", false, false},
   {13, 'S', 2, AGX_CHIP_G13X, AGX_ISA_AGX2, AGX_ISA_ENCODING_AGX2,
    "G13X", false, false},
   {14, 'G', 1, AGX_CHIP_G14G, AGX_ISA_AGX2, AGX_ISA_ENCODING_AGX2,
    "G14G", false, false},
   {14, 'C', 4, AGX_CHIP_G14X, AGX_ISA_AGX2, AGX_ISA_ENCODING_AGX2,
    "G14X", false, false},

   {15, 'P', 1, AGX_CHIP_G15P, AGX_ISA_AGX2, AGX_ISA_ENCODING_AGX2,
    "G15P", false, false},
   {15, 'G', 1, AGX_CHIP_G15G, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G15G", true, true},
   {15, 'S', 2, AGX_CHIP_G15S, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G15S", true, false},
   {15, 'C', 4, AGX_CHIP_G15C, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G15C", true, false},
   {15, 'D', 8, AGX_CHIP_G15D, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G15D", true, false},

   {16, 'P', 1, AGX_CHIP_G16P, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G16P", true, false},
   {16, 'G', 1, AGX_CHIP_G16G, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G16G", true, false},
   {16, 'S', 2, AGX_CHIP_G16S, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G16S", true, false},
   {16, 'C', 4, AGX_CHIP_G16C, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G16C", true, false},

   {17, 'P', 1, AGX_CHIP_G17P, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G17P", true, false},
   {17, 'A', 1, AGX_CHIP_G17A, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3B,
    "G17A", true, false},
   {17, 'G', 1, AGX_CHIP_G17G, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3C,
    "G17G", true, false},
   {17, 'S', 2, AGX_CHIP_G17S, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3C,
    "G17S", true, false},
   {17, 'C', 4, AGX_CHIP_G17C, AGX_ISA_AGX3, AGX_ISA_ENCODING_AGX3C,
    "G17C", true, false},
};

static enum agx_chip
chip_for(uint32_t generation, uint32_t variant, uint32_t clusters)
{
   struct drm_asahi_params_global params = {0};
   params.gpu_generation = generation;
   params.gpu_variant = variant;
   params.num_clusters_total = clusters;
   return agx_chip_from_params(&params);
}

static void
test_recovered_table(void)
{
   bool seen[AGX_CHIP_UNKNOWN + 1] = {0};

   for (unsigned i = 0; i < sizeof(chip_cases) / sizeof(chip_cases[0]); ++i) {
      const struct chip_case *c = &chip_cases[i];

      CHECK_EQ(chip_for(c->generation, c->variant, c->clusters), c->chip);
      CHECK_EQ(agx_chip_isa(c->chip), c->isa);
      CHECK_EQ(agx_chip_isa_encoding(c->chip), c->encoding);
      CHECK(strcmp(agx_chip_name(c->chip), c->name) == 0);
      CHECK(agx_chip_has_hardware_ray_tracing(c->chip) == c->ray_tracing);
      CHECK(agx_chip_needs_scheduling_stalls(c->chip) ==
            c->scheduling_stalls);
      CHECK(agx_chip_codegen_supported(c->chip) ==
            (c->encoding == AGX_ISA_ENCODING_AGX2));
      CHECK(!seen[c->chip]);
      seen[c->chip] = true;
   }

   /* Every named enum must have exactly one row above. */
   for (int chip = AGX_CHIP_G13G; chip < AGX_CHIP_UNKNOWN; ++chip)
      CHECK(seen[chip]);
}

static void
test_variant_is_load_bearing(void)
{
   /* Cluster count must never be used as a substitute for gpu_variant. */
   CHECK_EQ(chip_for(17, 'P', 4), AGX_CHIP_G17P);
   CHECK_EQ(chip_for(17, 'G', 4), AGX_CHIP_G17G);
   CHECK_EQ(chip_for(15, 'P', 2), AGX_CHIP_G15P);
   CHECK_EQ(chip_for(15, 'S', 1), AGX_CHIP_G15S);

   CHECK_EQ(agx_chip_isa(AGX_CHIP_G15P), AGX_ISA_AGX2);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G17P),
            AGX_ISA_ENCODING_AGX3B);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G17A),
            AGX_ISA_ENCODING_AGX3B);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G17G),
            AGX_ISA_ENCODING_AGX3C);
}

static void
test_invalid_identity(void)
{
   CHECK_EQ(chip_for(12, 'G', 1), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(18, 'G', 1), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(15, 'A', 1), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(16, 'D', 8), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(17, 'D', 8), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(13, 'P', 1), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(13, 'G', 0), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(17, 'S', DRM_ASAHI_MAX_CLUSTERS + 1),
            AGX_CHIP_UNKNOWN);
}

static void
test_classification_invariants(void)
{
   CHECK_EQ(AGX_ISA_AGX2, 2);
   CHECK_EQ(AGX_ISA_AGX3, 3);

   for (int chip = AGX_CHIP_G13G; chip <= AGX_CHIP_UNKNOWN; ++chip) {
      const enum agx_chip c = (enum agx_chip)chip;
      const enum agx_isa isa = agx_chip_isa(c);
      const enum agx_isa_encoding encoding = agx_chip_isa_encoding(c);
      const bool known = c != AGX_CHIP_UNKNOWN;
      const bool agx3_encoding = encoding == AGX_ISA_ENCODING_AGX3B ||
                                 encoding == AGX_ISA_ENCODING_AGX3C;

      CHECK(strcmp(agx_chip_name(c), "") != 0);
      CHECK((isa != AGX_ISA_UNKNOWN) == known);
      CHECK((encoding != AGX_ISA_ENCODING_UNKNOWN) == known);
      CHECK(agx3_encoding == (isa == AGX_ISA_AGX3));
      CHECK((encoding == AGX_ISA_ENCODING_AGX2) == (isa == AGX_ISA_AGX2));
      CHECK(agx_chip_has_hardware_ray_tracing(c) == (isa == AGX_ISA_AGX3));
   }

   CHECK(strcmp(agx_isa_name(AGX_ISA_AGX3), "AGX3") == 0);
   CHECK(strcmp(agx_isa_encoding_name(AGX_ISA_ENCODING_AGX3B), "agx3b") ==
         0);
   CHECK(strcmp(agx_isa_encoding_name(AGX_ISA_ENCODING_AGX3C), "agx3c") ==
         0);
   CHECK(strcmp(agx_chip_name(AGX_CHIP_UNKNOWN), "unknown") == 0);
}

int
main(void)
{
   test_recovered_table();
   test_variant_is_load_bearing();
   test_invalid_identity();
   test_classification_invariants();

   if (failures) {
      fprintf(stderr, "agx_chip: %u of %u checks failed\n", failures, checks);
      return 1;
   }

   printf("agx_chip: all %u checks passed\n", checks);
   return 0;
}
