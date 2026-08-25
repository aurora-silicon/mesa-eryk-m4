/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "hk_agx3_ioto_decode.h"
#include "hk_agx3_ioto_state_core.h"

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

static struct agx_ioto_state_tail
make_tail(bool gpu1, enum agx_ioto_state_tail_type type)
{
   struct agx_ioto_state_tail tail = {.type = type};

   if (gpu1) {
      tail.speclm.f[0] = 3;
      tail.speclm.f[1] = 165;
      tail.speclm.f[2] = 1;
      tail.speclm.f[4] = 1;
      tail.speclm.f[6] = 1;
      tail.speclm.f[8] = 1;
      tail.speclm.f[18] = 0xabcd;
      tail.speclm.f[21] = 2;
      tail.speclm.f[22] = 0x5a;
      tail.speclm.f[24] = 1;
      tail.speclm.f[26] = 1;

      tail.ldshdr.f[0] = UINT64_C(0x123456789ac0);
      tail.ldshdr.f[1] = 2;
      tail.ldshdr.f[3] = 1;
      tail.ldshdr.f[5] = 3;
      tail.ldshdr.f[6] = 107;
      tail.ldshdr.f[7] = 1;
      tail.ldshdr.f[9] = 1;
      tail.ldshdr.f[11] = 1;
      tail.ldshdr.f[13] = 1;
      tail.ldshdr.f[23] = 1;
      tail.ldshdr.f[24] = 1;

      tail.spectpr.f[0] = 3;
      tail.spectpr.f[1] = 166;
      tail.spectpr.f[2] = 1;
      tail.spectpr.f[4] = 1;
      tail.spectpr.f[6] = 1;
      tail.spectpr.f[8] = 1;
      tail.spectpr.f[10] = 0x6d;
      tail.spectpr.f[11] = 1;
      tail.spectpr.f[20] = 0xa55a;
      tail.spectpr.f[21] = 0x5aa5;
      tail.spectpr.f[22] = 0xafe;
      tail.spectpr.f[23] = 1;
   } else {
      tail.speclm.f[2] = 1;
      tail.speclm.f[4] = 1;
      tail.speclm.f[6] = 1;
      tail.speclm.f[10] = 2;
      tail.speclm.f[11] = 1;
      tail.speclm.f[18] = 0xa5c0;
      tail.speclm.f[21] = 3;
      tail.speclm.f[22] = 0xd8;
      tail.speclm.f[24] = 1;
      tail.speclm.f[25] = 1;

      tail.ldshdr.f[0] = UINT64_C(0x123456789ac0);
      tail.ldshdr.f[1] = 3;
      tail.ldshdr.f[7] = 1;
      tail.ldshdr.f[9] = 1;
      tail.ldshdr.f[11] = 1;
      tail.ldshdr.f[15] = 2;
      tail.ldshdr.f[16] = 1;
      tail.ldshdr.f[23] = 1;

      tail.spectpr.f[2] = 1;
      tail.spectpr.f[4] = 1;
      tail.spectpr.f[6] = 1;
      tail.spectpr.f[10] = 0x55;
      tail.spectpr.f[11] = 1;
      tail.spectpr.f[12] = 2;
      tail.spectpr.f[13] = 1;
      tail.spectpr.f[20] = 0xa55a;
      tail.spectpr.f[21] = 0x5aa5;
      tail.spectpr.f[22] = 0x7fe;
   }

   if (type == AGX_IOTO_STATE_TAIL_TYPE1)
      memset(&tail.speclm, 0, sizeof(tail.speclm));

   return tail;
}

static int
validate_tail(enum agx_chip chip, enum agx_ioto_state_tail_type type,
              const struct hk_agx3_ioto_state_tail_encoding *encoded)
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
      CHECK(hk_agx3_ioto_validate_record(
         chip, kinds[i], encoded->bytes + offset, encoded->length - offset,
         &record, &consumed));
      CHECK(record.kind == kinds[i]);
      CHECK(record.length == consumed);
      CHECK(memcmp(record.bytes, encoded->bytes + offset, consumed) == 0);
      offset += consumed;
   }

   CHECK(offset == encoded->length);
   return 0;
}

