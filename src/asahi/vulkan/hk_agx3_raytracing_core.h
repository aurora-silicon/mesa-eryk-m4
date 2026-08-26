/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for the bounded AGX3 continue_ray instruction codec.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asahi/compiler/agx3_pack.h"
#include "asahi/lib/agx_rt_result.h"

#ifdef __cplusplus
extern "C" {
#endif

struct hk_agx3_continue_ray_instruction {
   uint8_t bytes[AGX3_CONTINUE_RAY_MAX_LENGTH];
   uint8_t length;
};

struct hk_agx3_intersect_ray_instruction {
   uint8_t bytes[AGX3_INTERSECT_RAY_MAX_LENGTH];
   uint8_t length;
};

struct hk_agx3_inc_stk_fixed_witness {
   uint8_t bytes[AGX3_INC_STK_FIXED_WITNESS_LENGTH];
   uint8_t length;
};

struct hk_agx3_continue_ray_fixed_first_site_witness {
   uint8_t bytes[AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH];
   uint8_t length;
};

struct hk_agx3_update_node_expansion {
   uint8_t bytes[AGX3_UPDATE_NODE_MAX_LENGTH];
   uint8_t length;
};

struct hk_agx3_update_bvh_fixed_witness {
   uint8_t bytes[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH];
   uint8_t length;
};

/*
 * Encode/decode the exact M3/M4/A18 Pro/M5 native differential pairs for one
 * hardware `intersect_ray` instruction.  `state_register` is Apple's varying
 * f5 half-register field; every other field remains corpus-canonical, and
 * identities absent from the cross-target corpus stay rejected.
 *
 * These helpers validate bytes only.  They do not lower a SPIR-V/NIR ray
 * operation, establish the SCS layout or required barrier, append code,
 * submit work, admit a device, or advertise Vulkan ray tracing/ray query.
 */
bool hk_agx3_intersect_ray_instruction_generate(
   enum agx_chip chip, uint16_t state_register,
   struct hk_agx3_intersect_ray_instruction *out);

bool hk_agx3_intersect_ray_instruction_decode(
   enum agx_chip chip, const void *instruction, size_t instruction_size,
   uint16_t *state_register);

/*
 * Encode/decode exactly one corpus-witnessed `continue_ray` instruction for a
 * pinned chip identity.  `state_register` names the varying Apple-table f5
 * half-register field; it is not a Vulkan shader operand.
 *
 * These helpers do not lower a SPIR-V/NIR ray operation, construct SCS state,
 * append code to a shader, submit work, admit an AGX3 device, or advertise a
 * Vulkan ray-tracing or ray-query extension.
 */
bool hk_agx3_continue_ray_instruction_generate(
   enum agx_chip chip, uint16_t state_register,
   struct hk_agx3_continue_ray_instruction *out);

bool hk_agx3_continue_ray_instruction_decode(
   enum agx_chip chip, const void *instruction, size_t instruction_size,
   uint16_t *state_register);

/* Preserve the exact first continue_ray site from the native lowering.  This
 * is intentionally distinct from the operand-bearing codec above and is not
 * sufficient to lower or submit a ray query. */
bool hk_agx3_continue_ray_fixed_first_site_witness_generate(
   enum agx_chip chip,
   struct hk_agx3_continue_ray_fixed_first_site_witness *out);

bool hk_agx3_continue_ray_fixed_first_site_witness_valid(
   enum agx_chip chip, const void *instruction, size_t instruction_size);

/*
 * Generate/validate only the exact per-encoding `incStk` fixed witness on
 * scoped chip identities whose native rt_rayquery_committed corpus contains
 * those bytes.  Apple's table decodes f24=4, but no differential proves an
 * operand codec, so the field is deliberately absent from this API.
 *
 * This bridge does not lower SPIR-V/NIR, establish SCS state or barriers,
 * append code, submit work, admit a device, or advertise Vulkan ray tracing
 * or ray-query support.
 */
bool hk_agx3_inc_stk_fixed_witness_generate(
   enum agx_chip chip, struct hk_agx3_inc_stk_fixed_witness *out);

bool hk_agx3_inc_stk_fixed_witness_valid(enum agx_chip chip,
                                         const void *instruction,
                                         size_t instruction_size);

/* Exact barrier-plus-operation expansions recovered from Apple's RT compiler.
 * update_node is admitted only on the scoped cross-target RT corpus;
 * update_bvh remains G17S-only because that is the sole pinned witness. */
bool hk_agx3_update_node_expansion_generate(
   enum agx_chip chip, struct hk_agx3_update_node_expansion *out);

bool hk_agx3_update_node_expansion_valid(enum agx_chip chip,
                                         const void *instructions,
                                         size_t instruction_size);

bool hk_agx3_update_bvh_fixed_witness_generate(
   enum agx_chip chip, struct hk_agx3_update_bvh_fixed_witness *out);

bool hk_agx3_update_bvh_fixed_witness_valid(enum agx_chip chip,
                                            const void *instructions,
                                            size_t instruction_size);

/* Decode Apple's exact shared traversal-result and TLAS instance-leaf ABI.
 * This maps the result side into Honeykrisp without lowering or advertising a
 * Vulkan ray-tracing capability. */
bool hk_agx3_rt_hit_result_decode(const void *raw, size_t available,
                                  struct agx_rt_hit_result *out);

bool hk_agx3_rt_instance_leaf_object_to_world(
   const void *raw, size_t available, float out[AGX_RT_TRANSFORM_FLOATS]);

#ifdef __cplusplus
}
#endif
