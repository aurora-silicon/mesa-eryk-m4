/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_rt_bvh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void
require(bool condition, const char *message)
{
   ++checks;
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

static void
set_field(uint64_t *word, unsigned start, unsigned width, uint32_t value)
{
   const uint64_t field_mask = (UINT64_C(1) << width) - 1;
   const uint64_t mask = field_mask << start;

   *word = (*word & ~mask) | (((uint64_t)value & field_mask) << start);
}

static void
set_slot(struct agx_rt_bvh_gen5_header *header, unsigned slot,
         uint32_t value)
{
   set_field(&header->words[slot >> 1], 16 + (3 * (slot & 1)), 3, value);
}

static void
set_child_space_slot(struct agx_rt_bvh_gen5_header *header, int32_t slot)
{
   const uint32_t encoded = (uint32_t)slot;

   set_field(&header->words[5], 0, 16, encoded >> 16);
   set_field(&header->words[6], 0, 16, encoded & 0xffffu);
}

static void
set_qtb_occupancy(struct agx_rt_bvh_gen5_header *header, unsigned qtb,
                  uint32_t occupancy)
{
   set_field(&header->words[6], 16 + (2 * qtb), 2, occupancy & 3u);
   set_field(&header->words[7], 16 + (2 * qtb), 2, occupancy >> 2);
}

static struct agx_rt_bvh_gen5_header
make_config4_layout(void)
{
   struct agx_rt_bvh_gen5_header header = {0};

   set_field(&header.words[0], 0, 3, 4); /* configuration */
   set_field(&header.words[0], 3, 4, 3); /* inner-child boundary */
   set_slot(&header, 0, 1);
   set_slot(&header, 1, 2);
   set_slot(&header, 2, 3);
   set_slot(&header, 3, 1);
   set_slot(&header, 4, 2);
   set_slot(&header, 5, 0);
   set_slot(&header, 6, 1);
   set_slot(&header, 7, 0);

   /* Four configuration-4 VRB spans: 7 is the special encoding for 8. */
   set_slot(&header, 8, 7);
   set_slot(&header, 9, 2);
   set_slot(&header, 10, 0);
   set_slot(&header, 11, 1);

   set_qtb_occupancy(&header, 0, 1);
   set_qtb_occupancy(&header, 1, 2);
   set_qtb_occupancy(&header, 2, 3);
   set_child_space_slot(&header, 100);
   return header;
}

static void
test_unpack_and_signed_child_space(void)
{
   uint8_t storage[AGX_RT_BVH_GEN5_HEADER_SIZE + 1] = {0};
   uint8_t *packed = storage + 1; /* exercise an unaligned mapped node */

   const uint64_t words[8] = {
      UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210),
      UINT64_C(0x8877665544332211), UINT64_C(0x1020304050607080),
      UINT64_C(0x1112131415161718), UINT64_C(0xabcdefff00008000),
      UINT64_C(0x1234567800000001), UINT64_C(0x5555aaaa0000ffff),
   };

   for (unsigned word = 0; word < 8; ++word) {
      for (unsigned byte = 0; byte < 8; ++byte)
         packed[(word * 8) + byte] = (uint8_t)(words[word] >> (byte * 8));
   }

   struct agx_rt_bvh_gen5_header header;
   require(agx_rt_bvh_gen5_header_unpack(packed, &header),
           "an unaligned little-endian Gen5 header unpacks");
   require(memcmp(header.words, words, sizeof(words)) == 0,
           "all eight words unpack without host-endian or alignment leakage");
   require(agx_rt_bvh_gen5_child_space_slot(&header) ==
              (int32_t)UINT32_C(0x80000001),
           "child space is the signed w5-low:w6-low value");
   require(!agx_rt_bvh_gen5_header_unpack(NULL, &header),
           "a null packed node is refused");
   require(!agx_rt_bvh_gen5_header_unpack(packed, NULL),
           "a null unpack destination is refused");

   struct agx_rt_bvh_gen5_header negative = {0};
   set_child_space_slot(&negative, -2);
   set_slot(&negative, 0, 1);
   require(agx_rt_bvh_gen5_child_space_slot(&negative) == -2,
           "negative child-space slots preserve their sign");
   require(agx_rt_bvh_gen5_offset_child_qtbs_bytes(&negative) == -64,
           "signed slot arithmetic remains signed through byte addressing");
}

