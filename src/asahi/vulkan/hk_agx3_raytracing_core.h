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

#ifdef __cplusplus
extern "C" {
#endif

struct hk_agx3_continue_ray_instruction {
   uint8_t bytes[AGX3_CONTINUE_RAY_MAX_LENGTH];
   uint8_t length;
};

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

#ifdef __cplusplus
}
#endif
