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

static enum agx_chip
chip_for(uint32_t generation, uint32_t clusters)
{
   struct drm_asahi_params_global params = {0};
   params.gpu_generation = generation;
   params.num_clusters_total = clusters;
   return agx_chip_from_params(&params);
}

static void
test_identification(void)
{
   CHECK_EQ(chip_for(13, 1), AGX_CHIP_G13G);
   CHECK_EQ(chip_for(13, 2), AGX_CHIP_G13X);
   CHECK_EQ(chip_for(14, 1), AGX_CHIP_G14G);
   CHECK_EQ(chip_for(14, DRM_ASAHI_MAX_CLUSTERS), AGX_CHIP_G14X);

   /* Recognised from M3 on, so an unsupported part is refused by name. */
   CHECK_EQ(chip_for(15, 1), AGX_CHIP_G15G);
   CHECK_EQ(chip_for(15, 2), AGX_CHIP_G15X);
   CHECK_EQ(chip_for(16, 1), AGX_CHIP_G16G);
   CHECK_EQ(chip_for(16, 4), AGX_CHIP_G16X);
   CHECK_EQ(chip_for(17, 1), AGX_CHIP_G17G);
   CHECK_EQ(chip_for(17, DRM_ASAHI_MAX_CLUSTERS), AGX_CHIP_G17X);

   /* Neither a generation the driver has never seen nor a nonsensical
    * cluster count may resolve to a chip. */
   CHECK_EQ(chip_for(12, 1), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(18, 1), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(0, 1), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(13, 0), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(13, DRM_ASAHI_MAX_CLUSTERS + 1), AGX_CHIP_UNKNOWN);
   CHECK_EQ(chip_for(17, 0), AGX_CHIP_UNKNOWN);
}

static void
test_instruction_set(void)
{
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G13G), AGX_ISA_AGX2);
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G13X), AGX_ISA_AGX2);
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G14G), AGX_ISA_AGX2);
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G14X), AGX_ISA_AGX2);

   /* M3 is where the instruction set changes. */
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G15G), AGX_ISA_AGX3);
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G15X), AGX_ISA_AGX3);
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G16G), AGX_ISA_AGX3);
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G16X), AGX_ISA_AGX3);
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G17G), AGX_ISA_AGX3);
   CHECK_EQ(agx_chip_isa(AGX_CHIP_G17X), AGX_ISA_AGX3);

   CHECK_EQ(agx_chip_isa(AGX_CHIP_UNKNOWN), AGX_ISA_UNKNOWN);

   /* The enumerators are Apple's own generation numbers, which is what the
    * low nibble of an applegpu Mach-O cpusubtype carries. */
   CHECK_EQ(AGX_ISA_AGX2, 2);
   CHECK_EQ(AGX_ISA_AGX3, 3);
}

/*
 * The encoding a part actually executes.
 *
 * Feeding hardware an instruction stream from the wrong encoding of the right
 * instruction set is a silent mis-execution, not a fault, so this mapping has
 * to be measured rather than inferred from the naming scheme -- and it was:
 * every AGX3 family decodes 100% under exactly one of Apple's two tables.
 */
static void
test_encoding(void)
{
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G13G), AGX_ISA_ENCODING_AGX2);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G13X), AGX_ISA_ENCODING_AGX2);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G14G), AGX_ISA_ENCODING_AGX2);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G14X), AGX_ISA_ENCODING_AGX2);

   /* M3 and M4 share one encoder table, across every die. */
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G15G), AGX_ISA_ENCODING_AGX3B);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G15X), AGX_ISA_ENCODING_AGX3B);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G16G), AGX_ISA_ENCODING_AGX3B);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G16X), AGX_ISA_ENCODING_AGX3B);

   /* M5 is the other. */
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G17G), AGX_ISA_ENCODING_AGX3C);
   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G17X), AGX_ISA_ENCODING_AGX3C);

   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_UNKNOWN), AGX_ISA_ENCODING_UNKNOWN);

   /* The two AGX3 encodings are distinct tables, not aliases. */
   CHECK(AGX_ISA_ENCODING_AGX3B != AGX_ISA_ENCODING_AGX3C);
   CHECK(strcmp(agx_isa_encoding_name(AGX_ISA_ENCODING_AGX3B), "agx3b") == 0);
   CHECK(strcmp(agx_isa_encoding_name(AGX_ISA_ENCODING_AGX3C), "agx3c") == 0);
   CHECK(strcmp(agx_isa_encoding_name(AGX_ISA_ENCODING_AGX2), "agx2") == 0);
   CHECK(strcmp(agx_isa_encoding_name(AGX_ISA_ENCODING_UNKNOWN), "unknown") == 0);

   /* An encoding never spans instruction sets. */
   for (int chip = AGX_CHIP_G13G; chip <= AGX_CHIP_UNKNOWN; chip++) {
      const enum agx_chip c = (enum agx_chip)chip;
      const bool agx3 = agx_chip_isa_encoding(c) == AGX_ISA_ENCODING_AGX3B ||
                        agx_chip_isa_encoding(c) == AGX_ISA_ENCODING_AGX3C;

      CHECK(agx3 == (agx_chip_isa(c) == AGX_ISA_AGX3));
      CHECK((agx_chip_isa_encoding(c) == AGX_ISA_ENCODING_AGX2) ==
            (agx_chip_isa(c) == AGX_ISA_AGX2));
   }
}

/*
 * Code generation is gated on having a packing table, which is a different
 * question from having recognised the part. A device open that tests for
 * UNKNOWN instead would hand a recognised AGX3 part AGX2 instructions.
 */