static void
test_leaf_and_inner_counts(void)
{
   struct agx_rt_bvh_gen5_header header = {0};
   set_field(&header.words[0], 3, 4, 3);
   set_slot(&header, 0, 0);
   set_slot(&header, 1, 2);
   set_slot(&header, 2, 7);
   set_slot(&header, 3, 1);
   set_slot(&header, 4, 2);
   set_slot(&header, 5, 3);
   set_slot(&header, 6, 0);
   set_slot(&header, 7, 1);

   require(agx_rt_bvh_gen5_leaf_count(&header) == 17,
           "count mode treats zero as eight leaf records");
   require(agx_rt_bvh_gen5_inner_slots(&header) == 7,
           "inner slots are summed from the boundary through slot seven");

   set_field(&header.words[0], 7, 1, 1);
   require(agx_rt_bvh_gen5_uses_indexed_leaf_counts(&header),
           "w0 bit 7 selects indexed leaf counts");
   require(agx_rt_bvh_gen5_leaf_count(&header) == 8,
           "indexed mode decodes the maximum zero-based index plus one");

   set_field(&header.words[0], 3, 4, 0);
   require(agx_rt_bvh_gen5_leaf_count(&header) == 0,
           "a zero boundary owns no leaf records");

   set_field(&header.words[0], 3, 4, 8);
   require(agx_rt_bvh_gen5_inner_slots(&header) == 0,
           "a boundary beyond the eight inner fields has no inner slots");
   require(agx_rt_bvh_gen5_slot(&header, 16) == 0,
           "out-of-range slot reads fail closed");
}

static void
test_qtb_and_vrb_fields(void)
{
   struct agx_rt_bvh_gen5_header header = {0};
   set_field(&header.words[0], 0, 3, 4);
   set_field(&header.words[0], 3, 4, 3);
   set_slot(&header, 0, 5);
   set_slot(&header, 1, 6);
   set_slot(&header, 2, 4);
   set_qtb_occupancy(&header, 0, 5);
   set_qtb_occupancy(&header, 1, 6);
   set_qtb_occupancy(&header, 2, 4);

   uint32_t a, b, c;
   agx_rt_bvh_gen5_qtb_occupancies(&header, &a, &b, &c);
   require(a == 5 && b == 6 && c == 4,
           "QTB occupancies combine low w6 and high w7 halves");
   require(agx_rt_bvh_gen5_qtb_count(&header) == 3,
           "the highest occupied QTB determines a three-QTB section");
   require(agx_rt_bvh_gen5_qtb_layout_valid(&header),
           "matching QTB occupancies are accepted");

   set_qtb_occupancy(&header, 2, 3);
   require(agx_rt_bvh_gen5_qtb_count(&header) ==
              AGX_RT_BVH_GEN5_INVALID_QTB_COUNT,
           "an occupancy sum mismatch produces Apple's sentinel");
   require(!agx_rt_bvh_gen5_qtb_layout_valid(&header),
           "the QTB sentinel is classified as malformed");

   set_field(&header.words[0], 0, 3, 1);
   require(agx_rt_bvh_gen5_qtb_count(&header) == 0,
           "configurations outside 2..4 have no QTB section");

   set_slot(&header, 8, 7);
   set_slot(&header, 9, 2);
   set_slot(&header, 10, 0);
   set_slot(&header, 11, 1);
   require(agx_rt_bvh_gen5_vrb_size_bytes(&header) == 352,
           "VRB spans include the seven-means-eight encoding");
}

static void
test_offsets_for_every_configuration(void)
{
   static const int64_t expected_hw[8] = {
      6656, 6656, 6848, 6656, 6656, 6656, 6656, 6656,
   };
   static const int64_t expected_vrbs[8] = {
      6656, 6656, 6848, 6848, 6848, 6656, 6656, 6656,
   };
   static const int64_t expected_lhb[8] = {
      6656, 6880, 7072, 6848, 7200, 0, 7040, 6656,
   };
   static const int64_t expected_leaves[8] = {
      6680, 6904, 7096, 6872, 7224, 4, 7040, 6680,
   };

   for (uint32_t config = 0; config < 8; ++config) {
      struct agx_rt_bvh_gen5_header header = make_config4_layout();
      set_field(&header.words[0], 0, 3, config);

      require(agx_rt_bvh_gen5_leaf_count(&header) == 6,
              "the common vector owns six leaves");
      require(agx_rt_bvh_gen5_inner_slots(&header) == 4,
              "the common vector owns four inner slots");
      require(agx_rt_bvh_gen5_offset_child_qtbs_bytes(&header) == 6656,
              "child QTBs begin after the inner-child slots");
      require(agx_rt_bvh_gen5_offset_child_hw_leaves_bytes(&header) ==
                 expected_hw[config],
              "hardware-leaf offset matches the configuration table");
      require(agx_rt_bvh_gen5_offset_vrbs_bytes(&header) ==
                 expected_vrbs[config],
              "VRB offset matches the configuration table");
      require(agx_rt_bvh_gen5_offset_lhb_bytes(&header) ==
                 expected_lhb[config],
              "LHB offset matches the configuration table");
      require(agx_rt_bvh_gen5_offset_child_leaves_bytes(&header) ==
                 expected_leaves[config],
              "child-leaf offset matches the configuration table");
   }

   struct agx_rt_bvh_gen5_header special = make_config4_layout();
   set_field(&special.words[0], 0, 3, 7);
   set_field(&special.words[7], 8, 5, 0x1a);
   require(agx_rt_bvh_gen5_offset_child_leaves_bytes(&special) == 6656,
           "config 7 leaf type 0x1a has no LHB extent");

   set_field(&special.words[0], 0, 3, 0);
   require(agx_rt_bvh_gen5_offset_child_leaves_bytes(&special) == 6660,
           "a single-entry leaf type adds one four-byte LHB entry");
}

