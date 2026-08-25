/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asahi/lib/agx_ioto_profile_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hk_agx3_ioto_profile_encoding {
   uint8_t bytes[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
   uint8_t length;
};

bool hk_agx3_ioto_profile_generate(enum agx_chip chip, uint16_t payload0,
                                   uint16_t payload1,
                                   struct hk_agx3_ioto_profile_encoding *out);

bool hk_agx3_ioto_profile_decode(enum agx_chip chip, const void *command,
                                 size_t command_size, uint16_t *payload0,
                                 uint16_t *payload1);

#ifdef __cplusplus
}
#endif
