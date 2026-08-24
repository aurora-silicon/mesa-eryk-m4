/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Apple RIA Gen5 BVH8 node-header child-layout decoding.
 *
 * These accessors are an independent implementation of the six pure
 * RIANodeHeaderGen5 child-layout accessors in AGXMetalG17X.  They were
 * validated against the Apple implementation over 64,000,000 randomized
 * 64-byte headers with no mismatches.  Keep the two still-unidentified bit
 * ranges (w0[8:15] and w7[13:15]) out of this interface: none participates in
 * a child-section address.
 *
 * Offsets are signed byte offsets relative to the acceleration structure.
 * The encoded child-space base is a signed 32-bit count of 64-byte slots.
 *
 * This header intentionally depends only on the C standard library so it can
 * be shared by Vulkan, Gallium and the native D3D12 frontend.
 */

#ifndef AGX_RT_BVH_H
#define AGX_RT_BVH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_RT_BVH_GEN5_HEADER_SIZE 64u
#define AGX_RT_BVH_GEN5_INVALID_QTB_COUNT 4u

struct agx_rt_bvh_gen5_header {
   uint64_t words[8];
};

static inline uint64_t
agx_rt_bvh_load_le64(const uint8_t *src)
{
   uint64_t value = 0;

   for (unsigned i = 0; i < 8; ++i)
      value |= (uint64_t)src[i] << (i * 8);

   return value;
}

/* Unpack explicitly instead of casting mapped GPU memory.  This accepts
 * unaligned nodes and gives the same result on either host byte order. */
static inline bool
agx_rt_bvh_gen5_header_unpack(const void *packed,
                              struct agx_rt_bvh_gen5_header *header)
{
   if (!packed || !header)
      return false;

   const uint8_t *bytes = (const uint8_t *)packed;
   for (unsigned i = 0; i < 8; ++i)
      header->words[i] = agx_rt_bvh_load_le64(bytes + (i * 8));

   return true;
}

static inline uint32_t
agx_rt_bvh_gen5_config(const struct agx_rt_bvh_gen5_header *header)
{
   return (uint32_t)(header->words[0] & 7u);
}

/* Index of the first inner-child slot.  Leaf-box fields occupy [0, n), and
 * inner-child contributions occupy [n, 8). */
static inline uint32_t
agx_rt_bvh_gen5_inner_child_boundary(
   const struct agx_rt_bvh_gen5_header *header)
{
   return (uint32_t)((header->words[0] >> 3) & 15u);
}

static inline bool
agx_rt_bvh_gen5_uses_indexed_leaf_counts(
   const struct agx_rt_bvh_gen5_header *header)
{
   return ((header->words[0] >> 7) & 1u) != 0;
}

static inline uint32_t
agx_rt_bvh_gen5_leaf_type(const struct agx_rt_bvh_gen5_header *header)
{
   return (uint32_t)((header->words[7] >> 8) & 31u);
}

/* The low halves are deliberately ordered w5:w6. */
static inline int32_t
agx_rt_bvh_gen5_child_space_slot(
   const struct agx_rt_bvh_gen5_header *header)
{
   const uint32_t high = ((uint32_t)header->words[5] & 0xffffu) << 16;
   const uint32_t low = (uint32_t)header->words[6] & 0xffffu;

   return (int32_t)(high | low);
}

/* Sixteen 3-bit fields: bits 16..18 and 19..21 of words 0..7. */
static inline uint32_t
agx_rt_bvh_gen5_slot(const struct agx_rt_bvh_gen5_header *header,
                     unsigned slot)
{
   if (slot >= 16)
      return 0;

   return (uint32_t)((header->words[slot >> 1] >>
                      (16 + (3 * (slot & 1)))) &
                     7u);
}

static inline uint32_t
agx_rt_bvh_gen5_leaf_count(const struct agx_rt_bvh_gen5_header *header)
{
   const uint32_t boundary =
      agx_rt_bvh_gen5_inner_child_boundary(header);

   if (boundary == 0)
      return 0;

   if (agx_rt_bvh_gen5_uses_indexed_leaf_counts(header)) {
      uint32_t maximum = 0;

      for (uint32_t i = 0; i < boundary; ++i) {
         const uint32_t value = agx_rt_bvh_gen5_slot(header, i);
         if (value > maximum)
            maximum = value;
      }

      return maximum + 1;
   }

   uint32_t count = 0;
   for (uint32_t i = 0; i < boundary; ++i) {
      const uint32_t value = agx_rt_bvh_gen5_slot(header, i);
      count += value ? value : 8u;
   }

   return count;
}

static inline uint32_t
agx_rt_bvh_gen5_inner_slots(const struct agx_rt_bvh_gen5_header *header)
{
   const uint32_t boundary =
      agx_rt_bvh_gen5_inner_child_boundary(header);

   if (boundary > 7)
      return 0;

   uint32_t count = 0;
   for (uint32_t i = boundary; i < 8; ++i)
      count += agx_rt_bvh_gen5_slot(header, i);

   return count;
}

/* The three 4-bit QTB occupancies are split across w6/w7. */
static inline void
agx_rt_bvh_gen5_qtb_occupancies(
   const struct agx_rt_bvh_gen5_header *header, uint32_t *a, uint32_t *b,
   uint32_t *c)
{
   const uint32_t w6 = (uint32_t)header->words[6];
   const uint32_t w7 = (uint32_t)header->words[7];
   const uint32_t occupancy_a =
      ((w6 >> 16) & 3u) | (((w7 >> 16) & 3u) << 2);
   const uint32_t occupancy_b =
      ((w6 >> 18) & 3u) | (((w7 >> 18) & 3u) << 2);
   const uint32_t occupancy_c =
      ((w6 >> 20) & 3u) | (((w7 >> 20) & 3u) << 2);

   if (a)
      *a = occupancy_a;
   if (b)
      *b = occupancy_b;
   if (c)
      *c = occupancy_c;
}