static bool
all_decoded_fields_equal(const struct agx_rt_bvh_gen5_header *a,
                         const struct agx_rt_bvh_gen5_header *b)
{
   uint32_t aa, ab, ac, ba, bb, bc;
   agx_rt_bvh_gen5_qtb_occupancies(a, &aa, &ab, &ac);
   agx_rt_bvh_gen5_qtb_occupancies(b, &ba, &bb, &bc);

   return agx_rt_bvh_gen5_config(a) == agx_rt_bvh_gen5_config(b) &&
          agx_rt_bvh_gen5_inner_child_boundary(a) ==
             agx_rt_bvh_gen5_inner_child_boundary(b) &&
          agx_rt_bvh_gen5_uses_indexed_leaf_counts(a) ==
             agx_rt_bvh_gen5_uses_indexed_leaf_counts(b) &&
          agx_rt_bvh_gen5_leaf_type(a) == agx_rt_bvh_gen5_leaf_type(b) &&
          agx_rt_bvh_gen5_child_space_slot(a) ==
             agx_rt_bvh_gen5_child_space_slot(b) &&
          agx_rt_bvh_gen5_leaf_count(a) == agx_rt_bvh_gen5_leaf_count(b) &&
          agx_rt_bvh_gen5_inner_slots(a) == agx_rt_bvh_gen5_inner_slots(b) &&
          aa == ba && ab == bb && ac == bc &&
          agx_rt_bvh_gen5_qtb_count(a) == agx_rt_bvh_gen5_qtb_count(b) &&
          agx_rt_bvh_gen5_vrb_size_bytes(a) ==
             agx_rt_bvh_gen5_vrb_size_bytes(b) &&
          agx_rt_bvh_gen5_offset_child_qtbs_bytes(a) ==
             agx_rt_bvh_gen5_offset_child_qtbs_bytes(b) &&
          agx_rt_bvh_gen5_offset_child_hw_leaves_bytes(a) ==
             agx_rt_bvh_gen5_offset_child_hw_leaves_bytes(b) &&
          agx_rt_bvh_gen5_offset_vrbs_bytes(a) ==
             agx_rt_bvh_gen5_offset_vrbs_bytes(b) &&
          agx_rt_bvh_gen5_offset_lhb_bytes(a) ==
             agx_rt_bvh_gen5_offset_lhb_bytes(b) &&
          agx_rt_bvh_gen5_offset_child_leaves_bytes(a) ==
             agx_rt_bvh_gen5_offset_child_leaves_bytes(b);
}

static void
test_unknown_bits_do_not_affect_addressing(void)
{
   const struct agx_rt_bvh_gen5_header reference = make_config4_layout();

   /* Exhaust all 2^11 combinations of the still-unidentified bits. */
   for (uint32_t pattern = 0; pattern < (1u << 11); ++pattern) {
      struct agx_rt_bvh_gen5_header candidate = reference;
      set_field(&candidate.words[0], 8, 8, pattern & 0xffu);
      set_field(&candidate.words[7], 13, 3, pattern >> 8);
      if (!all_decoded_fields_equal(&reference, &candidate)) {
         require(false,
                 "unknown Gen5 bits changed a proved child-layout accessor");
      }
   }

   require(true,
           "all 2^11 unknown-bit combinations leave child addressing unchanged");
}

int
main(void)
{
   require(sizeof(struct agx_rt_bvh_gen5_header) ==
              AGX_RT_BVH_GEN5_HEADER_SIZE,
           "a Gen5 node header is exactly 64 bytes");
   test_unpack_and_signed_child_space();
   test_leaf_and_inner_counts();
   test_qtb_and_vrb_fields();
   test_offsets_for_every_configuration();
   test_unknown_bits_do_not_affect_addressing();

   printf("agx_rt_bvh: PASS (%u checks)\n", checks);
   return 0;
}
