/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_scs_layout.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);          \
         ++failures;                                                           \
      }                                                                        \
   } while (0)

int
main(void)
{
   static const char *const section_names[] = {
      "Start",          "TokenBuffers", "DriverParameters",
      "RayCorePairs",   "RaySideband",  "RayStackData",
      "InstanceLeafPtrs", "RaySharedState", "CommittedIntersectionInfo",
      "CandidateIntersectionInfo", "DriverData", "UserData", "End",
   };
   static const char *const payload_names[] = {
      "RayCore",
      "TokenBuffer",
      "RaySideband",
      "RaySharedState",
      "CandidateIntersectionInfo",
      "CommittedIntersectionInfo",
      "Header",
      "CommittedCommonData",
      "CommittedTriangleData",
      "Time",
      "CommittedCurveData",
      "CommittedPerPrimitiveData",
      "IntersectionFunctionBufferData",
      "IntersectionFunctionBufferUserData",
      "IntersectionFunctionBufferCommittedFunctionId",
      "RayQueryMetadata",
      "RayQueryCandidates",
      "InstanceData",
      "PrimitiveMotionData",
      "TemporalSplitPrimitiveMotionData",
      "CommittedInstanceLeaf",
      "CommittedInstanceMotionData",
      "CandidateInstanceLeaf",
      "CandidateInstanceMotionData",
      "RayCoreStack",
      "ExtendedInstanceBitTrails",
      "ExtendedVisibilityMask",
   };

   CHECK(sizeof(section_names) / sizeof(section_names[0]) ==
         AGX_SCS_SECTION_COUNT);
   CHECK(sizeof(payload_names) / sizeof(payload_names[0]) ==
         AGX_SCS_PAYLOAD_COUNT);
   for (unsigned i = 0; i < AGX_SCS_SECTION_COUNT; ++i) {
      const char *name = agx_scs_section_name((enum agx_scs_section_id)i);
      CHECK(name != NULL);
      CHECK(name != NULL && strcmp(name, section_names[i]) == 0);
   }
   for (unsigned i = 0; i < AGX_SCS_PAYLOAD_COUNT; ++i) {
      const char *name = agx_scs_driver_payload_segment_name(
         (enum agx_scs_driver_payload_segment_id)i);
      CHECK(name != NULL);
      CHECK(name != NULL && strcmp(name, payload_names[i]) == 0);
   }
   CHECK(agx_scs_section_name((enum agx_scs_section_id)-1) == NULL);
   CHECK(agx_scs_section_name(AGX_SCS_SECTION_COUNT) == NULL);
   CHECK(agx_scs_driver_payload_segment_name(
            (enum agx_scs_driver_payload_segment_id)-1) == NULL);
   CHECK(agx_scs_driver_payload_segment_name(AGX_SCS_PAYLOAD_COUNT) == NULL);

   if (failures != 0) {
      fprintf(stderr, "agx_scs_layout: %u/%u checks failed\n", failures,
              checks);
      return 1;
   }

   printf("agx_scs_layout: all %u Apple section/payload ID checks passed\n",
          checks);
   return 0;
}
