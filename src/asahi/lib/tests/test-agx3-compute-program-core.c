/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx3_compute_program_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                  \
         return false;                                                         \
      }                                                                        \
   } while (0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const enum agx_chip targets[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static bool
is_hal300(enum agx_chip chip)
{
   return chip == AGX_CHIP_G17G || chip == AGX_CHIP_G17S ||
          chip == AGX_CHIP_G17C;
}

static struct agx3_compute_program_desc
make_desc(enum agx_chip chip)
{
   static const uint8_t nop[] = {0x06, 0x00};
   struct agx3_compute_program_desc desc = {
      .chip = chip,
      .program_va = UINT64_C(0x123456789a80),
      .body = nop,
      .body_size = sizeof(nop),
      .profile =
         {
            .chip = chip,
            .payload0 = is_hal300(chip) ? UINT16_C(0x8098) : UINT16_C(0x0555),
            .payload1 = is_hal300(chip) ? UINT16_C(0xa400) : 0,
         },
      .preshader.external_va = AGX_IOTO_PRESHADER_NO_VA,
      .launch_sources =
         {
            .max_total_threads_per_threadgroup = 0x345,
            .extended_texture_write_mask_qword_1 = 0x124,
            .ray_traversal_depth_nonzero = true,
         },
   };

   desc.tail.type = AGX_IOTO_STATE_TAIL_TYPE0;
   desc.tail.ldshdr.f[0] = UINT64_C(0x123456789ac0);
   if (is_hal300(chip)) {
      desc.tail.speclm.f[0] = 3;
      desc.tail.speclm.f[1] = 165;
      desc.tail.ldshdr.f[5] = 3;
      desc.tail.ldshdr.f[6] = 107;
      desc.tail.spectpr.f[0] = 3;
      desc.tail.spectpr.f[1] = 166;
   } else {
      desc.tail.ldshdr.f[23] = 1;
   }

   return desc;
}

static bool
bytes_are(const void *data, size_t size, uint8_t value)
{
   const uint8_t *bytes = data;
   for (size_t i = 0; i < size; ++i) {
      if (bytes[i] != value)
         return false;
   }
   return true;
}

static bool
test_prepare_and_launch(void)
{
   for (size_t i = 0; i < ARRAY_SIZE(targets); ++i) {
      struct agx3_compute_program_desc desc = make_desc(targets[i]);
      uint8_t state[96];
      memset(state, 0xa5, sizeof(state));
      size_t written = SIZE_MAX;
      struct agx3_compute_program program;
      memset(&program, 0xa5, sizeof(program));
      CHECK(agx3_compute_program_prepare(&desc, state, sizeof(state), &written,
                                         &program));
      CHECK(program.chip == desc.chip);
      CHECK(program.program_va == desc.program_va);
      CHECK(written > desc.body_size && written < sizeof(state));
      CHECK(state[written] == 0xa5);

      struct agx3_cdm_direct_template expected;
      CHECK(agx3_cdm_direct_template_build_from_sources(&desc.launch_sources,
                                                        &expected));
      CHECK(memcmp(&program.launch_template, &expected, sizeof(expected)) == 0);

      const struct agx3_compute_direct_grid grid = {
         .source = AGX3_CDM_DIRECT_GRID_THREADGROUP_COUNTS,
         .size = {7, 8, 9},
         .local_size = {4, 5, 6},
         .dimensions = 3,
      };
      uint8_t launch[AGX3_CDM_DIRECT_LAUNCH_SIZE];
      CHECK(agx3_compute_program_pack_direct_launch(&program, &grid, launch));

      struct agx3_cdm_direct_launch_info decoded;
      CHECK(agx3_cdm_direct_launch_unpack(desc.chip, launch, sizeof(launch),
                                          &decoded));
      CHECK(decoded.pipeline == desc.program_va);
      CHECK(decoded.global_size[0] == 28 && decoded.global_size[1] == 40 &&
            decoded.global_size[2] == 54);
      CHECK(decoded.local_size[0] == 4 && decoded.local_size[1] == 5 &&
            decoded.local_size[2] == 6);
      CHECK(decoded.dimensions == 3);
      CHECK(memcmp(&decoded.template_words, &expected, sizeof(expected)) == 0);
   }

   return true;
}

static bool
test_a18_pro_one_dimensional_launch(void)
{
   struct agx3_compute_program_desc desc = make_desc(AGX_CHIP_G17P);
   uint8_t state[96];
   size_t written;
   struct agx3_compute_program program;
   CHECK(agx3_compute_program_prepare(&desc, state, sizeof(state), &written,
                                      &program));

   const struct agx3_compute_direct_grid grid = {
      .source = AGX3_CDM_DIRECT_GRID_GLOBAL_THREADS,
      .size = {257, 1, 1},
      .local_size = {32, 1, 1},
      .dimensions = 1,
   };
   uint8_t launch[AGX3_CDM_DIRECT_LAUNCH_SIZE];
   CHECK(agx3_compute_program_pack_direct_launch(&program, &grid, launch));

   struct agx3_cdm_direct_launch_info decoded;
   CHECK(agx3_cdm_direct_launch_unpack(AGX_CHIP_G17P, launch, sizeof(launch),
                                       &decoded));
   struct agx3_cdm_direct_template_fields fields;
   CHECK(agx3_cdm_direct_template_decode(&decoded.template_words, &fields));
   CHECK(!fields.subkernel_x_target_workitems);
   CHECK(fields.ray_traversal_depth_nonzero);
   CHECK(decoded.pipeline == desc.program_va);
   CHECK(decoded.global_size[0] == 257 && decoded.global_size[1] == 1 &&
         decoded.global_size[2] == 1);
   return true;
}

static bool
prepare_fails_unchanged(struct agx3_compute_program_desc *desc, size_t capacity)
{
   uint8_t state[96], before[sizeof(state)];
   memset(state, 0x5a, sizeof(state));
   memcpy(before, state, sizeof(state));
   size_t written = 37;
   struct agx3_compute_program program, program_before;
   memset(&program, 0xa5, sizeof(program));
   program_before = program;
   CHECK(
      !agx3_compute_program_prepare(desc, state, capacity, &written, &program));
   CHECK(memcmp(state, before, sizeof(state)) == 0);
   CHECK(written == 37);
   CHECK(memcmp(&program, &program_before, sizeof(program)) == 0);
   return true;
}

static bool
test_fail_closed(void)
{
   struct agx3_compute_program_desc desc = make_desc(AGX_CHIP_G17P);
   CHECK(prepare_fails_unchanged(NULL, sizeof((uint8_t[96]){0})));

   desc.profile.chip = AGX_CHIP_G17G;
   CHECK(prepare_fails_unchanged(&desc, 96));
   desc.profile.chip = desc.chip;
   desc.program_va |= 1;
   CHECK(prepare_fails_unchanged(&desc, 96));
   desc.program_va &= ~UINT64_C(63);
   desc.launch_sources.extended_texture_write_mask_qword_1 = UINT32_MAX;
   CHECK(prepare_fails_unchanged(&desc, 96));
   desc = make_desc(AGX_CHIP_G17P);
   CHECK(prepare_fails_unchanged(&desc, 1));
   desc = make_desc(AGX_CHIP_G14X);
   CHECK(prepare_fails_unchanged(&desc, 96));

   desc = make_desc(AGX_CHIP_G17P);
   uint8_t state[96];
   size_t written;
   struct agx3_compute_program program;
   CHECK(agx3_compute_program_prepare(&desc, state, sizeof(state), &written,
                                      &program));
   struct agx3_compute_direct_grid grid = {
      .source = AGX3_CDM_DIRECT_GRID_GLOBAL_THREADS,
      .size = {1, 1, 1},
      .local_size = {1, 1, 1},
      .dimensions = 1,
   };
   uint8_t launch[AGX3_CDM_DIRECT_LAUNCH_SIZE];
   memset(launch, 0xa5, sizeof(launch));
   CHECK(!agx3_compute_program_pack_direct_launch(NULL, &grid, launch));
   CHECK(bytes_are(launch, sizeof(launch), 0));
   memset(launch, 0xa5, sizeof(launch));
   CHECK(!agx3_compute_program_pack_direct_launch(&program, NULL, launch));
   CHECK(bytes_are(launch, sizeof(launch), 0));

   grid.local_size[0] = 0x800;
   memset(launch, 0xa5, sizeof(launch));
   CHECK(!agx3_compute_program_pack_direct_launch(&program, &grid, launch));
   CHECK(bytes_are(launch, sizeof(launch), 0));
   grid.local_size[0] = 1;
   grid.g15x_has_multiple_agcs = true;
   memset(launch, 0xa5, sizeof(launch));
   CHECK(!agx3_compute_program_pack_direct_launch(&program, &grid, launch));
   CHECK(bytes_are(launch, sizeof(launch), 0));

   program.program_va |= 1;
   memset(launch, 0xa5, sizeof(launch));
   CHECK(!agx3_compute_program_pack_direct_launch(&program, &grid, launch));
   CHECK(bytes_are(launch, sizeof(launch), 0));
   CHECK(!agx3_compute_program_pack_direct_launch(&program, &grid, NULL));
   return true;
}

int
main(void)
{
   if (!test_prepare_and_launch() || !test_a18_pro_one_dimensional_launch() ||
       !test_fail_closed())
      return 1;

   printf("agx3_compute_program_core: %u checks passed\n", checks);
   return 0;
}
