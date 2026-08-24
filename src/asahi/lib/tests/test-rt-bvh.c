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
set_gen1_control(struct agx_rt_bvh_gen1_header *header, uint32_t control)
{
   set_field(&header->words[7], 0, 6, control);
}

static void
set_gen1_leaf_lane_end(struct agx_rt_bvh_gen1_header *header, unsigned lane,
                       uint32_t low, uint32_t high_field)
{
   set_field(&header->words[lane], 16, 4, low);
   set_field(&header->words[lane], 20, 2, high_field);
}

static void
set_gen1_child_space_slot(struct agx_rt_bvh_gen1_header *header,
                          uint32_t slot)
{
   set_field(&header->words[5], 0, 16, slot >> 16);
   set_field(&header->words[6], 0, 16, slot & 0xffffu);
}

static uint32_t
reference_gen1_inner_child_count(uint32_t control)
{
   uint32_t count = control & 7u;

   if ((control & 0x3fu) == 0x3fu)
      count = 0;
   if (((control | (control >> 3)) & 7u) == 0)
      count = 8;

   return count;
}

static uint32_t
reference_gen1_leaf_box_count(uint32_t control)
{
   const uint32_t fields_intersection = (control & (control >> 3)) & 7u;
   return fields_intersection == 7u ? 8u : ((control >> 3) & 7u);
}

static uint32_t
reference_gen1_leaf_count(const struct agx_rt_bvh_gen1_header *header)
{
   const uint32_t control = (uint32_t)header->words[7] & 0x3fu;
   const uint32_t leaf_boxes = reference_gen1_leaf_box_count(control);

   if ((((uint32_t)header->words[0]) & 1u) != 0)
      return leaf_boxes;

   const uint32_t first = reference_gen1_inner_child_count(control);
   uint32_t maximum = 0;

   for (uint32_t i = 0; i < leaf_boxes && first + i < 8; ++i) {
      const uint32_t lane = (uint32_t)header->words[first + i];
      const uint32_t low = (lane >> 16) & 15u;
      const uint32_t high_field = (lane >> 20) & 3u;
      const uint32_t end = low + (high_field ? high_field : 4u);
      if (end > maximum)
         maximum = end;
   }

   return maximum;
}

static uint64_t
next_random(uint64_t *state)
{
   uint64_t value = *state;
   value ^= value << 13;
   value ^= value >> 7;
   value ^= value << 17;
   *state = value;
   return value;
}

static void
test_gen1_unpack_and_control(void)
{
   uint8_t storage[AGX_RT_BVH_GEN1_HEADER_SIZE + 1] = {0};
   uint8_t *packed = storage + 1;
   struct agx_rt_bvh_gen1_header header = {0};

   for (unsigned word = 0; word < 8; ++word) {
      const uint64_t value = UINT64_C(0x1020304050607080) + word;
      for (unsigned byte = 0; byte < 8; ++byte)
         packed[(word * 8) + byte] = (uint8_t)(value >> (byte * 8));
   }

   require(agx_rt_bvh_gen1_header_unpack(packed, &header),
           "an unaligned little-endian Gen1 header unpacks");
   for (unsigned word = 0; word < 8; ++word) {
      require(header.words[word] == UINT64_C(0x1020304050607080) + word,
              "each Gen1 header word preserves little-endian byte order");
   }
   require(!agx_rt_bvh_gen1_header_unpack(NULL, &header),
           "a null Gen1 packed node is refused");
   require(!agx_rt_bvh_gen1_header_unpack(packed, NULL),
           "a null Gen1 unpack destination is refused");

   for (uint32_t control = 0; control < 64; ++control) {
      set_gen1_control(&header, control);
      const uint32_t inner = reference_gen1_inner_child_count(control);
      const uint32_t leaves = reference_gen1_leaf_box_count(control);

      require(agx_rt_bvh_gen1_control(&header) == control,
              "all six Gen1 control bits round-trip");
      require(agx_rt_bvh_gen1_inner_child_count(&header) == inner,
              "the Gen1 inner-child endpoint encoding matches Apple");
      require(agx_rt_bvh_gen1_leaf_box_count(&header) == leaves,
              "the Gen1 leaf-box endpoint encoding matches Apple");
      require(agx_rt_bvh_gen1_branching_layout_valid(&header) ==
                 (inner + leaves <= 8),
              "the Gen1 BVH8 branching-factor gate matches Apple");
   }

   set_gen1_control(&header, 0);
   require(agx_rt_bvh_gen1_inner_child_count(&header) == 8 &&
              agx_rt_bvh_gen1_leaf_box_count(&header) == 0,
           "control zero is the all-inner endpoint");
   set_gen1_control(&header, 0x3f);
   require(agx_rt_bvh_gen1_inner_child_count(&header) == 0 &&
              agx_rt_bvh_gen1_leaf_box_count(&header) == 8,
           "control 0x3f is the all-leaf endpoint");
}

