/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_scs_layout.h"

#include <stddef.h>

_Static_assert(AGX_SCS_SECTION_START == 0 &&
                  AGX_SCS_SECTION_TOKEN_BUFFERS == 1 &&
                  AGX_SCS_SECTION_DRIVER_PARAMETERS == 2 &&
                  AGX_SCS_SECTION_RAY_CORE_PAIRS == 3 &&
                  AGX_SCS_SECTION_RAY_SIDEBAND == 4 &&
                  AGX_SCS_SECTION_RAY_STACK_DATA == 5 &&
                  AGX_SCS_SECTION_INSTANCE_LEAF_PTRS == 6 &&
                  AGX_SCS_SECTION_RAY_SHARED_STATE == 7 &&
                  AGX_SCS_SECTION_COMMITTED_INTERSECTION_INFO == 8 &&
                  AGX_SCS_SECTION_CANDIDATE_INTERSECTION_INFO == 9 &&
                  AGX_SCS_SECTION_DRIVER_DATA == 10 &&
                  AGX_SCS_SECTION_USER_DATA == 11 &&
                  AGX_SCS_SECTION_END == 12 && AGX_SCS_SECTION_COUNT == 13,
               "Apple SCS section IDs changed");

_Static_assert(
   AGX_SCS_PAYLOAD_RAY_CORE == 0 && AGX_SCS_PAYLOAD_TOKEN_BUFFER == 1 &&
      AGX_SCS_PAYLOAD_RAY_SIDEBAND == 2 &&
      AGX_SCS_PAYLOAD_RAY_SHARED_STATE == 3 &&
      AGX_SCS_PAYLOAD_CANDIDATE_INTERSECTION_INFO == 4 &&
      AGX_SCS_PAYLOAD_COMMITTED_INTERSECTION_INFO == 5 &&
      AGX_SCS_PAYLOAD_HEADER == 6 &&
      AGX_SCS_PAYLOAD_COMMITTED_COMMON_DATA == 7 &&
      AGX_SCS_PAYLOAD_COMMITTED_TRIANGLE_DATA == 8 &&
      AGX_SCS_PAYLOAD_TIME == 9 &&
      AGX_SCS_PAYLOAD_COMMITTED_CURVE_DATA == 10 &&
      AGX_SCS_PAYLOAD_COMMITTED_PER_PRIMITIVE_DATA == 11 &&
      AGX_SCS_PAYLOAD_INTERSECTION_FUNCTION_BUFFER_DATA == 12 &&
      AGX_SCS_PAYLOAD_INTERSECTION_FUNCTION_BUFFER_USER_DATA == 13 &&
      AGX_SCS_PAYLOAD_INTERSECTION_FUNCTION_BUFFER_COMMITTED_FUNCTION_ID ==
         14 &&
      AGX_SCS_PAYLOAD_RAY_QUERY_METADATA == 15 &&
      AGX_SCS_PAYLOAD_RAY_QUERY_CANDIDATES == 16 &&
      AGX_SCS_PAYLOAD_INSTANCE_DATA == 17 &&
      AGX_SCS_PAYLOAD_PRIMITIVE_MOTION_DATA == 18 &&
      AGX_SCS_PAYLOAD_TEMPORAL_SPLIT_PRIMITIVE_MOTION_DATA == 19 &&
      AGX_SCS_PAYLOAD_COMMITTED_INSTANCE_LEAF == 20 &&
      AGX_SCS_PAYLOAD_COMMITTED_INSTANCE_MOTION_DATA == 21 &&
      AGX_SCS_PAYLOAD_CANDIDATE_INSTANCE_LEAF == 22 &&
      AGX_SCS_PAYLOAD_CANDIDATE_INSTANCE_MOTION_DATA == 23 &&
      AGX_SCS_PAYLOAD_RAY_CORE_STACK == 24 &&
      AGX_SCS_PAYLOAD_EXTENDED_INSTANCE_BIT_TRAILS == 25 &&
      AGX_SCS_PAYLOAD_EXTENDED_VISIBILITY_MASK == 26 &&
      AGX_SCS_PAYLOAD_COUNT == 27,
   "Apple SCS driver-payload segment IDs changed");

static const char *const section_names[AGX_SCS_SECTION_COUNT] = {
   [AGX_SCS_SECTION_START] = "Start",
   [AGX_SCS_SECTION_TOKEN_BUFFERS] = "TokenBuffers",
   [AGX_SCS_SECTION_DRIVER_PARAMETERS] = "DriverParameters",
   [AGX_SCS_SECTION_RAY_CORE_PAIRS] = "RayCorePairs",
   [AGX_SCS_SECTION_RAY_SIDEBAND] = "RaySideband",
   [AGX_SCS_SECTION_RAY_STACK_DATA] = "RayStackData",
   [AGX_SCS_SECTION_INSTANCE_LEAF_PTRS] = "InstanceLeafPtrs",
   [AGX_SCS_SECTION_RAY_SHARED_STATE] = "RaySharedState",
   [AGX_SCS_SECTION_COMMITTED_INTERSECTION_INFO] =
      "CommittedIntersectionInfo",
   [AGX_SCS_SECTION_CANDIDATE_INTERSECTION_INFO] =
      "CandidateIntersectionInfo",
   [AGX_SCS_SECTION_DRIVER_DATA] = "DriverData",
   [AGX_SCS_SECTION_USER_DATA] = "UserData",
   [AGX_SCS_SECTION_END] = "End",
};

