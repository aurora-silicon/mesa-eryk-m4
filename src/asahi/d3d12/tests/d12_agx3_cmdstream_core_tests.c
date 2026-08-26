/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "../d12_agx3_cmdstream_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);               \
         return 1;                                                             \
      }                                                                        \
   } while (0)

static struct agx_ioto_state_tail
make_ioto_state_tail(bool gpu1, enum agx_ioto_state_tail_type type)
{
   struct agx_ioto_state_tail tail = {.type = type};

   if (gpu1) {
      tail.speclm.f[0] = 3;
      tail.speclm.f[1] = 165;
      tail.speclm.f[2] = tail.speclm.f[4] = tail.speclm.f[6] = 1;
      tail.speclm.f[8] = tail.speclm.f[24] = tail.speclm.f[26] = 1;
      tail.speclm.f[18] = 0xabcd;
      tail.speclm.f[21] = 2;
      tail.speclm.f[22] = 0x5a;

      tail.ldshdr.f[0] = UINT64_C(0x123456789ac0);
      tail.ldshdr.f[1] = 2;
      tail.ldshdr.f[3] = 1;
      tail.ldshdr.f[5] = 3;
      tail.ldshdr.f[6] = 107;
      tail.ldshdr.f[7] = tail.ldshdr.f[9] = tail.ldshdr.f[11] = 1;
      tail.ldshdr.f[13] = tail.ldshdr.f[23] = tail.ldshdr.f[24] = 1;

      tail.spectpr.f[0] = 3;
      tail.spectpr.f[1] = 166;
      tail.spectpr.f[2] = tail.spectpr.f[4] = tail.spectpr.f[6] = 1;
      tail.spectpr.f[8] = tail.spectpr.f[11] = tail.spectpr.f[23] = 1;
      tail.spectpr.f[10] = 0x6d;
      tail.spectpr.f[20] = 0xa55a;
      tail.spectpr.f[21] = 0x5aa5;
      tail.spectpr.f[22] = 0xafe;
   } else {
      tail.speclm.f[2] = tail.speclm.f[4] = tail.speclm.f[6] = 1;
      tail.speclm.f[10] = 2;
      tail.speclm.f[11] = tail.speclm.f[24] = tail.speclm.f[25] = 1;
      tail.speclm.f[18] = 0xa5c0;
      tail.speclm.f[21] = 3;
      tail.speclm.f[22] = 0xd8;

      tail.ldshdr.f[0] = UINT64_C(0x123456789ac0);
      tail.ldshdr.f[1] = 3;
      tail.ldshdr.f[7] = tail.ldshdr.f[9] = tail.ldshdr.f[11] = 1;
      tail.ldshdr.f[15] = 2;
      tail.ldshdr.f[16] = tail.ldshdr.f[23] = 1;

      tail.spectpr.f[2] = tail.spectpr.f[4] = tail.spectpr.f[6] = 1;
      tail.spectpr.f[10] = 0x55;
      tail.spectpr.f[11] = tail.spectpr.f[13] = 1;
      tail.spectpr.f[12] = 2;
      tail.spectpr.f[20] = 0xa55a;
      tail.spectpr.f[21] = 0x5aa5;
      tail.spectpr.f[22] = 0x7fe;
   }

   if (type == AGX_IOTO_STATE_TAIL_TYPE1)
      memset(&tail.speclm, 0, sizeof(tail.speclm));

   return tail;
}