static void
test_gen1_leaf_layout(void)
{
   struct agx_rt_bvh_gen1_header header = {0};

   for (unsigned lane = 0; lane < 8; ++lane) {
      for (uint32_t low = 0; low < 16; ++low) {
         for (uint32_t high = 0; high < 4; ++high) {
            set_gen1_leaf_lane_end(&header, lane, low, high);
            require(agx_rt_bvh_gen1_leaf_lane_end(&header, lane) ==
                       low + (high ? high : 4u),
                    "every Gen1 4+2-bit leaf-lane end decodes");
         }
      }
   }
   require(agx_rt_bvh_gen1_leaf_lane_end(&header, 8) == 0,
           "an out-of-range Gen1 leaf lane fails closed");

   header = (struct agx_rt_bvh_gen1_header){0};
   set_gen1_control(&header, (3u << 3) | 2u);
   set_gen1_leaf_lane_end(&header, 2, 5, 0);  /* 9 */
   set_gen1_leaf_lane_end(&header, 3, 14, 3); /* 17 */
   set_gen1_leaf_lane_end(&header, 4, 2, 2);  /* 4 */
   set_gen1_child_space_slot(&header, UINT32_C(0xabcd1234));
   set_field(&header.words[0], 1, 4, 6);

   require(agx_rt_bvh_gen1_leaf_count(&header) == 17,
           "count mode takes the maximum leaf-lane end");
   require(agx_rt_bvh_gen1_leaf_slot_capacity(&header) == 6,
           "the Gen1 leaf-slot divisor decodes from w0[1:4]");
   require(agx_rt_bvh_gen1_child_space_slot(&header) == UINT32_C(0xabcd1234),
           "the Gen1 child-space slot uses w5-low:w6-low");
   require(agx_rt_bvh_gen1_leaf_storage_slots(&header) == 3,
           "the Gen1 leaf extent rounds up to three child-space slots");
   require(agx_rt_bvh_gen1_inner_child_offset(&header) ==
              UINT32_C(0xabcd1237),
           "the Gen1 inner-child offset follows rounded leaf storage");

   set_field(&header.words[0], 0, 1, 1);
   require(agx_rt_bvh_gen1_uses_indexed_leaf_counts(&header),
           "w0 bit zero selects indexed Gen1 leaf counts");
   require(agx_rt_bvh_gen1_leaf_count(&header) == 3,
           "indexed mode uses the leaf-box count");
   require(agx_rt_bvh_gen1_leaf_storage_slots(&header) == 1 &&
              agx_rt_bvh_gen1_inner_child_offset(&header) ==
                 UINT32_C(0xabcd1235),
           "indexed leaf storage advances the inner-child offset once");

   set_field(&header.words[0], 1, 4, 0);
   require(agx_rt_bvh_gen1_leaf_storage_slots(&header) == 0 &&
              agx_rt_bvh_gen1_inner_child_offset(&header) ==
                 UINT32_C(0xabcd1234),
           "a zero Gen1 divisor leaves the child-space base unchanged");

   set_gen1_control(&header, (6u << 3) | 7u);
   require(!agx_rt_bvh_gen1_branching_layout_valid(&header),
           "an overfull ordinary Gen1 branch layout is rejected");
}

static void
test_gen1_quantized_bounds(void)
{
   struct agx_rt_bvh_gen1_header header = {0};

   for (unsigned child = 0; child < 8; ++child) {
      uint32_t expected[6];
      for (unsigned component = 0; component < 6; ++component) {
         expected[component] = (child * 17u + component * 11u) & 0x7fu;
         const unsigned shift = 57u - component * 7u;
         set_field(&header.words[child], shift, 7, expected[component]);
      }

      struct agx_rt_bvh_gen1_quantized_bounds bounds;
      require(agx_rt_bvh_gen1_quantized_child_bounds(&header, child, &bounds),
              "each Gen1 child exposes quantized bounds");
      require(memcmp(bounds.component, expected, sizeof(expected)) == 0,
              "Gen1 quantized bounds follow Apple's descending bit order");
   }

   struct agx_rt_bvh_gen1_quantized_bounds bounds;
   require(!agx_rt_bvh_gen1_quantized_child_bounds(NULL, 0, &bounds),
           "a missing Gen1 header has no quantized bounds");
   require(!agx_rt_bvh_gen1_quantized_child_bounds(&header, 8, &bounds),
           "a ninth Gen1 child has no quantized bounds");
   require(!agx_rt_bvh_gen1_quantized_child_bounds(&header, 0, NULL),
           "a missing Gen1 bounds destination is refused");
}