static const char *const payload_names[AGX_SCS_PAYLOAD_COUNT] = {
   [AGX_SCS_PAYLOAD_RAY_CORE] = "RayCore",
   [AGX_SCS_PAYLOAD_TOKEN_BUFFER] = "TokenBuffer",
   [AGX_SCS_PAYLOAD_RAY_SIDEBAND] = "RaySideband",
   [AGX_SCS_PAYLOAD_RAY_SHARED_STATE] = "RaySharedState",
   [AGX_SCS_PAYLOAD_CANDIDATE_INTERSECTION_INFO] =
      "CandidateIntersectionInfo",
   [AGX_SCS_PAYLOAD_COMMITTED_INTERSECTION_INFO] =
      "CommittedIntersectionInfo",
   [AGX_SCS_PAYLOAD_HEADER] = "Header",
   [AGX_SCS_PAYLOAD_COMMITTED_COMMON_DATA] = "CommittedCommonData",
   [AGX_SCS_PAYLOAD_COMMITTED_TRIANGLE_DATA] = "CommittedTriangleData",
   [AGX_SCS_PAYLOAD_TIME] = "Time",
   [AGX_SCS_PAYLOAD_COMMITTED_CURVE_DATA] = "CommittedCurveData",
   [AGX_SCS_PAYLOAD_COMMITTED_PER_PRIMITIVE_DATA] =
      "CommittedPerPrimitiveData",
   [AGX_SCS_PAYLOAD_INTERSECTION_FUNCTION_BUFFER_DATA] =
      "IntersectionFunctionBufferData",
   [AGX_SCS_PAYLOAD_INTERSECTION_FUNCTION_BUFFER_USER_DATA] =
      "IntersectionFunctionBufferUserData",
   [AGX_SCS_PAYLOAD_INTERSECTION_FUNCTION_BUFFER_COMMITTED_FUNCTION_ID] =
      "IntersectionFunctionBufferCommittedFunctionId",
   [AGX_SCS_PAYLOAD_RAY_QUERY_METADATA] = "RayQueryMetadata",
   [AGX_SCS_PAYLOAD_RAY_QUERY_CANDIDATES] = "RayQueryCandidates",
   [AGX_SCS_PAYLOAD_INSTANCE_DATA] = "InstanceData",
   [AGX_SCS_PAYLOAD_PRIMITIVE_MOTION_DATA] = "PrimitiveMotionData",
   [AGX_SCS_PAYLOAD_TEMPORAL_SPLIT_PRIMITIVE_MOTION_DATA] =
      "TemporalSplitPrimitiveMotionData",
   [AGX_SCS_PAYLOAD_COMMITTED_INSTANCE_LEAF] = "CommittedInstanceLeaf",
   [AGX_SCS_PAYLOAD_COMMITTED_INSTANCE_MOTION_DATA] =
      "CommittedInstanceMotionData",
   [AGX_SCS_PAYLOAD_CANDIDATE_INSTANCE_LEAF] = "CandidateInstanceLeaf",
   [AGX_SCS_PAYLOAD_CANDIDATE_INSTANCE_MOTION_DATA] =
      "CandidateInstanceMotionData",
   [AGX_SCS_PAYLOAD_RAY_CORE_STACK] = "RayCoreStack",
   [AGX_SCS_PAYLOAD_EXTENDED_INSTANCE_BIT_TRAILS] =
      "ExtendedInstanceBitTrails",
   [AGX_SCS_PAYLOAD_EXTENDED_VISIBILITY_MASK] = "ExtendedVisibilityMask",
};

_Static_assert(sizeof(section_names) / sizeof(section_names[0]) ==
                  AGX_SCS_SECTION_COUNT,
               "SCS section name table size");
_Static_assert(sizeof(payload_names) / sizeof(payload_names[0]) ==
                  AGX_SCS_PAYLOAD_COUNT,
               "SCS payload name table size");

const char *
agx_scs_section_name(enum agx_scs_section_id id)
{
   return (unsigned)id < AGX_SCS_SECTION_COUNT ? section_names[id] : NULL;
}

const char *
agx_scs_driver_payload_segment_name(
   enum agx_scs_driver_payload_segment_id id)
{
   return (unsigned)id < AGX_SCS_PAYLOAD_COUNT ? payload_names[id] : NULL;
}