static int
test_ioto_raw(void)
{
   static const uint8_t gpu0_tail[] = {
      0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x77, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xf7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   static const uint8_t gpu1_tail[] = {
      0x07, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00,
      0x07, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x07, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   static const enum agx_ioto_kind tail_kinds[] = {
      AGX_IOTO_KIND_SPECLM_EP0,
      AGX_IOTO_KIND_LDSHDR_EP0,
      AGX_IOTO_KIND_SPECTPR_EP0,
   };
   static const uint8_t iadd_gpu0[] = {
      0x9f, 0x11, 0x54, 0x00, 0x02, 0x00, 0x08, 0xa8, 0x10, 0x05,
   };
   static const uint8_t iadd_gpu1[] = {
      0x27, 0x04, 0x04, 0x1a, 0x21, 0x00,
      0xa3, 0x02, 0x88, 0x80, 0x00, 0x00,
   };

   const uint8_t *tails[] = {gpu0_tail, gpu1_tail};
   const enum agx_chip chips[] = {AGX_CHIP_G15G, AGX_CHIP_G17S};
   const size_t lengths[] = {sizeof(gpu0_tail), sizeof(gpu1_tail)};
   for (unsigned target = 0; target < 2; ++target) {
      size_t offset = 0;
      for (unsigned i = 0; i < 3; ++i) {
         struct agx_ioto_record record;
         size_t consumed;
         CHECK(d12_agx3_validate_ioto_record(
            chips[target], tail_kinds[i], tails[target] + offset,
            lengths[target] - offset, &record, &consumed));
         CHECK(record.kind == tail_kinds[i]);
         CHECK(consumed == (i == 0 ? 8 : 10));
         CHECK(memcmp(record.bytes, tails[target] + offset, consumed) == 0);
         offset += consumed;
      }
      CHECK(offset == lengths[target]);
   }

   const uint8_t *iadds[] = {iadd_gpu0, iadd_gpu1};
   const size_t iadd_sizes[] = {sizeof(iadd_gpu0), sizeof(iadd_gpu1)};
   for (unsigned target = 0; target < 2; ++target) {
      struct agx_ioto_record record;
      size_t consumed = 99;
      CHECK(d12_agx3_decode_ioto_record(chips[target], iadds[target],
                                        iadd_sizes[target], &record,
                                        &consumed));
      CHECK(record.kind == AGX_IOTO_KIND_IADD_EP0);
      CHECK(consumed == iadd_sizes[target]);
      CHECK(memcmp(record.bytes, iadds[target], consumed) == 0);

      memset(&record, 0xff, sizeof(record));
      consumed = 99;
      CHECK(!d12_agx3_validate_ioto_record(
         chips[target], AGX_IOTO_KIND_IADD_EP0, iadds[target],
         iadd_sizes[target] - 1, &record, &consumed));
      CHECK(record.kind == AGX_IOTO_KIND_INVALID);
      CHECK(record.length == 0 && consumed == 0);
   }

   struct agx_ioto_record record;
   size_t consumed = 99;
   CHECK(!d12_agx3_decode_ioto_record(AGX_CHIP_G14X, iadd_gpu0,
                                      sizeof(iadd_gpu0), &record, &consumed));
   CHECK(record.kind == AGX_IOTO_KIND_INVALID && consumed == 0);
   return 0;
}

static int
validate_ioto_state_tail(
   enum agx_chip chip, enum agx_ioto_state_tail_type type,
   const struct d12_agx3_ioto_state_tail_encoding *encoded)
{
   static const enum agx_ioto_kind type0_kinds[] = {
      AGX_IOTO_KIND_SPECLM_EP0,
      AGX_IOTO_KIND_LDSHDR_EP0,
      AGX_IOTO_KIND_SPECTPR_EP0,
   };
   static const enum agx_ioto_kind type1_kinds[] = {
      AGX_IOTO_KIND_LDSHDR_EP0,
      AGX_IOTO_KIND_SPECTPR_EP0,
   };
   const enum agx_ioto_kind *kinds =
      type == AGX_IOTO_STATE_TAIL_TYPE0 ? type0_kinds : type1_kinds;
   const unsigned count = type == AGX_IOTO_STATE_TAIL_TYPE0 ? 3 : 2;
   size_t offset = 0;

   for (unsigned i = 0; i < count; ++i) {
      struct agx_ioto_record record;
      size_t consumed = 0;
      CHECK(d12_agx3_validate_ioto_record(
         chip, kinds[i], encoded->bytes + offset, encoded->length - offset,
         &record, &consumed));
      CHECK(record.kind == kinds[i] && record.length == consumed);
      CHECK(memcmp(record.bytes, encoded->bytes + offset, consumed) == 0);
      offset += consumed;
   }

   CHECK(offset == encoded->length);
   return 0;
}

static int
test_ioto_state_tail(void)
{
   static const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   struct d12_agx3_ioto_state_tail_encoding cohort_reference[2][2];
   bool have_reference[2][2] = {{false}};

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool gpu1 = t >= 8;
      struct d12_agx3_ioto_state_tail_encoding encoded[2];

      for (unsigned type = 0; type < 2; ++type) {
         const struct agx_ioto_state_tail tail = make_ioto_state_tail(
            gpu1, (enum agx_ioto_state_tail_type)type);
         struct agx_ioto_state_tail_output shared;
         CHECK(agx_ioto_state_tail_pack(targets[t], &tail, &shared));

         memset(&encoded[type], 0xa5, sizeof(encoded[type]));
         CHECK(d12_agx3_encode_ioto_state_tail(targets[t], &tail,
                                               &encoded[type]));
         CHECK(encoded[type].length == shared.length);
         CHECK(encoded[type].length ==
               (type == 0 ? AGX_IOTO_STATE_TAIL_TYPE0_SIZE
                          : AGX_IOTO_STATE_TAIL_TYPE1_SIZE));
         CHECK(memcmp(encoded[type].bytes, shared.bytes, shared.length) == 0);
         if (validate_ioto_state_tail(targets[t], tail.type, &encoded[type]))
            return 1;

         if (!have_reference[gpu1][type]) {
            cohort_reference[gpu1][type] = encoded[type];
            have_reference[gpu1][type] = true;
         } else {
            CHECK(memcmp(&cohort_reference[gpu1][type], &encoded[type],
                         sizeof(encoded[type])) == 0);
         }

         struct agx_ioto_state_tail shader_tail = tail;
         shader_tail.ldshdr.f[0] = 0;
         struct d12_agx3_ioto_state_tail_encoding shader_encoded;
         memset(&shader_encoded, 0x5a, sizeof(shader_encoded));
         CHECK(d12_agx3_encode_ioto_state_tail_shader(
            targets[t], &shader_tail, UINT64_C(0x123456789ac0),
            &shader_encoded));
         CHECK(memcmp(&shader_encoded, &encoded[type], sizeof(encoded[type])) ==
               0);
      }

      CHECK(memcmp(encoded[0].bytes + AGX_IOTO_SPECLM_SIZE, encoded[1].bytes,
                   encoded[1].length) == 0);
   }

   struct agx_ioto_state_tail tail =
      make_ioto_state_tail(false, AGX_IOTO_STATE_TAIL_TYPE0);
   struct d12_agx3_ioto_state_tail_encoding out;
   memset(&out, 0xa5, sizeof(out));
   const struct d12_agx3_ioto_state_tail_encoding before = out;

#define CHECK_UNCHANGED(call)                                                  \
   do {                                                                        \
      CHECK(!(call));                                                          \
      CHECK(memcmp(&out, &before, sizeof(out)) == 0);                          \
   } while (0)

   CHECK_UNCHANGED(
      d12_agx3_encode_ioto_state_tail(AGX_CHIP_G14X, &tail, &out));
   tail.type = (enum agx_ioto_state_tail_type)2;
   CHECK_UNCHANGED(
      d12_agx3_encode_ioto_state_tail(AGX_CHIP_G15G, &tail, &out));
   tail = make_ioto_state_tail(false, AGX_IOTO_STATE_TAIL_TYPE0);
   tail.ldshdr.f[0] |= 1;
   CHECK_UNCHANGED(
      d12_agx3_encode_ioto_state_tail(AGX_CHIP_G15G, &tail, &out));
   tail = make_ioto_state_tail(false, AGX_IOTO_STATE_TAIL_TYPE1);
   tail.speclm.f[2] = 1;
   CHECK_UNCHANGED(
      d12_agx3_encode_ioto_state_tail(AGX_CHIP_G15G, &tail, &out));
   tail = make_ioto_state_tail(false, AGX_IOTO_STATE_TAIL_TYPE0);
   CHECK_UNCHANGED(d12_agx3_encode_ioto_state_tail_shader(
      AGX_CHIP_G15G, &tail, UINT64_C(0x123456789ac0), &out));
   tail.ldshdr.f[0] = 0;
   CHECK_UNCHANGED(d12_agx3_encode_ioto_state_tail_shader(
      AGX_CHIP_G15G, &tail, UINT64_C(0x123456789ac1), &out));
   CHECK_UNCHANGED(d12_agx3_encode_ioto_state_tail_shader(
      AGX_CHIP_G15G, &tail, UINT64_C(1) << 48, &out));
   CHECK_UNCHANGED(
      d12_agx3_encode_ioto_state_tail(AGX_CHIP_G15G, NULL, &out));
   CHECK(!d12_agx3_encode_ioto_state_tail(AGX_CHIP_G15G, &tail, NULL));

#undef CHECK_UNCHANGED
   return 0;
}

static int
test_ioto_profile(void)
{
   const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint16_t classic_values[] = {0, 1, 0x00ff, 0x0700, 0x07ff};
   const uint16_t hal300_values0[] = {0, 0x0008, 0x0010, 0x0080, 0x9088,
                                     0xf098};
   const uint16_t hal300_values1[] = {0, 0x0400, 0x5400, 0xa800, 0xfc00, 0};

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool hal300 = targets[t] == AGX_CHIP_G17G ||
                          targets[t] == AGX_CHIP_G17S ||
                          targets[t] == AGX_CHIP_G17C;
      const uint16_t *values = hal300 ? hal300_values0 : classic_values;
      const unsigned value_count =
         hal300 ? sizeof(hal300_values0) / sizeof(hal300_values0[0])
                : sizeof(classic_values) / sizeof(classic_values[0]);
      for (unsigned v = 0; v < value_count; ++v) {
         const uint16_t payload1_in = hal300 ? hal300_values1[v] : 0;
         struct d12_agx3_ioto_profile_encoding encoded;
         CHECK(d12_agx3_encode_ioto_profile(
            targets[t], values[v], payload1_in, &encoded));
         CHECK(encoded.length == (hal300 ? 6 : 4));
         CHECK(encoded.bytes[0] == (hal300 ? 0x07 : 0xf7));
         CHECK(encoded.bytes[1] == (hal300 ? 0x00 : 0x03));

         uint16_t payload0, payload1;
         CHECK(d12_agx3_decode_ioto_profile(
            targets[t], encoded.bytes, encoded.length, &payload0, &payload1));
         CHECK(payload0 == values[v]);
         CHECK(payload1 == (hal300 ? hal300_values1[v] : 0));
         CHECK(!d12_agx3_decode_ioto_profile(targets[t], encoded.bytes,
                                             encoded.length + 1, &payload0,
                                             &payload1));
         CHECK(payload0 == 0 && payload1 == 0);
      }
   }

   const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G14X, AGX_CHIP_G15P, AGX_CHIP_G16P,
      AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      struct d12_agx3_ioto_profile_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      CHECK(!d12_agx3_encode_ioto_profile(rejected[i], 0, 0, &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }

   struct d12_agx3_ioto_profile_encoding encoded;
   CHECK(!d12_agx3_encode_ioto_profile(AGX_CHIP_G16G, 0, 1, &encoded));
   CHECK(!d12_agx3_encode_ioto_profile(AGX_CHIP_G17S, 1, 0, &encoded));
   CHECK(!d12_agx3_encode_ioto_profile(AGX_CHIP_G16G, 0, 0, NULL));
   CHECK(!d12_agx3_decode_ioto_profile(AGX_CHIP_G16G, NULL, 4, NULL, NULL));
   return 0;
}

static int
test_ioto_program_bridge(void)
{
   static const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   static const uint8_t body[] = {0x06, 0x00};

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool gpu1 = t >= 8;
      const struct agx_ioto_state_tail tail = make_ioto_state_tail(
         gpu1, AGX_IOTO_STATE_TAIL_TYPE1);
      const struct agx_ioto_profile_control profile = {
         .chip = targets[t],
         .payload0 = gpu1 ? UINT16_C(0x9088) : UINT16_C(0x0555),
         .payload1 = gpu1 ? UINT16_C(0xa800) : 0,
      };
      uint8_t shared[64], encoded[64];
      memset(shared, 0xa5, sizeof(shared));
      memset(encoded, 0x5a, sizeof(encoded));
      size_t shared_written = SIZE_MAX, encoded_written = SIZE_MAX;

      CHECK(agx_ioto_straight_line_program_compose(
         targets[t], UINT64_C(0x100000040), body, sizeof(body), &tail,
         &profile, shared, sizeof(shared), &shared_written));
      CHECK(d12_agx3_encode_ioto_straight_line_program(
         targets[t], UINT64_C(0x100000040), body, sizeof(body), &tail,
         &profile, encoded, sizeof(encoded), &encoded_written));
      CHECK(encoded_written == shared_written);
      CHECK(memcmp(encoded, shared, shared_written) == 0);

      uint8_t packed_profile[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
      struct agx_ioto_state_tail_output packed_tail;
      CHECK(agx_ioto_profile_control_pack(&profile, packed_profile));
      CHECK(agx_ioto_state_tail_pack(targets[t], &tail, &packed_tail));
      const size_t profile_size = agx_ioto_profile_control_size(targets[t]);
      CHECK(memcmp(encoded, body, sizeof(body)) == 0);
      CHECK(memcmp(encoded + sizeof(body), packed_profile, profile_size) == 0);
      CHECK(memcmp(encoded + sizeof(body) + profile_size, packed_tail.bytes,
                   packed_tail.length) == 0);
      CHECK(memcmp(encoded + encoded_written - AGX_IOTO_PROGRAM_TERMINATE_SIZE,
                   "\x0e\x00\x00\x00",
                   AGX_IOTO_PROGRAM_TERMINATE_SIZE) == 0);
      CHECK(encoded[encoded_written] == 0x5a);
   }

   struct agx_ioto_state_tail tail =
      make_ioto_state_tail(false, AGX_IOTO_STATE_TAIL_TYPE1);
   const struct agx_ioto_profile_control profile = {.chip = AGX_CHIP_G15G};
   uint8_t output[64], before[64];
   memset(output, 0xa5, sizeof(output));
   memcpy(before, output, sizeof(before));
   size_t written = 0x1234;
   CHECK(!d12_agx3_encode_ioto_straight_line_program(
      AGX_CHIP_G14X, UINT64_C(0x100000040), body, sizeof(body), &tail,
      &profile, output, sizeof(output), &written));
   CHECK(written == 0x1234);
   CHECK(memcmp(output, before, sizeof(output)) == 0);
   CHECK(!d12_agx3_encode_ioto_straight_line_program(
      AGX_CHIP_G15G, UINT64_C(0x100000041), body, sizeof(body), &tail,
      &profile, output, sizeof(output), &written));
   CHECK(written == 0x1234);
   CHECK(memcmp(output, before, sizeof(output)) == 0);
   return 0;
}

static int
expect_g17s_minimal_d12_failure(
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

   CHECK(!d12_agx3_g17s_minimal_compute_program_prepare(
      chip, desc, output, capacity, &written, &program));
   CHECK(memcmp(output, before, sizeof(output)) == 0);
   CHECK(written == 47);
   CHECK(memcmp(&program, program_before, sizeof(program)) == 0);
   return 0;
}

static int
test_g17s_minimal_compute_program_bridge(void)
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
   CHECK(d12_agx3_g17s_minimal_compute_program_prepare(
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
   CHECK(d12_agx3_g17s_minimal_compute_program_upload_prepare(
      AGX_CHIP_G17S, &desc, &upload, &mapped_written, &mapped_program));
   CHECK(mapped_written == direct_written);
   CHECK(memcmp(mapped, direct, mapped_written) == 0);
   CHECK(mapped[mapped_written] == 0x3c);
   CHECK(mapped_program.chip == AGX_CHIP_G17S);
   CHECK(mapped_program.program_va == upload.gpu_va);

   uint8_t mapped_before[sizeof(mapped)];
   memcpy(mapped_before, mapped, sizeof(mapped));
   const struct agx3_compute_program mapped_program_before = mapped_program;

   static const enum agx_chip rejected[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      CHECK(expect_g17s_minimal_d12_failure(rejected[i], &desc,
                                             sizeof(bridged)) == 0);
      mapped_written = 47;
      CHECK(!d12_agx3_g17s_minimal_compute_program_upload_prepare(
         rejected[i], &desc, &upload, &mapped_written, &mapped_program));
      CHECK(memcmp(mapped, mapped_before, sizeof(mapped)) == 0 &&
            mapped_written == 47);
      CHECK(memcmp(&mapped_program, &mapped_program_before,
                   sizeof(mapped_program)) == 0);
   }

   struct agx3_compute_program_upload invalid_upload = upload;
   invalid_upload.gpu_va += AGX_IOTO_PROGRAM_ALIGNMENT;
   mapped_written = 47;
   CHECK(!d12_agx3_g17s_minimal_compute_program_upload_prepare(
      AGX_CHIP_G17S, &desc, &invalid_upload, &mapped_written,
      &mapped_program));
   CHECK(memcmp(mapped, mapped_before, sizeof(mapped)) == 0 &&
         mapped_written == 47);
   CHECK(memcmp(&mapped_program, &mapped_program_before,
                sizeof(mapped_program)) == 0);

   struct agx3_g17s_minimal_compute_program_desc invalid = desc;
   invalid.root_uniform.user_buffer_binding = 0;
   CHECK(expect_g17s_minimal_d12_failure(AGX_CHIP_G17S, &invalid,
                                          sizeof(bridged)) == 0);
   CHECK(expect_g17s_minimal_d12_failure(
            AGX_CHIP_G17S, &desc,
            AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE - 1) == 0);
   CHECK(expect_g17s_minimal_d12_failure(AGX_CHIP_G17S, NULL,
                                          sizeof(bridged)) == 0);
   return 0;
}

static int
test_ioto_preshader_bridge(void)
{
   static const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   static const uint8_t inline_code[] = {
      0x1b, 0x0c, 0x06, 0x00, 0x06, 0x00,
   };
   uint8_t external_code[AGX_IOTO_PRESHADER_INLINE_LIMIT + 2] = {0};

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool gpu1 = t >= 8;
      struct agx_ioto_preshader_desc desc = {
         .code = inline_code,
         .code_size = sizeof(inline_code),
         .external_va = UINT64_C(0x123456789abc),
      };
      struct agx_ioto_preshader_output shared;
      struct d12_agx3_ioto_preshader_encoding encoded;
      CHECK(agx_ioto_preshader_emit(targets[t], &desc, &shared));
      CHECK(d12_agx3_encode_ioto_preshader_suffix(targets[t], &desc,
                                                  &encoded));
      CHECK(encoded.mode == AGX_IOTO_PRESHADER_MODE_INLINE);
      CHECK(encoded.length == 2);
      CHECK(encoded.branch_offset == AGX_IOTO_PRESHADER_NO_BRANCH);
      CHECK(encoded.mode == shared.mode && encoded.length == shared.length);
      CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);

      desc.code = external_code;
      desc.code_size = sizeof(external_code);
      desc.request_prelude = true;
      CHECK(agx_ioto_preshader_emit(targets[t], &desc, &shared));
      CHECK(d12_agx3_encode_ioto_preshader_suffix(targets[t], &desc,
                                                  &encoded));
      CHECK(encoded.mode == AGX_IOTO_PRESHADER_MODE_EXTERNAL);
      CHECK(encoded.length == AGX_IOTO_PRESHADER_PRELUDE_SIZE +
                                   AGX_IOTO_PRESHADER_BRANCH_SIZE);
      CHECK(encoded.branch_offset == AGX_IOTO_PRESHADER_PRELUDE_SIZE);
      CHECK(encoded.bytes[0] == (gpu1 ? 0x04 : 0x0c));
      CHECK(encoded.bytes[1] == 0x10);
      CHECK(encoded.mode == shared.mode && encoded.length == shared.length);
      CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);

      struct agx_ioto_record branch;
      size_t consumed;
      CHECK(d12_agx3_validate_ioto_record(
         targets[t], gpu1 ? AGX_IOTO_KIND_B_EP1 : AGX_IOTO_KIND_B_EP0,
         encoded.bytes + encoded.branch_offset,
         encoded.length - encoded.branch_offset, &branch, &consumed));
      CHECK(consumed == AGX_IOTO_PRESHADER_BRANCH_SIZE);

      desc.force_external = true;
      desc.external_va = AGX_IOTO_PRESHADER_NO_VA;
      CHECK(d12_agx3_encode_ioto_preshader_suffix(targets[t], &desc,
                                                  &encoded));
      CHECK(encoded.mode == AGX_IOTO_PRESHADER_MODE_ABSENT);
      CHECK(encoded.length == AGX_IOTO_PRESHADER_ABSENT_SIZE);
      CHECK(memcmp(encoded.bytes, "\x0e\x00\x00\x00", 4) == 0);
   }

   struct agx_ioto_preshader_desc invalid = {
      .code = external_code,
      .code_size = sizeof(external_code),
      .external_va = 1,
   };
   struct d12_agx3_ioto_preshader_encoding output, before;
   memset(&output, 0xa5, sizeof(output));
   before = output;
   CHECK(!d12_agx3_encode_ioto_preshader_suffix(AGX_CHIP_G15G, &invalid,
                                                 &output));
   CHECK(memcmp(&output, &before, sizeof(output)) == 0);
   invalid.external_va = 0;
   CHECK(!d12_agx3_encode_ioto_preshader_suffix(AGX_CHIP_G14X, &invalid,
                                                 &output));
   CHECK(memcmp(&output, &before, sizeof(output)) == 0);
   CHECK(!d12_agx3_encode_ioto_preshader_suffix(AGX_CHIP_G15G, &invalid,
                                                 NULL));
   return 0;
}

