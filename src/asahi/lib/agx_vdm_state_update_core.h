/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded, target-aware AGX3 VDM State Update representation.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_VDM_STATE_UPDATE_LENGTH 28u
#define AGX_VDM_STATE_UPDATE_HEADER UINT32_C(0x4000002e)

struct agx_vdm_state_update_info {
   uint32_t raw_vertex_shader_word0;
   uint64_t pipeline;
   uint32_t raw_pipeline_template;
   uint32_t raw_vertex_outputs[2];
   uint32_t raw_vertex_unknown;
};

struct agx_vdm_state_update {
   uint8_t bytes[AGX_VDM_STATE_UPDATE_LENGTH];
   uint8_t length;
};

/* The live renderer selects one of the first four 28-byte templates with two
 * boolean inputs.  Two more templates are built by VertexProgramVariant but
 * are not selected by the inspected encodeAndEmitRenderState path. */
enum agx_vdm_state_update_template_variant {
   AGX_VDM_STATE_UPDATE_TEMPLATE_00 = 0,
   AGX_VDM_STATE_UPDATE_TEMPLATE_01 = 1,
   AGX_VDM_STATE_UPDATE_TEMPLATE_10 = 2,
   AGX_VDM_STATE_UPDATE_TEMPLATE_11 = 3,
};

/*
 * Structural inputs used by Apple's VertexProgramVariant template builder.
 *
 * The two state-load counts and the VsStateConfig fields are deliberately
 * named after their proven producer/position.  Their higher-level compiler
 * meanings are not yet grounded, and they are not interchangeable with the
 * AGX2 Counts, VDM_STATE_VERTEX_OUTPUTS, or VDM_STATE_VERTEX_UNKNOWN words.
 */
struct agx_vdm_state_update_template_info {
   uint64_t pipeline;

   uint16_t raw_state_load_count_12;
   uint32_t raw_state_load_count_18;

   uint8_t raw_vs_output_count_0;
   uint8_t raw_vs_output_count_2;
   bool raw_vs_output_count_1_extra;
   uint8_t raw_vs_output_selector;
   bool raw_vs_output_flag_6;

   /* Pre-HAL300 (G15/G16/G17P) only.  These are respectively
    * `(flags & 0xc) != 0` and `(flags & 0x10) != 0` from VsStateConfig.
    * HAL300 does not consume them and rejects nonzero values rather than
    * silently discarding them. */
   bool pre_hal300_flag_group_nonzero;
   bool pre_hal300_flag_bit_4;

   enum agx_vdm_state_update_template_variant variant;

   /* One-based value loaded from the live device/context.  Values 1..8 are
    * the bounded domain for which the final word is exactly value - 1. */
   uint8_t active_selector_value;
};

/*
 * Pack/unpack the complete seven-word AGX3 VDM State Update emitted by the
 * inspected Apple VDMEncoderGen5 and VDMEncoderGen6 paths:
 *
 *   word 0: exact 0x4000002e header
 *   word 1: raw vertex-shader word 0
 *   words 2..3: widened 48-bit vertex-pipeline pair
 *   words 4..5: raw two-word vertex-output payload
 *   word 6: raw vertex-unknown payload
 *
 * Only the pipeline field's 64-byte alignment and 48-bit address range are
 * fully named.  The remaining payloads are preserved losslessly because their
 * complete AGX3 bit semantics are not independently grounded.
 *
 * The target allowlist contains only identities directly served by the
 * inspected Apple Metal plugins.  G16P and G17A remain closed despite sharing
 * neighboring command-stream generations.  This helper does not emit a live
 * command or enable lowering, scheduling, submission, or device admission.
 */
bool agx_vdm_state_update_supported(enum agx_chip chip);

bool agx_vdm_state_update_pack(enum agx_chip chip,
                               const struct agx_vdm_state_update_info *info,
                               struct agx_vdm_state_update *out);

bool agx_vdm_state_update_unpack(enum agx_chip chip, const void *bytes,
                                 size_t size,
                                 struct agx_vdm_state_update_info *info);

/*
 * Materialize/pack the exact first-four-template path observed in:
 *
 *   G15X_M1, G16X, G17P: VertexProgramVariant + VDMEncoderGen5
 *   G17G, G17X:          VertexProgramVariant + VDMEncoderGen6
 *
 * HAL300 intentionally differs: vertex word 0 has no pre-HAL300 bit-31 rule,
 * and the four per-template output constants are 0x20/0/0x21/1 instead of
 * 0x30/0/0x31/1.  Both calls are transactional and fail closed outside the
 * directly observed target/input domains.
 */
bool agx_vdm_state_update_template_materialize(
   enum agx_chip chip, const struct agx_vdm_state_update_template_info *info,
   struct agx_vdm_state_update_info *raw);

bool agx_vdm_state_update_template_pack(
   enum agx_chip chip, const struct agx_vdm_state_update_template_info *info,
   struct agx_vdm_state_update *out);

#ifdef __cplusplus
}
#endif