static uint32_t
float_bits(float value)
{
   uint32_t bits;
   memcpy(&bits, &value, sizeof(bits));
   return bits;
}

static void
test_gen1_hw_triangle_leaf(void)
{
   /* Slot 14 from the pinned public-Metal M5 Pro version-3 capture.  The
    * end-to-end walker identifies this as geometry 0, primitive 7 and matches
    * all four SoA lanes against the probe's source vertices. */
   static const uint8_t captured[AGX_RT_BVH_GEN1_HW_TRIANGLE_LEAF_SIZE] = {
      0x50, 0x00, 0x00, 0x00, 0x9c, 0x54, 0xce, 0xbe,
      0x08, 0xec, 0xd2, 0xbd, 0x9c, 0x54, 0xce, 0xbe,
      0x9c, 0x54, 0xce, 0xbe, 0x05, 0xc0, 0x7d, 0xbf,
      0x05, 0xc0, 0x7d, 0xbf, 0x38, 0xf3, 0x30, 0xbf,
      0x38, 0xf3, 0x30, 0xbf, 0x33, 0x33, 0xb3, 0x3e,
      0x33, 0x33, 0xb3, 0x3e, 0x66, 0x66, 0xe6, 0x3e,
      0x66, 0x66, 0xe6, 0x3e, 0x00, 0x00, 0x00, 0x00,
      0x07, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
   };
   static const uint32_t expected_coordinate_bits[3][4] = {
      {0xbece549c, 0xbdd2ec08, 0xbece549c, 0xbece549c},
      {0xbf7dc005, 0xbf7dc005, 0xbf30f338, 0xbf30f338},
      {0x3eb33333, 0x3eb33333, 0x3ee66666, 0x3ee66666},
   };

   uint8_t unaligned[AGX_RT_BVH_GEN1_HW_TRIANGLE_LEAF_SIZE + 1];
   memcpy(unaligned + 1, captured, sizeof(captured));

   struct agx_rt_bvh_gen1_hw_triangle_leaf leaf;
   require(agx_rt_bvh_gen1_hw_triangle_leaf_unpack(
              unaligned + 1, sizeof(captured), &leaf),
           "an unaligned real Gen1 hardware triangle leaf decodes");
   for (unsigned axis = 0; axis < 3; ++axis) {
      for (unsigned lane = 0; lane < 4; ++lane) {
         require(float_bits(leaf.coordinate[axis][lane]) ==
                    expected_coordinate_bits[axis][lane],
                 "every captured triangle coordinate bit round-trips");
      }
   }
   require(leaf.geometry_word == 0 && leaf.primitive_word[0] == 7 &&
              leaf.primitive_word[1] == 7,
           "the captured triangle leaf preserves all three raw ID words");
   require(agx_rt_bvh_gen1_hw_triangle_geometry_id(&leaf) == 0 &&
              agx_rt_bvh_gen1_hw_triangle_primitive_id(&leaf, 0) == 7 &&
              agx_rt_bvh_gen1_hw_triangle_primitive_id(&leaf, 1) == 7,
           "the captured triangle leaf exposes its exact low-30-bit IDs");

   uint8_t flagged[sizeof(captured)];
   memcpy(flagged, captured, sizeof(flagged));
   flagged[55] = 0xc0;
   flagged[59] = 0x80;
   flagged[63] = 0x40;
   require(agx_rt_bvh_gen1_hw_triangle_leaf_unpack(flagged, sizeof(flagged),
                                                   &leaf),
           "unknown high ID bits do not invalidate a triangle leaf");
   require(leaf.geometry_word == UINT32_C(0xc0000000) &&
              leaf.primitive_word[0] == UINT32_C(0x80000007) &&
              leaf.primitive_word[1] == UINT32_C(0x40000007),
           "unknown high ID bits remain available to future decoders");
   require(agx_rt_bvh_gen1_hw_triangle_geometry_id(&leaf) == 0 &&
              agx_rt_bvh_gen1_hw_triangle_primitive_id(&leaf, 0) == 7 &&
              agx_rt_bvh_gen1_hw_triangle_primitive_id(&leaf, 1) == 7,
           "unknown high ID bits cannot contaminate decoded IDs");

   const struct agx_rt_bvh_gen1_hw_triangle_leaf zero = {0};
   for (size_t available = 0;
        available < AGX_RT_BVH_GEN1_HW_TRIANGLE_LEAF_SIZE; ++available) {
      memset(&leaf, 0xa5, sizeof(leaf));
      require(!agx_rt_bvh_gen1_hw_triangle_leaf_unpack(captured, available,
                                                       &leaf),
              "every truncated hardware triangle leaf is rejected");
      require(memcmp(&leaf, &zero, sizeof(leaf)) == 0,
              "a truncated triangle leaf leaves no stale output");
   }

   for (unsigned bit = 0; bit < 32; ++bit) {
      uint8_t malformed[sizeof(captured)];
      memcpy(malformed, captured, sizeof(malformed));
      malformed[bit / 8] ^= 1u << (bit % 8);
      memset(&leaf, 0xa5, sizeof(leaf));
      require(!agx_rt_bvh_gen1_hw_triangle_leaf_unpack(
                 malformed, sizeof(malformed), &leaf),
              "every changed triangle-leaf marker bit is rejected");
      require(memcmp(&leaf, &zero, sizeof(leaf)) == 0,
              "a wrong triangle marker leaves no stale output");
   }

   memset(&leaf, 0xa5, sizeof(leaf));
   require(!agx_rt_bvh_gen1_hw_triangle_leaf_unpack(NULL, sizeof(captured),
                                                    &leaf),
           "a missing triangle leaf is rejected");
   require(memcmp(&leaf, &zero, sizeof(leaf)) == 0,
           "a missing triangle leaf leaves no stale output");
   require(!agx_rt_bvh_gen1_hw_triangle_leaf_unpack(captured,
                                                    sizeof(captured), NULL),
           "a missing triangle-leaf destination is rejected");
   require(agx_rt_bvh_gen1_hw_triangle_geometry_id(NULL) == 0 &&
              agx_rt_bvh_gen1_hw_triangle_primitive_id(NULL, 0) == 0 &&
              agx_rt_bvh_gen1_hw_triangle_primitive_id(&leaf, 2) == 0,
           "missing and out-of-range triangle ID queries fail closed");
}