static int
test_ioto_full_program_bridge(void)
{
   static const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   static const uint8_t body[] = {0x06, 0x00};
   static const uint8_t inline_code[] = {
      0x1b, 0x0c, 0x06, 0x00, 0x06, 0x00,
   };
   uint8_t external_code[AGX_IOTO_PRESHADER_INLINE_LIMIT + 2] = {0};

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool gpu1 = t >= 8;
      const struct agx_ioto_state_tail tail = make_ioto_state_tail(
         gpu1, AGX_IOTO_STATE_TAIL_TYPE1);
      const struct agx_ioto_profile_control profile = {
         .chip = targets[t],
         .payload0 = gpu1 ? UINT16_C(0x9088) : UINT16_C(0x0555),
         .payload1 = gpu1 ? UINT16_C(0xa800) : 0,
      };
      const struct agx_ioto_preshader_desc preshaders[] = {
         {
            .external_va = AGX_IOTO_PRESHADER_NO_VA,
         },
         {
            .code = inline_code,
            .code_size = sizeof(inline_code),
            .external_va = UINT64_C(0x123456789abc),
         },
         {
            .code = external_code,
            .code_size = sizeof(external_code),
            .external_va = UINT64_C(0x123456789abc),
            .request_prelude = true,
         },
      };

      for (unsigned p = 0; p < sizeof(preshaders) / sizeof(preshaders[0]);
           ++p) {
         uint8_t direct[128], encoded[128];
         memset(direct, 0xa5, sizeof(direct));
         memset(encoded, 0x5a, sizeof(encoded));
         size_t direct_written = SIZE_MAX, encoded_written = SIZE_MAX;
         CHECK(agx_ioto_program_compose_no_pixrel(
            targets[t], UINT64_C(0x100000040), body, sizeof(body), &tail,
            &profile, &preshaders[p], direct, sizeof(direct),
            &direct_written));
         CHECK(d12_agx3_encode_ioto_program_no_pixrel(
            targets[t], UINT64_C(0x100000040), body, sizeof(body), &tail,
            &profile, &preshaders[p], encoded, sizeof(encoded),
            &encoded_written));
         CHECK(encoded_written == direct_written);
         CHECK(memcmp(encoded, direct, direct_written) == 0);

         uint8_t packed_profile[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
         struct agx_ioto_state_tail_output packed_tail;
         struct agx_ioto_preshader_output packed_suffix;
         CHECK(agx_ioto_profile_control_pack(&profile, packed_profile));
         CHECK(agx_ioto_state_tail_pack(targets[t], &tail, &packed_tail));
         CHECK(agx_ioto_preshader_emit(targets[t], &preshaders[p],
                                       &packed_suffix));

         const size_t profile_size =
            agx_ioto_profile_control_size(targets[t]);
         const size_t profile_offset = sizeof(body);
         const size_t tail_offset = profile_offset + profile_size;
         const size_t suffix_offset = tail_offset + packed_tail.length;
         CHECK(encoded_written == suffix_offset + packed_suffix.length);
         CHECK(memcmp(encoded, body, sizeof(body)) == 0);
         CHECK(memcmp(encoded + profile_offset, packed_profile,
                      profile_size) == 0);
         CHECK(memcmp(encoded + tail_offset, packed_tail.bytes,
                      packed_tail.length) == 0);
         CHECK(memcmp(encoded + suffix_offset, packed_suffix.bytes,
                      packed_suffix.length) == 0);
         CHECK(encoded[encoded_written] == 0x5a);

         if (p == 0) {
            CHECK(packed_suffix.mode == AGX_IOTO_PRESHADER_MODE_ABSENT);
            CHECK(packed_suffix.length == AGX_IOTO_PROGRAM_TERMINATE_SIZE);
            CHECK(memcmp(encoded + suffix_offset, "\x0e\x00\x00\x00",
                         AGX_IOTO_PROGRAM_TERMINATE_SIZE) == 0);
         } else if (p == 1) {
            CHECK(packed_suffix.mode == AGX_IOTO_PRESHADER_MODE_INLINE);
            CHECK(packed_suffix.length == 2);
            CHECK(memcmp(encoded + suffix_offset, "\x1b\x0c", 2) == 0);
         } else {
            CHECK(packed_suffix.mode == AGX_IOTO_PRESHADER_MODE_EXTERNAL);
            CHECK(packed_suffix.length == AGX_IOTO_PRESHADER_PRELUDE_SIZE +
                                             AGX_IOTO_PRESHADER_BRANCH_SIZE);
            CHECK(packed_suffix.branch_offset ==
                  AGX_IOTO_PRESHADER_PRELUDE_SIZE);
            CHECK(encoded[suffix_offset] == (gpu1 ? 0x04 : 0x0c));
            CHECK(encoded[suffix_offset + 1] == 0x10);

            struct agx_ioto_record branch;
            size_t consumed;
            CHECK(d12_agx3_validate_ioto_record(
               targets[t], gpu1 ? AGX_IOTO_KIND_B_EP1 : AGX_IOTO_KIND_B_EP0,
               encoded + suffix_offset + packed_suffix.branch_offset,
               encoded_written - suffix_offset - packed_suffix.branch_offset,
               &branch, &consumed));
            CHECK(consumed == AGX_IOTO_PRESHADER_BRANCH_SIZE);
         }
      }
   }

   const struct agx_ioto_state_tail tail =
      make_ioto_state_tail(false, AGX_IOTO_STATE_TAIL_TYPE1);
   const struct agx_ioto_profile_control profile = {.chip = AGX_CHIP_G15G};
   const struct agx_ioto_preshader_desc invalid = {
      .code = external_code,
      .code_size = sizeof(external_code),
      .external_va = 1,
   };
   uint8_t output[64], before[64];
   memset(output, 0xa5, sizeof(output));
   memcpy(before, output, sizeof(before));
   size_t written = 0x1234;
   CHECK(!d12_agx3_encode_ioto_program_no_pixrel(
      AGX_CHIP_G15G, UINT64_C(0x100000040), body, sizeof(body), &tail,
      &profile, &invalid, output, sizeof(output), &written));
   CHECK(written == 0x1234);
   CHECK(memcmp(output, before, sizeof(output)) == 0);
   return 0;
}

