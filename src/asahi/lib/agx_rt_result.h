/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Shared Apple RIA3 traversal-result ABI recovered from the pinned runtime IR.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact IntersectRayResult byte offsets.  The two trailing transforms follow
 * the last i32 at the standard LLVM struct offsets (+60 and +108). */
#define AGX_RT_HIT_OFF_TYPE               0u
#define AGX_RT_HIT_OFF_T                  4u
#define AGX_RT_HIT_OFF_PRIMITIVE_ID       8u
#define AGX_RT_HIT_OFF_GEOMETRY_ID        12u
#define AGX_RT_HIT_OFF_PRIMITIVE_DATA     16u
#define AGX_RT_HIT_OFF_INSTANCE_ID        24u
#define AGX_RT_HIT_OFF_USER_INSTANCE_ID   28u
#define AGX_RT_HIT_OFF_INSTANCE_COUNT     32u
#define AGX_RT_HIT_OFF_BARYCENTRICS       40u
#define AGX_RT_HIT_OFF_FRONT_FACE         48u
#define AGX_RT_HIT_OFF_CURVE_PARAM        52u
#define AGX_RT_HIT_OFF_INTERSECTION_FN_ID 56u
#define AGX_RT_HIT_OFF_OBJECT_TO_WORLD    60u
#define AGX_RT_HIT_OFF_WORLD_TO_OBJECT    108u
#define AGX_RT_HIT_RESULT_SIZE            156u
#define AGX_RT_TRANSFORM_FLOATS            12u

/* Exact size of Apple's inline ray-query state object.  Its internal field
 * layout remains unexposed. */
#define AGX_RT_RAY_QUERY_STATE_SIZE 168u

/* Exact TLAS instance-leaf offsets from the RIA3 module's TBAA metadata. */
#define AGX_RT_INSTANCE_LEAF_OFF_HEADER           0u
#define AGX_RT_INSTANCE_LEAF_OFF_OBJECT_TO_WORLD 12u
#define AGX_RT_INSTANCE_LEAF_OFF_AABB             60u
#define AGX_RT_INSTANCE_LEAF_OFF_IDS              84u
#define AGX_RT_INSTANCE_LEAF_OFF_INSTANCE_FLAGS  108u
#define AGX_RT_INSTANCE_LEAF_SIZE                124u

struct agx_rt_hit_result {
   uint32_t intersection_type;
   float t;
   uint32_t primitive_id;
   uint32_t geometry_id;
   uint64_t primitive_data;
   uint32_t instance_id;
   uint32_t user_instance_id;
   uint8_t instance_count;
   float barycentrics[2];
   uint8_t front_face;
   float curve_param;
   uint32_t intersection_function_id;
   float object_to_world[AGX_RT_TRANSFORM_FLOATS];
   float world_to_object[AGX_RT_TRANSFORM_FLOATS];
};

static inline uint32_t
agx_rt_read_u32_le(const void *base, uint32_t offset)
{
   const uint8_t *p = (const uint8_t *)base + offset;
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
          ((uint32_t)p[3] << 24);
}

static inline float
agx_rt_read_f32_le(const void *base, uint32_t offset)
{
   const uint32_t bits = agx_rt_read_u32_le(base, offset);
   float value;
   memcpy(&value, &bits, sizeof(value));
   return value;
}

static inline uint64_t
agx_rt_read_u64_le(const void *base, uint32_t offset)
{
   return (uint64_t)agx_rt_read_u32_le(base, offset) |
          ((uint64_t)agx_rt_read_u32_le(base, offset + 4) << 32);
}

static inline void
agx_rt_read_transform(const void *base, uint32_t offset,
                      float out[AGX_RT_TRANSFORM_FLOATS])
{
   for (uint32_t i = 0; i < AGX_RT_TRANSFORM_FLOATS; ++i)
      out[i] = agx_rt_read_f32_le(base, offset + i * 4u);
}

/* Decode the exact public result prefix.  The two transforms are decoded only
 * when the complete 156-byte object is present and otherwise remain zero. */
static inline bool
agx_rt_hit_result_decode(const void *raw, size_t available,
                         struct agx_rt_hit_result *out)
{
   if (!raw || !out || available < AGX_RT_HIT_OFF_INTERSECTION_FN_ID + 4u)
      return false;

   memset(out, 0, sizeof(*out));
   out->intersection_type = agx_rt_read_u32_le(raw, AGX_RT_HIT_OFF_TYPE);
   out->t = agx_rt_read_f32_le(raw, AGX_RT_HIT_OFF_T);
   out->primitive_id = agx_rt_read_u32_le(raw, AGX_RT_HIT_OFF_PRIMITIVE_ID);
   out->geometry_id = agx_rt_read_u32_le(raw, AGX_RT_HIT_OFF_GEOMETRY_ID);
   out->primitive_data = agx_rt_read_u64_le(raw, AGX_RT_HIT_OFF_PRIMITIVE_DATA);
   out->instance_id = agx_rt_read_u32_le(raw, AGX_RT_HIT_OFF_INSTANCE_ID);
   out->user_instance_id =
      agx_rt_read_u32_le(raw, AGX_RT_HIT_OFF_USER_INSTANCE_ID);
   out->instance_count = ((const uint8_t *)raw)[AGX_RT_HIT_OFF_INSTANCE_COUNT];
   out->barycentrics[0] =
      agx_rt_read_f32_le(raw, AGX_RT_HIT_OFF_BARYCENTRICS);
   out->barycentrics[1] =
      agx_rt_read_f32_le(raw, AGX_RT_HIT_OFF_BARYCENTRICS + 4u);
   out->front_face = ((const uint8_t *)raw)[AGX_RT_HIT_OFF_FRONT_FACE];
   out->curve_param = agx_rt_read_f32_le(raw, AGX_RT_HIT_OFF_CURVE_PARAM);
   out->intersection_function_id =
      agx_rt_read_u32_le(raw, AGX_RT_HIT_OFF_INTERSECTION_FN_ID);

   if (available >= AGX_RT_HIT_RESULT_SIZE) {
      agx_rt_read_transform(raw, AGX_RT_HIT_OFF_OBJECT_TO_WORLD,
                            out->object_to_world);
      agx_rt_read_transform(raw, AGX_RT_HIT_OFF_WORLD_TO_OBJECT,
                            out->world_to_object);
   }

   return true;
}

static inline bool
agx_rt_instance_leaf_object_to_world(const void *raw, size_t available,
                                     float out[AGX_RT_TRANSFORM_FLOATS])
{
   if (!raw || !out || available < AGX_RT_INSTANCE_LEAF_OFF_AABB)
      return false;

   agx_rt_read_transform(raw, AGX_RT_INSTANCE_LEAF_OFF_OBJECT_TO_WORLD, out);
   return true;
}

#ifdef __cplusplus
}
#endif