static void
test_gen1_randomized_assembly_equivalence(void)
{
   uint64_t random = UINT64_C(0xf24c8e91d37ab605);

   for (unsigned iteration = 0; iteration < 1000000; ++iteration) {
      struct agx_rt_bvh_gen1_header header;
      for (unsigned word = 0; word < 8; ++word)
         header.words[word] = next_random(&random);

      const uint32_t control = (uint32_t)header.words[7] & 0x3fu;
      const uint32_t expected_inner =
         reference_gen1_inner_child_count(control);
      const uint32_t expected_boxes = reference_gen1_leaf_box_count(control);
      const uint32_t expected_leaves = reference_gen1_leaf_count(&header);
      const uint32_t capacity = ((uint32_t)header.words[0] >> 1) & 15u;
      const uint32_t expected_storage =
         capacity ? ((expected_leaves + capacity - 1u) / capacity) &
                       0x03ffffffu
                  : 0;

      if (agx_rt_bvh_gen1_inner_child_count(&header) != expected_inner ||
          agx_rt_bvh_gen1_leaf_box_count(&header) != expected_boxes ||
          agx_rt_bvh_gen1_leaf_count(&header) != expected_leaves ||
          agx_rt_bvh_gen1_leaf_storage_slots(&header) != expected_storage ||
          agx_rt_bvh_gen1_inner_child_offset(&header) !=
             agx_rt_bvh_gen1_child_space_slot(&header) + expected_storage) {
         require(false,
                 "random Gen1 header diverged from the Apple assembly model");
      }
   }

   require(true,
           "one million Gen1 headers match the independent assembly model");
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
   require(sizeof(struct agx_rt_bvh_gen1_header) ==
              AGX_RT_BVH_GEN1_HEADER_SIZE,
           "a Gen1 node header is exactly 64 bytes");
   require(sizeof(struct agx_rt_bvh_gen5_header) ==
              AGX_RT_BVH_GEN5_HEADER_SIZE,
           "a Gen5 node header is exactly 64 bytes");
   test_gen1_unpack_and_control();
   test_gen1_leaf_layout();
   test_gen1_quantized_bounds();
   test_gen1_hw_triangle_leaf();
   test_gen1_randomized_assembly_equivalence();
   test_unpack_and_signed_child_space();
   test_leaf_and_inner_counts();
   test_qtb_and_vrb_fields();
   test_offsets_for_every_configuration();
   test_unknown_bits_do_not_affect_addressing();

   printf("agx_rt_bvh: PASS (%u checks)\n", checks);
   return 0;
}