int
main(void)
{
   if (test_ioto_raw())
      return 1;
   if (test_ioto_state_tail())
      return 1;
   if (test_ioto_profile())
      return 1;
   if (test_ioto_program_bridge())
      return 1;
   if (test_g17s_minimal_compute_program_bridge())
      return 1;
   if (test_ioto_preshader_bridge())
      return 1;
   if (test_ioto_full_program_bridge())
      return 1;

   const enum agx_chip launch_targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint8_t launch_bytes[AGX3_CDM_LAUNCH_SIZE] = {
      0x67, 0x45, 0x23, 0x01, 0xde, 0xbc, 0x0a, 0x01,
      0x23, 0x01, 0xef, 0x8d, 0xef, 0xbe, 0xad, 0xde,
      0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
      0x07, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
   };
   struct agx3_cdm_launch_info launch = {
      .word0_payload = 0x01234567,
      .word1_payload = 0x000abcde,
      .pipeline = UINT64_C(0x1237bc048c0),
      .opaque_word3 = 0xdeadbeef,
      .global_size = {3, 5, 7},
      .local_size = {8, 16, 32},
   };

   for (unsigned i = 0;
        i < sizeof(launch_targets) / sizeof(launch_targets[0]); ++i) {
      launch.chip = launch_targets[i];
      struct d12_agx3_cdm_launch_encoding encoded;
      CHECK(d12_agx3_encode_cdm_launch(&launch, &encoded));
      CHECK(encoded.length == AGX3_CDM_LAUNCH_SIZE);
      CHECK(memcmp(encoded.bytes, launch_bytes, sizeof(launch_bytes)) == 0);

      struct agx3_cdm_launch_info decoded;
      CHECK(d12_agx3_decode_cdm_launch(
         launch.chip, encoded.bytes, encoded.length, &decoded));
      CHECK(decoded.chip == launch.chip);
      CHECK(decoded.pipeline == launch.pipeline);
      CHECK(decoded.word0_payload == launch.word0_payload);
      CHECK(decoded.word1_payload == launch.word1_payload);
      CHECK(decoded.opaque_word3 == launch.opaque_word3);
      CHECK(memcmp(decoded.global_size, launch.global_size,
                   sizeof(launch.global_size)) == 0);
      CHECK(memcmp(decoded.local_size, launch.local_size,
                   sizeof(launch.local_size)) == 0);

      struct agx3_cdm_decoded_record record;
      CHECK(d12_agx3_decode_cdm_record(launch.chip, encoded.bytes,
                                       encoded.length, &record));
      CHECK(record.kind == AGX3_CDM_RECORD_LAUNCH);
      CHECK(record.length == encoded.length);
      CHECK(record.chip == launch.chip);
      CHECK(record.data.launch.pipeline == launch.pipeline);
      CHECK(!d12_agx3_decode_cdm_launch(
         launch.chip, encoded.bytes, encoded.length - 1, &decoded));
      CHECK(decoded.pipeline == 0);
      CHECK(!d12_agx3_decode_cdm_launch(
         launch.chip, encoded.bytes, encoded.length + 1, &decoded));
   }

   const enum agx_chip rejected_launch[] = {
      AGX_CHIP_G13G, AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A,
      AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0;
        i < sizeof(rejected_launch) / sizeof(rejected_launch[0]); ++i) {
      struct d12_agx3_cdm_launch_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      launch.chip = rejected_launch[i];
      CHECK(!d12_agx3_encode_cdm_launch(&launch, &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }
   CHECK(!d12_agx3_encode_cdm_launch(&launch, NULL));

   const uint8_t indirect_launch_bytes[AGX3_CDM_INDIRECT_LAUNCH_SIZE] = {
      0x67, 0x45, 0x23, 0x01, 0xde, 0xbc, 0x0a, 0x01,
      0x23, 0x01, 0xef, 0x8d, 0xef, 0xbe, 0xad, 0x20,
      0xcd, 0xab, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
   };
   struct agx3_cdm_indirect_launch_info indirect_launch = {
      .word0_payload = UINT32_C(0x01234567),
      .word1_payload = UINT32_C(0x000abcde),
      .pipeline = UINT64_C(0x01237bc048c0),
      .opaque_word3 = UINT32_C(0x20adbeef),
      .indirect_word4_payload = UINT16_C(0x9abc),
      .indirect_parameters = UINT64_C(0xabcd12345678),
   };
   for (unsigned i = 0;
        i < sizeof(launch_targets) / sizeof(launch_targets[0]); ++i) {
      indirect_launch.chip = launch_targets[i];
      struct d12_agx3_cdm_indirect_launch_encoding encoded;
      CHECK(d12_agx3_encode_cdm_indirect_launch(&indirect_launch, &encoded));
      CHECK(encoded.length == AGX3_CDM_INDIRECT_LAUNCH_SIZE);
      CHECK(memcmp(encoded.bytes, indirect_launch_bytes,
                   sizeof(indirect_launch_bytes)) == 0);

      struct agx3_cdm_indirect_launch_info decoded;
      CHECK(d12_agx3_decode_cdm_indirect_launch(
         indirect_launch.chip, encoded.bytes, encoded.length, &decoded));
      CHECK(decoded.chip == indirect_launch.chip);
      CHECK(decoded.word0_payload == indirect_launch.word0_payload);
      CHECK(decoded.word1_payload == indirect_launch.word1_payload);
      CHECK(decoded.pipeline == indirect_launch.pipeline);
      CHECK(decoded.opaque_word3 == indirect_launch.opaque_word3);
      CHECK(decoded.indirect_word4_payload ==
            indirect_launch.indirect_word4_payload);
      CHECK(decoded.indirect_parameters ==
            indirect_launch.indirect_parameters);
      CHECK(!d12_agx3_decode_cdm_indirect_launch(
         indirect_launch.chip, encoded.bytes, encoded.length - 1, &decoded));
      CHECK(decoded.indirect_parameters == 0);
      CHECK(!d12_agx3_decode_cdm_indirect_launch(
         indirect_launch.chip, encoded.bytes, encoded.length + 1, &decoded));
      CHECK(decoded.indirect_parameters == 0);
   }

   for (unsigned i = 0;
        i < sizeof(rejected_launch) / sizeof(rejected_launch[0]); ++i) {
      struct d12_agx3_cdm_indirect_launch_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      indirect_launch.chip = rejected_launch[i];
      CHECK(!d12_agx3_encode_cdm_indirect_launch(&indirect_launch, &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }
   CHECK(!d12_agx3_encode_cdm_indirect_launch(&indirect_launch, NULL));
   struct agx3_cdm_indirect_launch_info decoded_indirect;
   indirect_launch.chip = AGX_CHIP_G17P;
   CHECK(!d12_agx3_decode_cdm_indirect_launch(
      indirect_launch.chip, indirect_launch_bytes,
      sizeof(indirect_launch_bytes), NULL));
   memset(&decoded_indirect, 0xa5, sizeof(decoded_indirect));
   CHECK(!d12_agx3_decode_cdm_indirect_launch(
      indirect_launch.chip, NULL, sizeof(indirect_launch_bytes),
      &decoded_indirect));
   CHECK(decoded_indirect.pipeline == 0);
   uint8_t malformed_indirect[sizeof(indirect_launch_bytes)];
   memcpy(malformed_indirect, indirect_launch_bytes,
          sizeof(malformed_indirect));
   malformed_indirect[3] |= 0x20;
   CHECK(!d12_agx3_decode_cdm_indirect_launch(
      indirect_launch.chip, malformed_indirect, sizeof(malformed_indirect),
      &decoded_indirect));
   CHECK(decoded_indirect.pipeline == 0);

   const uint8_t
      indirect_launch_36_bytes[AGX3_CDM_INDIRECT_LAUNCH_36_SIZE] = {
         0x67, 0x45, 0x23, 0x01, 0xde, 0xbc, 0x0a, 0x01,
         0x23, 0x01, 0xef, 0x8d, 0xef, 0xbe, 0xad, 0x20,
         0xcd, 0xab, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
         0x55, 0x01, 0x00, 0x00, 0xaa, 0x03, 0x00, 0x00,
         0xff, 0x07, 0x00, 0x00,
      };
   struct agx3_cdm_indirect_launch_36_info indirect_launch_36 = {
      .word0_payload = UINT32_C(0x01234567),
      .word1_payload = UINT32_C(0x000abcde),
      .pipeline = UINT64_C(0x01237bc048c0),
      .opaque_word3 = UINT32_C(0x20adbeef),
      .indirect_word4_payload = UINT16_C(0x9abc),
      .indirect_parameters = UINT64_C(0xabcd12345678),
      .trailing_words = {0x155, 0x3aa, 0x7ff},
   };
   for (unsigned i = 0;
        i < sizeof(launch_targets) / sizeof(launch_targets[0]); ++i) {
      indirect_launch_36.chip = launch_targets[i];
      struct d12_agx3_cdm_indirect_launch_36_encoding encoded;
      CHECK(d12_agx3_encode_cdm_indirect_launch_36(&indirect_launch_36,
                                                   &encoded));
      CHECK(encoded.length == AGX3_CDM_INDIRECT_LAUNCH_36_SIZE);
      CHECK(memcmp(encoded.bytes, indirect_launch_36_bytes,
                   sizeof(indirect_launch_36_bytes)) == 0);

      struct agx3_cdm_indirect_launch_36_info decoded;
      CHECK(d12_agx3_decode_cdm_indirect_launch_36(
         indirect_launch_36.chip, encoded.bytes, encoded.length, &decoded));
      CHECK(decoded.chip == indirect_launch_36.chip);
      CHECK(decoded.word0_payload == indirect_launch_36.word0_payload);
      CHECK(decoded.word1_payload == indirect_launch_36.word1_payload);
      CHECK(decoded.pipeline == indirect_launch_36.pipeline);
      CHECK(decoded.opaque_word3 == indirect_launch_36.opaque_word3);
      CHECK(decoded.indirect_word4_payload ==
            indirect_launch_36.indirect_word4_payload);
      CHECK(decoded.indirect_parameters ==
            indirect_launch_36.indirect_parameters);
      CHECK(memcmp(decoded.trailing_words,
                   indirect_launch_36.trailing_words,
                   sizeof(decoded.trailing_words)) == 0);
      CHECK(!d12_agx3_decode_cdm_indirect_launch_36(
         indirect_launch_36.chip, encoded.bytes, encoded.length - 1,
         &decoded));
      CHECK(decoded.indirect_parameters == 0);
      CHECK(!d12_agx3_decode_cdm_indirect_launch_36(
         indirect_launch_36.chip, encoded.bytes, encoded.length + 1,
         &decoded));
      CHECK(decoded.indirect_parameters == 0);
   }

   for (unsigned i = 0;
        i < sizeof(rejected_launch) / sizeof(rejected_launch[0]); ++i) {
      struct d12_agx3_cdm_indirect_launch_36_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      indirect_launch_36.chip = rejected_launch[i];
      CHECK(!d12_agx3_encode_cdm_indirect_launch_36(&indirect_launch_36,
                                                    &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }
   CHECK(!d12_agx3_encode_cdm_indirect_launch_36(&indirect_launch_36,
                                                 NULL));
   struct agx3_cdm_indirect_launch_36_info decoded_indirect_36;
   indirect_launch_36.chip = AGX_CHIP_G17P;
   CHECK(!d12_agx3_decode_cdm_indirect_launch_36(
      indirect_launch_36.chip, indirect_launch_36_bytes,
      sizeof(indirect_launch_36_bytes), NULL));
   memset(&decoded_indirect_36, 0xa5, sizeof(decoded_indirect_36));
   CHECK(!d12_agx3_decode_cdm_indirect_launch_36(
      indirect_launch_36.chip, NULL, sizeof(indirect_launch_36_bytes),
      &decoded_indirect_36));
   CHECK(decoded_indirect_36.pipeline == 0);
   uint8_t malformed_indirect_36[sizeof(indirect_launch_36_bytes)];
   memcpy(malformed_indirect_36, indirect_launch_36_bytes,
          sizeof(malformed_indirect_36));
   malformed_indirect_36[34] = 0x08;
   CHECK(!d12_agx3_decode_cdm_indirect_launch_36(
      indirect_launch_36.chip, malformed_indirect_36,
      sizeof(malformed_indirect_36), &decoded_indirect_36));
   CHECK(decoded_indirect_36.pipeline == 0);

   const enum agx_chip link_targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint8_t link_bytes[AGX3_CDM_STREAM_LINK_SIZE] = {
      0xbc, 0x9a, 0x00, 0x30, 0x78, 0x56, 0x34, 0x12,
   };
   struct agx3_cdm_stream_link_info link = {
      .target = UINT64_C(0x9abc12345678),
      .with_return = true,
   };
   for (unsigned i = 0; i < sizeof(link_targets) / sizeof(link_targets[0]);
        ++i) {
      link.chip = link_targets[i];
      struct d12_agx3_cdm_stream_link_encoding encoded;
      CHECK(d12_agx3_encode_cdm_stream_link(&link, &encoded));
      CHECK(encoded.length == AGX3_CDM_STREAM_LINK_SIZE);
      CHECK(memcmp(encoded.bytes, link_bytes, sizeof(link_bytes)) == 0);

      struct agx3_cdm_stream_link_info decoded;
      CHECK(d12_agx3_decode_cdm_stream_link(
         link.chip, encoded.bytes, encoded.length, &decoded));
      CHECK(decoded.chip == link.chip);
      CHECK(decoded.target == link.target);
      CHECK(decoded.with_return == link.with_return);
      CHECK(!d12_agx3_decode_cdm_stream_link(
         link.chip, encoded.bytes, encoded.length - 1, &decoded));
      CHECK(decoded.target == 0);
      CHECK(!d12_agx3_decode_cdm_stream_link(
         link.chip, encoded.bytes, encoded.length + 1, &decoded));
   }

   const enum agx_chip rejected_link[] = {
      AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected_link) / sizeof(rejected_link[0]);
        ++i) {
      struct d12_agx3_cdm_stream_link_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      link.chip = rejected_link[i];
      CHECK(!d12_agx3_encode_cdm_stream_link(&link, &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }
   CHECK(!d12_agx3_encode_cdm_stream_link(&link, NULL));

   for (unsigned i = 0;
        i < sizeof(launch_targets) / sizeof(launch_targets[0]); ++i) {
      for (uint8_t count = 0; count < 8; ++count) {
         struct agx3_cdm_link_padding_info padding = {
            .chip = launch_targets[i],
            .count = count,
         };
         struct d12_agx3_cdm_link_padding_encoding encoded;
         struct agx3_cdm_link_padding_info decoded;
         CHECK(d12_agx3_encode_cdm_link_padding(&padding, &encoded));
         CHECK(encoded.length == AGX3_CDM_LINK_PADDING_SIZE);
         CHECK(agx3_cmdstream_load_le32(encoded.bytes) ==
               (UINT32_C(0xa0000000) | count));
         CHECK(d12_agx3_decode_cdm_link_padding(
            padding.chip, encoded.bytes, encoded.length, &decoded));
         CHECK(decoded.count == count);
         CHECK(!d12_agx3_decode_cdm_link_padding(
            padding.chip, encoded.bytes, encoded.length + 1, &decoded));
         CHECK(decoded.count == 0);
      }
   }

   struct agx3_cdm_link_padding_info rejected_padding = {
      .chip = AGX_CHIP_G17A,
      .count = 0,
   };
   struct d12_agx3_cdm_link_padding_encoding padding_encoding;
   memset(&padding_encoding, 0xff, sizeof(padding_encoding));
   CHECK(!d12_agx3_encode_cdm_link_padding(&rejected_padding,
                                           &padding_encoding));
   CHECK(padding_encoding.length == 0);
   for (unsigned b = 0; b < sizeof(padding_encoding.bytes); ++b)
      CHECK(padding_encoding.bytes[b] == 0);
   CHECK(!d12_agx3_encode_cdm_link_padding(&rejected_padding, NULL));

   const enum agx_chip barrier_targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   for (unsigned i = 0;
        i < sizeof(barrier_targets) / sizeof(barrier_targets[0]); ++i) {
      const bool gen7 = barrier_targets[i] == AGX_CHIP_G17G ||
                        barrier_targets[i] == AGX_CHIP_G17S ||
                        barrier_targets[i] == AGX_CHIP_G17C;
      const uint8_t end = gen7 ? 16 : 6;
      for (uint8_t flags = 0; flags < end; ++flags) {
         if (!gen7 && flags != 0 && flags != 5)
            continue;
         struct agx3_cdm_barrier_info barrier = {
            .chip = barrier_targets[i],
            .flags = flags,
         };
         struct d12_agx3_cdm_barrier_encoding encoded;
         struct agx3_cdm_barrier_info decoded;
         CHECK(d12_agx3_encode_cdm_barrier(&barrier, &encoded));
         CHECK(encoded.length == AGX3_CDM_BARRIER_SIZE);
         CHECK(agx3_cmdstream_load_le32(encoded.bytes) ==
               agx3_cdm_barrier_word(flags));
         CHECK(d12_agx3_decode_cdm_barrier(
            barrier.chip, encoded.bytes, encoded.length, &decoded));
         CHECK(decoded.flags == flags);
         CHECK(!d12_agx3_decode_cdm_barrier(
            barrier.chip, encoded.bytes, encoded.length + 1, &decoded));
         CHECK(decoded.flags == 0);
      }
   }

   struct agx3_cdm_barrier_info rejected_barrier = {
      .chip = AGX_CHIP_G17A,
      .flags = 0,
   };
   struct d12_agx3_cdm_barrier_encoding barrier_encoding;
   memset(&barrier_encoding, 0xff, sizeof(barrier_encoding));
   CHECK(!d12_agx3_encode_cdm_barrier(&rejected_barrier,
                                      &barrier_encoding));
   CHECK(barrier_encoding.length == 0);
   for (unsigned b = 0; b < sizeof(barrier_encoding.bytes); ++b)
      CHECK(barrier_encoding.bytes[b] == 0);
   CHECK(!d12_agx3_encode_cdm_barrier(&rejected_barrier, NULL));

   for (unsigned i = 0;
        i < sizeof(barrier_targets) / sizeof(barrier_targets[0]); ++i) {
      const bool gen7 = barrier_targets[i] == AGX_CHIP_G17G ||
                        barrier_targets[i] == AGX_CHIP_G17S ||
                        barrier_targets[i] == AGX_CHIP_G17C;
      const uint8_t flags[] = {0, 5, 0xf};
      const uint8_t substreams[] = {0, 1, 0x7f, 0xfe};
      for (unsigned f = 0; f < sizeof(flags) / sizeof(flags[0]); ++f) {
         if (!gen7 && flags[f] == 0xf)
            continue;
         for (unsigned s = 0;
              s < sizeof(substreams) / sizeof(substreams[0]); ++s) {
            struct agx3_cdm_substream_barrier_info barrier = {
               .chip = barrier_targets[i],
               .flags = flags[f],
               .substream = substreams[s],
            };
            struct d12_agx3_cdm_barrier_encoding encoded;
            struct agx3_cdm_substream_barrier_info decoded;
            CHECK(d12_agx3_encode_cdm_substream_barrier(&barrier, &encoded));
            CHECK(encoded.length == AGX3_CDM_BARRIER_SIZE);
            CHECK(d12_agx3_decode_cdm_substream_barrier(
               barrier.chip, encoded.bytes, encoded.length, &decoded));
            CHECK(decoded.flags == barrier.flags);
            CHECK(decoded.substream == barrier.substream);
            CHECK(!d12_agx3_decode_cdm_substream_barrier(
               barrier.chip, encoded.bytes, encoded.length + 1, &decoded));
            CHECK(decoded.substream == 0);
         }
      }
   }

   struct agx3_cdm_substream_barrier_info rejected_substream = {
      .chip = AGX_CHIP_G17G,
      .flags = 0,
      .substream = UINT8_MAX,
   };
   memset(&barrier_encoding, 0xff, sizeof(barrier_encoding));
   CHECK(!d12_agx3_encode_cdm_substream_barrier(&rejected_substream,
                                                 &barrier_encoding));
   CHECK(barrier_encoding.length == 0);
   for (unsigned b = 0; b < sizeof(barrier_encoding.bytes); ++b)
      CHECK(barrier_encoding.bytes[b] == 0);

   const enum agx_chip secondary_targets[] = {
      AGX_CHIP_G17G,
      AGX_CHIP_G17S,
      AGX_CHIP_G17C,
   };
   for (unsigned i = 0;
        i < sizeof(secondary_targets) / sizeof(secondary_targets[0]); ++i) {
      struct d12_agx3_cdm_secondary_barrier_encoding encoded;
      CHECK(d12_agx3_encode_cdm_secondary_barrier(secondary_targets[i],
                                                   &encoded));
      CHECK(encoded.length == AGX3_CDM_SECONDARY_BARRIER_SIZE);
      CHECK(agx3_cmdstream_load_le32(encoded.bytes) == UINT32_C(0x60000502));
      CHECK(d12_agx3_validate_cdm_secondary_barrier(
         secondary_targets[i], encoded.bytes, encoded.length));
      CHECK(!d12_agx3_validate_cdm_secondary_barrier(
         secondary_targets[i], encoded.bytes, encoded.length + 1));
   }

   struct d12_agx3_cdm_secondary_barrier_encoding secondary;
   memset(&secondary, 0xff, sizeof(secondary));
   CHECK(!d12_agx3_encode_cdm_secondary_barrier(AGX_CHIP_G17P, &secondary));
   CHECK(secondary.length == 0);
   for (unsigned b = 0; b < sizeof(secondary.bytes); ++b)
      CHECK(secondary.bytes[b] == 0);

   for (unsigned i = 0;
        i < sizeof(launch_targets) / sizeof(launch_targets[0]); ++i) {
      struct d12_agx3_cdm_stream_terminate_encoding encoded;
      CHECK(d12_agx3_encode_cdm_stream_terminate(launch_targets[i],
                                                 &encoded));
      CHECK(encoded.length == AGX3_CDM_STREAM_TERMINATE_SIZE);
      CHECK(agx3_cmdstream_load_le32(encoded.bytes) == UINT32_C(0x40000000));
      CHECK(d12_agx3_validate_cdm_stream_terminate(
         launch_targets[i], encoded.bytes, encoded.length));
      CHECK(!d12_agx3_validate_cdm_stream_terminate(
         launch_targets[i], encoded.bytes, encoded.length + 1));
   }

   struct d12_agx3_cdm_stream_terminate_encoding terminate;
   memset(&terminate, 0xff, sizeof(terminate));
   CHECK(!d12_agx3_encode_cdm_stream_terminate(AGX_CHIP_G16P, &terminate));
   CHECK(terminate.length == 0);
   for (unsigned b = 0; b < sizeof(terminate.bytes); ++b)
      CHECK(terminate.bytes[b] == 0);
   CHECK(!d12_agx3_encode_cdm_stream_terminate(AGX_CHIP_G17S, NULL));

   printf("d12 AGX3 command stream: %u checks passed\n", checks);
   return 0;
}
