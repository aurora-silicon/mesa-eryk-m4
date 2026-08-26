/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Exact semantic construction of the directly emitted AGX3 CDM launch form.
 *
 * This is a research/validation surface.  It does not allocate a pipeline,
 * construct USC state, emit a live command stream, submit work, or admit an
 * AGX3 device.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX3_CDM_DIRECT_LAUNCH_SIZE 40u

/* Finalize supplies only these directly recovered template bit ranges. */
#define AGX3_CDM_DIRECT_TEMPLATE_WORD0_MASK UINT32_C(0x00887ff0)
#define AGX3_CDM_DIRECT_TEMPLATE_WORD1_MASK UINT32_C(0x001ffff8)

/* The two direct API paths have different grid sources but serialize the same
 * canonical 40-byte record containing global thread counts.  The source is
 * intentionally absent from the decoded record because it is not encoded. */
enum agx3_cdm_direct_grid_source {
   AGX3_CDM_DIRECT_GRID_THREADGROUP_COUNTS = 0,
   AGX3_CDM_DIRECT_GRID_GLOBAL_THREADS = 1,
};

struct agx3_cdm_direct_template_fields {
   uint16_t word0_bits_3_14;
   uint32_t word1_bits_3_20;
   bool ray_traversal_depth_nonzero;
   bool subkernel_x_target_workitems;
};

/* Inputs to the exact ComputeProgramVariant::finalize transforms.  These are
 * the names and scalar types from AGCCodeTranslator.ShaderInfo in Apple's
 * embedded BFBS schema. */
struct agx3_cdm_direct_template_sources {
   uint32_t max_total_threads_per_threadgroup;

   /* HAL300's ProgramVariantESLState constructor reads only the low 32 bits
    * of this ULong before finalize constructs launch word 0. */
   uint64_t extended_texture_write_mask_qword_1;

   /* Finalize booleanizes this ULong into launch word 0 bit 23. */
   uint64_t ray_traversal_depth;
};

struct agx3_cdm_direct_template {
   uint32_t word0;
   uint32_t word1;
};

struct agx3_cdm_direct_launch_info {
   enum agx_chip chip;
   struct agx3_cdm_direct_template template_words;
   uint64_t pipeline;
   uint8_t dimensions;

   /* DeviceConstants::hasMultipleAGCs supplies word 3 bit 31 only in the
    * aggregate G15X emitter (G15S, G15C and G15D here). */
   bool g15x_has_multiple_agcs;

   uint32_t global_size[3];
   uint32_t local_size[3];
};

bool agx3_cdm_direct_launch_target_grounded(enum agx_chip chip);

/* Resolve either direct API's grid source into the serialized global thread
 * counts.  `global_size` is cleared on every failure. */
bool agx3_cdm_direct_grid_resolve(
   enum agx3_cdm_direct_grid_source source, const uint32_t input_size[3],
   const uint32_t local_size[3], uint32_t global_size[3]);

bool agx3_cdm_direct_template_build(
   const struct agx3_cdm_direct_template_fields *fields,
   struct agx3_cdm_direct_template *result);

bool agx3_cdm_direct_template_build_from_sources(
   const struct agx3_cdm_direct_template_sources *sources,
   struct agx3_cdm_direct_template *result);

bool agx3_cdm_direct_template_decode(
   const struct agx3_cdm_direct_template *template_words,
   struct agx3_cdm_direct_template_fields *fields);

/* Pack one canonical 40-byte record.  `packed` is cleared on every failure. */
bool agx3_cdm_direct_launch_pack(
   const struct agx3_cdm_direct_launch_info *info,
   uint8_t packed[AGX3_CDM_DIRECT_LAUNCH_SIZE]);

/* Decode the first canonical record in a stream.  `available` may include
 * following records.  `info` is cleared on every failure. */
bool agx3_cdm_direct_launch_unpack(
   enum agx_chip chip, const void *packed, size_t available,
   struct agx3_cdm_direct_launch_info *info);

#ifdef __cplusplus
}
#endif