static void
test_codegen_gate(void)
{
   CHECK(agx_chip_codegen_supported(AGX_CHIP_G13G));
   CHECK(agx_chip_codegen_supported(AGX_CHIP_G13X));
   CHECK(agx_chip_codegen_supported(AGX_CHIP_G14G));
   CHECK(agx_chip_codegen_supported(AGX_CHIP_G14X));

   CHECK(!agx_chip_codegen_supported(AGX_CHIP_G15G));
   CHECK(!agx_chip_codegen_supported(AGX_CHIP_G15X));
   CHECK(!agx_chip_codegen_supported(AGX_CHIP_G16G));
   CHECK(!agx_chip_codegen_supported(AGX_CHIP_G16X));
   CHECK(!agx_chip_codegen_supported(AGX_CHIP_G17G));
   CHECK(!agx_chip_codegen_supported(AGX_CHIP_G17X));
   CHECK(!agx_chip_codegen_supported(AGX_CHIP_UNKNOWN));

   /* Recognising a part must never on its own make it supported. */
   CHECK(chip_for(17, 2) != AGX_CHIP_UNKNOWN);
   CHECK(!agx_chip_codegen_supported(chip_for(17, 2)));

   /* Nor must naming its encoding. There are disassemblers for both AGX3
    * tables and a packer for neither, so every AGX3 part has a known encoding
    * and no code generator. */
   for (int chip = AGX_CHIP_G13G; chip <= AGX_CHIP_UNKNOWN; chip++) {
      const enum agx_chip c = (enum agx_chip)chip;

      if (agx_chip_isa(c) == AGX_ISA_AGX3) {
         CHECK(agx_chip_isa_encoding(c) != AGX_ISA_ENCODING_UNKNOWN);
         CHECK(!agx_chip_codegen_supported(c));
      }
   }
}

static void
test_hardware_capabilities(void)
{
   /* Hardware ray tracing arrives with AGX3 and exists on nothing before. */
   CHECK(!agx_chip_has_hardware_ray_tracing(AGX_CHIP_G13G));
   CHECK(!agx_chip_has_hardware_ray_tracing(AGX_CHIP_G13X));
   CHECK(!agx_chip_has_hardware_ray_tracing(AGX_CHIP_G14G));
   CHECK(!agx_chip_has_hardware_ray_tracing(AGX_CHIP_G14X));
   CHECK(agx_chip_has_hardware_ray_tracing(AGX_CHIP_G15G));
   CHECK(agx_chip_has_hardware_ray_tracing(AGX_CHIP_G15X));
   CHECK(agx_chip_has_hardware_ray_tracing(AGX_CHIP_G16G));
   CHECK(agx_chip_has_hardware_ray_tracing(AGX_CHIP_G17X));
   CHECK(!agx_chip_has_hardware_ray_tracing(AGX_CHIP_UNKNOWN));

   /* Scheduling stalls are the M3 base part alone -- not a property of its
    * encoding, which it shares with three chips that do not pad. */
   CHECK(agx_chip_needs_scheduling_stalls(AGX_CHIP_G15G));

   for (int chip = AGX_CHIP_G13G; chip <= AGX_CHIP_UNKNOWN; chip++) {
      const enum agx_chip c = (enum agx_chip)chip;

      if (c != AGX_CHIP_G15G)
         CHECK(!agx_chip_needs_scheduling_stalls(c));
   }

   CHECK_EQ(agx_chip_isa_encoding(AGX_CHIP_G15X), AGX_ISA_ENCODING_AGX3B);
   CHECK(!agx_chip_needs_scheduling_stalls(AGX_CHIP_G15X));
}

/* Every enumerator has to answer every query, so a chip added later cannot
 * fall through a switch into a default that flatters it. */
static void
test_every_chip_is_classified(void)
{
   for (int chip = AGX_CHIP_G13G; chip <= AGX_CHIP_UNKNOWN; chip++) {
      const enum agx_chip c = (enum agx_chip)chip;
      const enum agx_isa isa = agx_chip_isa(c);
      const bool known = c != AGX_CHIP_UNKNOWN;

      CHECK(strcmp(agx_chip_name(c), "") != 0);
      CHECK((isa != AGX_ISA_UNKNOWN) == known);
      CHECK(strcmp(agx_isa_name(isa), "") != 0);

      /* Every recognised chip has a named encoding, and only the unrecognised
       * one does not. */
      CHECK((agx_chip_isa_encoding(c) != AGX_ISA_ENCODING_UNKNOWN) == known);
      CHECK(strcmp(agx_isa_encoding_name(agx_chip_isa_encoding(c)), "") != 0);

      /* Codegen implies a named encoding, and a named encoding implies a
       * known instruction set. */
      if (agx_chip_codegen_supported(c)) {
         CHECK(agx_chip_isa_encoding(c) != AGX_ISA_ENCODING_UNKNOWN);
         CHECK(isa != AGX_ISA_UNKNOWN);
      }
      /* Ray tracing is exactly AGX3. */
      CHECK(agx_chip_has_hardware_ray_tracing(c) == (isa == AGX_ISA_AGX3));
   }

   CHECK(strcmp(agx_chip_name(AGX_CHIP_G17X), "G17X") == 0);
   CHECK(strcmp(agx_isa_name(AGX_ISA_AGX3), "AGX3") == 0);
   CHECK(strcmp(agx_chip_name(AGX_CHIP_UNKNOWN), "unknown") == 0);
}

int
main(void)
{
   test_identification();
   test_instruction_set();
   test_encoding();
   test_codegen_gate();
   test_hardware_capabilities();
   test_every_chip_is_classified();

   if (failures) {
      fprintf(stderr, "agx_chip: %u of %u checks failed\n", failures, checks);
      return 1;
   }

   printf("agx_chip: all %u checks passed\n", checks);
   return 0;
}
