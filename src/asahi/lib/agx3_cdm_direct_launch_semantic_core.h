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

/* Finalize supplies only these raw, directly recovered template bit ranges.
 * The names deliberately do not assign meanings the pinned emitters do not
 * establish. */
#define AGX3_CDM_DIRECT_TEMPLATE_WORD0_MASK UINT32_C(0x00887ff0)
#define AGX3_CDM_DIRECT_TEMPLATE_WORD1_MASK UINT32_C(0x001ffff8)

enum agx3_cdm_direct_grid_mode {
   /* The emitter received threadgroup counts, expanded them to global thread
    * counts, and marked word 3 with bit 30. */
   AGX3_CDM_DIRECT_GRID_THREADGROUPS = 0,

   /* Direct thread dispatch uses a distinct 24-byte record.  This value is
    * recognized so callers fail closed instead of accidentally emitting the
    * 40-byte threadgroup form with bit 30 cleared. */
   AGX3_CDM_DIRECT_GRID_THREADS = 1,
};

struct agx3_cdm_direct_template_fields {
   uint16_t word0_bits_3_14;
   uint32_t word1_bits_3_20;
   bool compiler_reply_word0_bit23;
   bool subkernel_x_target_workitems;
};

/* Inputs to the exact ComputeProgramVariant::finalize transforms.  The two
 * byte-count meanings in AGC/Mesa shader state are not yet grounded, so their
 * names intentionally describe only the witnessed LoadShader producers. */
struct agx3_cdm_direct_template_sources {
   uint32_t load_shader_word0_source_bytes;
   uint32_t load_shader_word1_source_bytes;
   bool compiler_reply_word0_bit23;
   bool subkernel_x_target_workitems;
};

struct agx3_cdm_direct_template {
   uint32_t word0;
   uint32_t word1;
};

struct agx3_cdm_direct_launch_info {
   enum agx_chip chip;
   struct agx3_cdm_direct_template template_words;
   uint64_t pipeline;
   enum agx3_cdm_direct_grid_mode grid_mode;
   uint8_t dimensions;

   /* This separately controlled bit is sourced from an unresolved plugin
    * global only in the aggregate G15X emitter, whose concrete identities
    * here are G15S, G15C and G15D.  It is raw research input, not a device or
    * shader-info mapping. */
   bool g15x_word3_bit31;

   uint32_t global_size[3];
   uint32_t local_size[3];
};

bool agx3_cdm_direct_launch_target_grounded(enum agx_chip chip);

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
