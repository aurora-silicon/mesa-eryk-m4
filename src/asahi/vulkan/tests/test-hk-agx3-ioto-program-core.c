/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "hk_agx3_ioto_program_core.h"

#include "asahi/lib/agx_ioto_profile_core.h"
#include "asahi/lib/agx_ioto_state_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                \
         return 1;                                                             \
      }                                                                        \
   } while (0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const enum agx_chip targets[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static bool
is_gpu1(enum agx_chip chip)
{
   return chip == AGX_CHIP_G17G || chip == AGX_CHIP_G17S ||
          chip == AGX_CHIP_G17C;
}

static struct hk_agx3_ioto_program_desc
make_desc(enum agx_chip chip)
{
   static const uint8_t body[] = {0x06, 0x00};
   struct hk_agx3_ioto_program_desc desc = {
      .program_va = UINT64_C(0x123456789a80),
      .body = body,
      .body_size = sizeof(body),
      .tail.type = AGX_IOTO_STATE_TAIL_TYPE0,
      .profile_payload0 = is_gpu1(chip) ? UINT16_C(0x8098)
                                       : UINT16_C(0x0555),
      .profile_payload1 = is_gpu1(chip) ? UINT16_C(0xa400) : 0,
   };

   desc.tail.ldshdr.f[0] = UINT64_C(0x123456789ac0);
   if (is_gpu1(chip)) {
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

static int
test_exact_target_order(void)
{
   for (size_t i = 0; i < ARRAY_SIZE(targets); ++i) {
      const enum agx_chip chip = targets[i];
      const struct hk_agx3_ioto_program_desc desc = make_desc(chip);
      const struct agx_ioto_profile_control profile = {
         .chip = chip,
         .payload0 = desc.profile_payload0,
         .payload1 = desc.profile_payload1,
      };
      uint8_t packed_profile[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
      struct agx_ioto_state_tail_output packed_tail;
      CHECK(agx_ioto_profile_control_pack(&profile, packed_profile));
      CHECK(agx_ioto_state_tail_pack(chip, &desc.tail, &packed_tail));

      const size_t profile_size = agx_ioto_profile_control_size(chip);
      const size_t total = desc.body_size + profile_size + packed_tail.length +
                           AGX_IOTO_PROGRAM_TERMINATE_SIZE;
      uint8_t output[64];
      memset(output, 0xa5, sizeof(output));
      size_t written = SIZE_MAX;
      CHECK(hk_agx3_ioto_program_generate(chip, &desc, output, total,
                                          &written));
      CHECK(written == total);
      CHECK(memcmp(output, desc.body, desc.body_size) == 0);
      CHECK(memcmp(output + desc.body_size, packed_profile, profile_size) ==
            0);
      CHECK(memcmp(output + desc.body_size + profile_size, packed_tail.bytes,
                   packed_tail.length) == 0);
      static const uint8_t terminate[] = {0x0e, 0x00, 0x00, 0x00};
      CHECK(memcmp(output + total - sizeof(terminate), terminate,
                   sizeof(terminate)) == 0);
      CHECK(output[total] == 0xa5);
   }

   return 0;
}

static int
test_terminal_suffix_modes(void)
{
   static const uint8_t inline_code[] = {0x1b, 0x0c, 0x06, 0x00};

   for (size_t i = 0; i < ARRAY_SIZE(targets); ++i) {
      const enum agx_chip chip = targets[i];
      const struct hk_agx3_ioto_program_desc desc = make_desc(chip);
      struct agx_ioto_state_tail_output packed_tail;
      CHECK(agx_ioto_state_tail_pack(chip, &desc.tail, &packed_tail));
      const size_t prefix_size = desc.body_size +
                                 agx_ioto_profile_control_size(chip) +
                                 packed_tail.length;

      for (unsigned mode = 0; mode < 3; ++mode) {
         struct agx_ioto_preshader_desc preshader = {
            .external_va = AGX_IOTO_PRESHADER_NO_VA,
         };
         if (mode == 1) {
            preshader.code = inline_code;
            preshader.code_size = sizeof(inline_code);
         } else if (mode == 2) {
            preshader.force_external = true;
            preshader.request_prelude = true;
            preshader.external_va = UINT64_C(0x123456789abc);
         }

         struct agx_ioto_preshader_output suffix;
         CHECK(agx_ioto_preshader_emit(chip, &preshader, &suffix));
         const size_t total = prefix_size + suffix.length;
         uint8_t output[96];
         memset(output, 0xa5, sizeof(output));
         size_t written = SIZE_MAX;
         CHECK(hk_agx3_ioto_program_generate_no_pixrel(
            chip, &desc, &preshader, output, total, &written));
         CHECK(written == total);
         CHECK(memcmp(output + prefix_size, suffix.bytes, suffix.length) == 0);
         CHECK(output[total] == 0xa5);
         CHECK(suffix.mode == (mode == 0 ? AGX_IOTO_PRESHADER_MODE_ABSENT
                                        : mode == 1
                                             ? AGX_IOTO_PRESHADER_MODE_INLINE
                                             : AGX_IOTO_PRESHADER_MODE_EXTERNAL));
         if (mode != 0) {
            static const uint8_t absent[] = {0x0e, 0x00, 0x00, 0x00};
            CHECK(suffix.length < sizeof(absent) ||
                  memcmp(output + total - sizeof(absent), absent,
                         sizeof(absent)) != 0);
         }

         uint8_t failed[96];
         uint8_t before[sizeof(failed)];
         memset(failed, 0x5a, sizeof(failed));
         memcpy(before, failed, sizeof(failed));
         size_t failed_written = 29;
         CHECK(!hk_agx3_ioto_program_generate_no_pixrel(
            chip, &desc, &preshader, failed, total - 1, &failed_written));
         CHECK(memcmp(failed, before, sizeof(failed)) == 0);
         CHECK(failed_written == 29);
      }
   }

   return 0;
}

static int
expect_failure_unchanged(enum agx_chip chip,
                         const struct hk_agx3_ioto_program_desc *desc,
                         size_t capacity)
{
   uint8_t output[96];
   uint8_t before[sizeof(output)];
   memset(output, 0x5a, sizeof(output));
   memcpy(before, output, sizeof(output));
   size_t written = SIZE_MAX - 7;
   const size_t before_written = written;

   CHECK(!hk_agx3_ioto_program_generate(chip, desc, output, capacity,
                                        &written));
   CHECK(memcmp(output, before, sizeof(output)) == 0);
   CHECK(written == before_written);
   return 0;
}

static int
test_fail_closed(void)
{
   static const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G14X, AGX_CHIP_G15P,
      AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   struct hk_agx3_ioto_program_desc desc = make_desc(AGX_CHIP_G15G);
   for (size_t i = 0; i < ARRAY_SIZE(rejected); ++i)
      CHECK(expect_failure_unchanged(rejected[i], &desc, 96) == 0);

   static const uint8_t branch[] = {
      0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   desc.body = branch;
   desc.body_size = sizeof(branch);
   CHECK(expect_failure_unchanged(AGX_CHIP_G15G, &desc, 96) == 0);

   desc = make_desc(AGX_CHIP_G15G);
   desc.program_va |= 1;
   CHECK(expect_failure_unchanged(AGX_CHIP_G15G, &desc, 96) == 0);
   desc = make_desc(AGX_CHIP_G15G);
   desc.profile_payload0 = UINT16_C(0x0800);
   CHECK(expect_failure_unchanged(AGX_CHIP_G15G, &desc, 96) == 0);
   desc = make_desc(AGX_CHIP_G15G);
   CHECK(expect_failure_unchanged(AGX_CHIP_G15G, &desc, 1) == 0);
   CHECK(expect_failure_unchanged(AGX_CHIP_G15G, NULL, 96) == 0);

   uint8_t output[96];
   size_t written = 9;
   CHECK(!hk_agx3_ioto_program_generate(AGX_CHIP_G15G, &desc, NULL,
                                         sizeof(output), &written));
   CHECK(written == 9);
   CHECK(!hk_agx3_ioto_program_generate(AGX_CHIP_G15G, &desc, output,
                                         sizeof(output), NULL));

   memset(output, 0x5a, sizeof(output));
   uint8_t before[sizeof(output)];
   memcpy(before, output, sizeof(output));
   written = 11;
   CHECK(!hk_agx3_ioto_program_generate_no_pixrel(
      AGX_CHIP_G15G, &desc, NULL, output, sizeof(output), &written));
   CHECK(memcmp(output, before, sizeof(output)) == 0);
   CHECK(written == 11);
   return 0;
}

static int
expect_g17s_minimal_hk_failure(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   size_t capacity)
{
   uint8_t output[AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE + 1];
   uint8_t before[sizeof(output)];
   memset(output, 0x5a, sizeof(output));
   memcpy(before, output, sizeof(output));
   size_t written = 47;
   struct agx3_compute_program program;
   memset(&program, 0xa5, sizeof(program));
   uint8_t program_before[sizeof(program)];
   memcpy(program_before, &program, sizeof(program));

   CHECK(!hk_agx3_g17s_minimal_compute_program_prepare(
      chip, desc, output, capacity, &written, &program));
   CHECK(memcmp(output, before, sizeof(output)) == 0);
   CHECK(written == 47);
   CHECK(memcmp(&program, program_before, sizeof(program)) == 0);
   return 0;
}

static int
test_g17s_minimal_compute_admission(void)
{
   struct agx3_cdm_direct_template_sources sources;
   CHECK(agx3_cdm_direct_template_sources_compute(false, false, false,
                                                   &sources));
   const struct agx3_g17s_minimal_compute_program_desc desc = {
      .program_va = UINT64_C(0x100000040),
      .shader_va = UINT64_C(0x100000006c0),
      .root_uniform = {
         .user_buffer_binding = 1,
         .byte_offset = 0,
         .uniform_data_destination = 4,
         .uniform_data_count = 1,
         .temporary_base = 0,
      },
      .launch_sources = sources,
   };
   uint8_t direct[AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE + 1];
   uint8_t bridged[sizeof(direct)];
   memset(direct, 0xa5, sizeof(direct));
   memset(bridged, 0x5a, sizeof(bridged));
   size_t direct_written = SIZE_MAX, bridged_written = SIZE_MAX;
   struct agx3_compute_program direct_program, bridged_program;

   CHECK(agx3_g17s_minimal_compute_program_prepare(
      &desc, direct, sizeof(direct), &direct_written, &direct_program));
   CHECK(hk_agx3_g17s_minimal_compute_program_prepare(
      AGX_CHIP_G17S, &desc, bridged, sizeof(bridged), &bridged_written,
      &bridged_program));
   CHECK(bridged_written == AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE);
   CHECK(bridged_written == direct_written);
   CHECK(memcmp(bridged, direct, bridged_written) == 0);
   CHECK(bridged[bridged_written] == 0x5a);
   CHECK(bridged_program.chip == AGX_CHIP_G17S);
   CHECK(bridged_program.program_va == desc.program_va);
   CHECK(memcmp(&bridged_program.launch_template,
                &direct_program.launch_template,
                sizeof(bridged_program.launch_template)) == 0);

   _Alignas(AGX_IOTO_PROGRAM_ALIGNMENT)
      uint8_t mapped[AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE + 1];
   memset(mapped, 0x3c, sizeof(mapped));
   struct agx3_compute_program_upload upload = {
      .cpu = mapped,
      .capacity = sizeof(mapped),
      .gpu_va = desc.program_va,
   };
   size_t mapped_written = SIZE_MAX;
   struct agx3_compute_program mapped_program;
   CHECK(hk_agx3_g17s_minimal_compute_program_upload_prepare(
      AGX_CHIP_G17S, &desc, &upload, &mapped_written, &mapped_program));
   CHECK(mapped_written == direct_written);
   CHECK(memcmp(mapped, direct, mapped_written) == 0);
   CHECK(mapped[mapped_written] == 0x3c);
   CHECK(mapped_program.chip == AGX_CHIP_G17S);
   CHECK(mapped_program.program_va == upload.gpu_va);

   const struct agx_ioto_g17s_minimal_compute_resources resources = {
      .gather_buffer_va = UINT64_C(0x100000e1ba0),
      .user_compute_argument_gpu_va_1 = UINT64_C(0x10000030100),
      .absolute_uniform_data_va =
         AGX_IOTO_G17S_MINIMAL_COMPUTE_WITNESS_ABSOLUTE_VA,
   };
   _Alignas(AGX_IOTO_PROGRAM_ALIGNMENT)
      uint8_t live_mapped[AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE + 1];
   memset(live_mapped, 0x6d, sizeof(live_mapped));
   const struct agx3_compute_program_upload live_upload = {
      .cpu = live_mapped,
      .capacity = sizeof(live_mapped),
      .gpu_va = desc.program_va,
   };
   size_t live_written = SIZE_MAX;
   struct agx3_compute_program live_program;
   CHECK(hk_agx3_g17s_minimal_compute_program_live_upload_prepare(
      AGX_CHIP_G17S, &desc, &resources, &live_upload, &live_written,
      &live_program));
   CHECK(live_written == direct_written);
   CHECK(memcmp(live_mapped + 32, direct + 32, live_written - 32) == 0);
   CHECK(memcmp(live_mapped, direct, 32) != 0);
   CHECK(live_mapped[live_written] == 0x6d);
   CHECK(live_program.chip == AGX_CHIP_G17S &&
         live_program.program_va == live_upload.gpu_va);

   const struct agx3_compute_direct_grid grid = {
      .source = AGX3_CDM_DIRECT_GRID_THREADGROUP_COUNTS,
      .size = {2, 3, 4},
      .local_size = {8, 4, 2},
      .dimensions = 3,
   };
   uint8_t expected_launch[AGX3_CDM_DIRECT_LAUNCH_SIZE];
   CHECK(agx3_compute_program_pack_direct_launch(&live_program, &grid,
                                                  expected_launch));
   uint8_t dispatch[AGX3_G17S_MINIMAL_COMPUTE_DISPATCH_SIZE + 3];
   memset(dispatch, 0xb6, sizeof(dispatch));
   struct agx3_compute_dispatch_sink dispatch_sink = {
      .data = dispatch,
      .capacity = sizeof(dispatch),
      .length = 2,
   };
   CHECK(hk_agx3_g17s_minimal_compute_dispatch_append(
      &live_program, &grid, &dispatch_sink));
   CHECK(dispatch_sink.length ==
         2 + AGX3_G17S_MINIMAL_COMPUTE_DISPATCH_SIZE);
   CHECK(dispatch[0] == 0xb6 && dispatch[1] == 0xb6 &&
         dispatch[dispatch_sink.length] == 0xb6);
   CHECK(memcmp(dispatch + 2, expected_launch,
                AGX3_CDM_DIRECT_LAUNCH_SIZE) == 0);
   static const uint8_t strong_barrier[] = {0x60, 0x15, 0x00, 0x60};
   CHECK(memcmp(dispatch + 2 + AGX3_CDM_DIRECT_LAUNCH_SIZE, strong_barrier,
                sizeof(strong_barrier)) == 0);

   uint8_t dispatch_before[sizeof(dispatch)];
   memcpy(dispatch_before, dispatch, sizeof(dispatch));
   struct agx3_compute_dispatch_sink failed_sink = {
      .data = dispatch,
      .capacity = sizeof(dispatch),
      .length = 1,
   };
   CHECK(!hk_agx3_g17s_minimal_compute_dispatch_append(
      &direct_program, &grid, &failed_sink));
   CHECK(failed_sink.length == 1 &&
         memcmp(dispatch, dispatch_before, sizeof(dispatch)) == 0);
   struct agx3_compute_program sibling_program = live_program;
   sibling_program.chip = AGX_CHIP_G17G;
   CHECK(!hk_agx3_g17s_minimal_compute_dispatch_append(
      &sibling_program, &grid, &failed_sink));
   CHECK(failed_sink.length == 1 &&
         memcmp(dispatch, dispatch_before, sizeof(dispatch)) == 0);
   failed_sink.capacity = AGX3_G17S_MINIMAL_COMPUTE_DISPATCH_SIZE;
   CHECK(!hk_agx3_g17s_minimal_compute_dispatch_append(
      &live_program, &grid, &failed_sink));
   CHECK(failed_sink.length == 1 &&
         memcmp(dispatch, dispatch_before, sizeof(dispatch)) == 0);

   uint8_t live_before[sizeof(live_mapped)];
   memcpy(live_before, live_mapped, sizeof(live_mapped));
   const struct agx3_compute_program live_program_before = live_program;

   uint8_t mapped_before[sizeof(mapped)];
   memcpy(mapped_before, mapped, sizeof(mapped));
   const struct agx3_compute_program mapped_program_before = mapped_program;

   static const enum agx_chip rejected[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   for (size_t i = 0; i < ARRAY_SIZE(rejected); ++i) {
      CHECK(expect_g17s_minimal_hk_failure(rejected[i], &desc,
                                            sizeof(bridged)) == 0);
      mapped_written = 47;
      CHECK(!hk_agx3_g17s_minimal_compute_program_upload_prepare(
         rejected[i], &desc, &upload, &mapped_written, &mapped_program));
      CHECK(memcmp(mapped, mapped_before, sizeof(mapped)) == 0 &&
            mapped_written == 47);
      CHECK(memcmp(&mapped_program, &mapped_program_before,
                   sizeof(mapped_program)) == 0);
      live_written = 47;
      CHECK(!hk_agx3_g17s_minimal_compute_program_live_upload_prepare(
         rejected[i], &desc, &resources, &live_upload, &live_written,
         &live_program));
      CHECK(memcmp(live_mapped, live_before, sizeof(live_mapped)) == 0 &&
            live_written == 47);
      CHECK(memcmp(&live_program, &live_program_before,
                   sizeof(live_program)) == 0);
   }

   struct agx3_compute_program_upload invalid_upload = upload;
   invalid_upload.gpu_va += AGX_IOTO_PROGRAM_ALIGNMENT;
   mapped_written = 47;
   CHECK(!hk_agx3_g17s_minimal_compute_program_upload_prepare(
      AGX_CHIP_G17S, &desc, &invalid_upload, &mapped_written,
      &mapped_program));
   CHECK(memcmp(mapped, mapped_before, sizeof(mapped)) == 0 &&
         mapped_written == 47);
   CHECK(memcmp(&mapped_program, &mapped_program_before,
                sizeof(mapped_program)) == 0);

   struct agx_ioto_g17s_minimal_compute_resources invalid_resources =
      resources;
   invalid_resources.absolute_uniform_data_va = 0;
   live_written = 47;
   CHECK(!hk_agx3_g17s_minimal_compute_program_live_upload_prepare(
      AGX_CHIP_G17S, &desc, &invalid_resources, &live_upload, &live_written,
      &live_program));
   CHECK(memcmp(live_mapped, live_before, sizeof(live_mapped)) == 0 &&
         live_written == 47);
   CHECK(memcmp(&live_program, &live_program_before,
                sizeof(live_program)) == 0);

   struct agx3_g17s_minimal_compute_program_desc invalid = desc;
   invalid.root_uniform.user_buffer_binding = 0;
   CHECK(expect_g17s_minimal_hk_failure(AGX_CHIP_G17S, &invalid,
                                         sizeof(bridged)) == 0);
   CHECK(expect_g17s_minimal_hk_failure(
            AGX_CHIP_G17S, &desc,
            AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE - 1) == 0);
   CHECK(expect_g17s_minimal_hk_failure(AGX_CHIP_G17S, NULL,
                                         sizeof(bridged)) == 0);
   return 0;
}

static int
test_alias(void)
{
   struct hk_agx3_ioto_program_desc desc = make_desc(AGX_CHIP_G17G);
   uint8_t storage[96] = {0x06, 0x00, 0x06, 0x00};
   static const uint8_t expected[] = {0x06, 0x00, 0x06, 0x00};
   desc.body = storage;
   desc.body_size = sizeof(expected);
   size_t written;
   CHECK(hk_agx3_ioto_program_generate(AGX_CHIP_G17G, &desc, storage + 2,
                                       sizeof(storage) - 2, &written));
   CHECK(memcmp(storage + 2, expected, sizeof(expected)) == 0);

   memset(storage, 0, sizeof(storage));
   memcpy(storage, expected, sizeof(expected));
   static const uint8_t suffix_code[] = {0x1b, 0x0c, 0x06, 0x00};
   memcpy(storage + 4, suffix_code, sizeof(suffix_code));
   desc.body = storage;
   desc.body_size = sizeof(expected);
   const struct agx_ioto_preshader_desc preshader = {
      .code = storage + 4,
      .code_size = sizeof(suffix_code),
      .external_va = AGX_IOTO_PRESHADER_NO_VA,
   };
   CHECK(hk_agx3_ioto_program_generate_no_pixrel(
      AGX_CHIP_G17G, &desc, &preshader, storage + 2, sizeof(storage) - 2,
      &written));
   CHECK(memcmp(storage + 2, expected, sizeof(expected)) == 0);
   CHECK(storage[2 + written - 2] == 0x1b &&
         storage[2 + written - 1] == 0x0c);
   return 0;
}

int
main(void)
{
   if (test_exact_target_order() || test_terminal_suffix_modes() ||
       test_fail_closed() || test_g17s_minimal_compute_admission() ||
       test_alias())
      return 1;

   printf("Honeykrisp AGX3 Ioto program core: %u checks passed\n", checks);
   return 0;
}
