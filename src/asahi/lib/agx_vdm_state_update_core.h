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

#ifdef __cplusplus
}
#endif