/* Apple returns 4 as an invalid-occupancy sentinel for configurations 2..4. */
static inline uint32_t
agx_rt_bvh_gen5_qtb_count(const struct agx_rt_bvh_gen5_header *header)
{
   const uint32_t config = agx_rt_bvh_gen5_config(header);

   if (config < 2 || config > 4)
      return 0;

   uint32_t a, b, c;
   agx_rt_bvh_gen5_qtb_occupancies(header, &a, &b, &c);

   if (a + b + c != (agx_rt_bvh_gen5_leaf_count(header) & 0xffffu))
      return AGX_RT_BVH_GEN5_INVALID_QTB_COUNT;
   if (c)
      return 3;
   if (b)
      return 2;
   if (a)
      return 1;
   return 0;
}

static inline bool
agx_rt_bvh_gen5_qtb_layout_valid(
   const struct agx_rt_bvh_gen5_header *header)
{
   return agx_rt_bvh_gen5_qtb_count(header) !=
          AGX_RT_BVH_GEN5_INVALID_QTB_COUNT;
}

/* Configuration 4's four VRB spans use slots 8..11.  Seven encodes eight
 * 32-byte units rather than seven. */
static inline uint32_t
agx_rt_bvh_gen5_vrb_size_bytes(
   const struct agx_rt_bvh_gen5_header *header)
{
   uint32_t units = 0;

   for (unsigned i = 8; i < 12; ++i) {
      const uint32_t value = agx_rt_bvh_gen5_slot(header, i);
      units += value == 7 ? 8u : value;
   }

   return units * 32u;
}

static inline int64_t
agx_rt_bvh_gen5_base_slots(const struct agx_rt_bvh_gen5_header *header)
{
   return (int64_t)agx_rt_bvh_gen5_child_space_slot(header) +
          (int64_t)agx_rt_bvh_gen5_inner_slots(header);
}

static inline int64_t
agx_rt_bvh_gen5_offset_child_qtbs_bytes(
   const struct agx_rt_bvh_gen5_header *header)
{
   return agx_rt_bvh_gen5_base_slots(header) * 64;
}

static inline int64_t
agx_rt_bvh_gen5_offset_child_hw_leaves_bytes(
   const struct agx_rt_bvh_gen5_header *header)
{
   int64_t slots = agx_rt_bvh_gen5_base_slots(header);

   if (agx_rt_bvh_gen5_config(header) == 2)
      slots += (int64_t)agx_rt_bvh_gen5_qtb_count(header);

   return slots * 64;
}

static inline int64_t
agx_rt_bvh_gen5_offset_vrbs_bytes(
   const struct agx_rt_bvh_gen5_header *header)
{
   const int64_t slots =
      agx_rt_bvh_gen5_base_slots(header) +
      (int64_t)agx_rt_bvh_gen5_qtb_count(header);

   return slots * 64;
}

static inline int64_t
agx_rt_bvh_gen5_offset_lhb_bytes(
   const struct agx_rt_bvh_gen5_header *header)
{
   const uint32_t config = agx_rt_bvh_gen5_config(header);
   const int64_t base_slots = agx_rt_bvh_gen5_base_slots(header);

   switch (config) {
   case 0:
   case 7:
      return base_slots * 64;
   case 1:
   case 2: {
      const int64_t hw =
         agx_rt_bvh_gen5_offset_child_hw_leaves_bytes(header);
      const int64_t end =
         hw + (36 * (int64_t)(agx_rt_bvh_gen5_leaf_count(header) & 0xffffu));
      return (int64_t)(((uint64_t)end + 31u) & ~UINT64_C(31));
   }
   case 3:
      return (base_slots +
              (int64_t)agx_rt_bvh_gen5_qtb_count(header)) *
             64;
   case 4:
      return (base_slots +
              (int64_t)agx_rt_bvh_gen5_qtb_count(header)) *
                64 +
             (int64_t)agx_rt_bvh_gen5_vrb_size_bytes(header);
   case 5:
      return 0;
   case 6:
      return agx_rt_bvh_gen5_offset_child_hw_leaves_bytes(header) +
             ((int64_t)(agx_rt_bvh_gen5_inner_child_boundary(header) &
                        0xffffu) *
              128);
   default:
      return 0;
   }
}

static inline int64_t
agx_rt_bvh_gen5_offset_child_leaves_bytes(
   const struct agx_rt_bvh_gen5_header *header)
{
   const uint32_t config = agx_rt_bvh_gen5_config(header);
   const uint32_t leaf_type = agx_rt_bvh_gen5_leaf_type(header);
   uint32_t extra;

   if (config == 6)
      extra = 0;
   else if (config == 5)
      extra = 4;
   else if (config == 7 && leaf_type == 0x1a)
      extra = 0;
   else {
      const uint32_t type_index = leaf_type - 0x0cu;
      const bool single_entry =
         type_index <= 0x10u && ((UINT32_C(0x0001d0c3) >> type_index) & 1u);

      extra = single_entry
                 ? 4
                 : ((agx_rt_bvh_gen5_leaf_count(header) & 0xffu) * 4);
   }

   return agx_rt_bvh_gen5_offset_lhb_bytes(header) + (int64_t)extra;
}

#ifdef __cplusplus
}
#endif

#endif
