/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp validation bridge for the exact AGX3 Ioto state tail.
 */

#pragma once

#include "asahi/lib/agx_ioto_state_core.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hk_agx3_ioto_state_tail_encoding {
   uint8_t bytes[AGX_IOTO_STATE_TAIL_MAX_SIZE];
   uint8_t length;
};

/* These wrappers preserve out on failure and assign no meaning to f* fields. */
bool hk_agx3_ioto_state_tail_generate(
   enum agx_chip chip, const struct agx_ioto_state_tail *tail,
   struct hk_agx3_ioto_state_tail_encoding *out);

bool hk_agx3_ioto_state_tail_generate_shader(
   enum agx_chip chip, const struct agx_ioto_state_tail *tail,
   uint64_t shader_va, struct hk_agx3_ioto_state_tail_encoding *out);

#ifdef __cplusplus
}
#endif