static int
test_target_parity(void)
{
   static const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   struct hk_agx3_ioto_state_tail_encoding cohort_reference[2][2];
   bool have_reference[2][2] = {{false}};

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool gpu1 = t >= 8;

      struct hk_agx3_ioto_state_tail_encoding encoded[2];
      for (unsigned type = 0; type < 2; ++type) {
         const struct agx_ioto_state_tail tail =
            make_tail(gpu1, (enum agx_ioto_state_tail_type)type);
         struct agx_ioto_state_tail_output shared;
         CHECK(agx_ioto_state_tail_pack(targets[t], &tail, &shared));

         memset(&encoded[type], 0xa5, sizeof(encoded[type]));
         CHECK(hk_agx3_ioto_state_tail_generate(targets[t], &tail,
                                                &encoded[type]));
         CHECK(encoded[type].length == shared.length);
         CHECK(encoded[type].length ==
               (type == 0 ? AGX_IOTO_STATE_TAIL_TYPE0_SIZE
                          : AGX_IOTO_STATE_TAIL_TYPE1_SIZE));
         CHECK(memcmp(encoded[type].bytes, shared.bytes, shared.length) == 0);
         if (validate_tail(targets[t], tail.type, &encoded[type]))
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
         struct hk_agx3_ioto_state_tail_encoding shader_encoded;
         memset(&shader_encoded, 0x5a, sizeof(shader_encoded));
         CHECK(hk_agx3_ioto_state_tail_generate_shader(
            targets[t], &shader_tail, UINT64_C(0x123456789ac0),
            &shader_encoded));
         CHECK(memcmp(&shader_encoded, &encoded[type], sizeof(encoded[type])) ==
               0);
      }

      CHECK(memcmp(encoded[0].bytes + AGX_IOTO_SPECLM_SIZE, encoded[1].bytes,
                   encoded[1].length) == 0);
   }

   return 0;
}

static int
test_failure_preserves_output(void)
{
   struct agx_ioto_state_tail tail =
      make_tail(false, AGX_IOTO_STATE_TAIL_TYPE0);
   struct hk_agx3_ioto_state_tail_encoding out;
   memset(&out, 0xa5, sizeof(out));
   const struct hk_agx3_ioto_state_tail_encoding before = out;

#define CHECK_UNCHANGED(call)                                                  \
   do {                                                                        \
      CHECK(!(call));                                                          \
      CHECK(memcmp(&out, &before, sizeof(out)) == 0);                          \
   } while (0)

   CHECK_UNCHANGED(hk_agx3_ioto_state_tail_generate(AGX_CHIP_G14X, &tail,
                                                     &out));
   tail.type = (enum agx_ioto_state_tail_type)2;
   CHECK_UNCHANGED(hk_agx3_ioto_state_tail_generate(AGX_CHIP_G15G, &tail,
                                                     &out));
   tail = make_tail(false, AGX_IOTO_STATE_TAIL_TYPE0);
   tail.ldshdr.f[0] |= 1;
   CHECK_UNCHANGED(hk_agx3_ioto_state_tail_generate(AGX_CHIP_G15G, &tail,
                                                     &out));
   tail = make_tail(false, AGX_IOTO_STATE_TAIL_TYPE1);
   tail.speclm.f[2] = 1;
   CHECK_UNCHANGED(hk_agx3_ioto_state_tail_generate(AGX_CHIP_G15G, &tail,
                                                     &out));

   tail = make_tail(false, AGX_IOTO_STATE_TAIL_TYPE0);
   CHECK_UNCHANGED(hk_agx3_ioto_state_tail_generate_shader(
      AGX_CHIP_G15G, &tail, UINT64_C(0x123456789ac0), &out));
   tail.ldshdr.f[0] = 0;
   CHECK_UNCHANGED(hk_agx3_ioto_state_tail_generate_shader(
      AGX_CHIP_G15G, &tail, UINT64_C(0x123456789ac1), &out));
   CHECK_UNCHANGED(hk_agx3_ioto_state_tail_generate_shader(
      AGX_CHIP_G15G, &tail, UINT64_C(1) << 48, &out));
   CHECK_UNCHANGED(
      hk_agx3_ioto_state_tail_generate(AGX_CHIP_G15G, NULL, &out));
   CHECK(!hk_agx3_ioto_state_tail_generate(AGX_CHIP_G15G, &tail, NULL));

#undef CHECK_UNCHANGED
   return 0;
}

int
main(void)
{
   if (test_target_parity() || test_failure_preserves_output())
      return 1;

   printf("Honeykrisp AGX3 Ioto state core: %u checks passed\n", checks);
   return 0;
}
