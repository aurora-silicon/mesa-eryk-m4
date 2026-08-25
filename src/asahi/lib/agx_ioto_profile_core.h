/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "agx_chip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_IOTO_PROFILE_CONTROL_MAX_SIZE 6

/*
 * Byte-witnessed fields of Ioto SETPROFILECTL_0.  Their meaning is not yet
 * proved, so callers must preserve them as opaque halfwords.
 */
struct agx_ioto_profile_control {
   enum agx_chip chip;
   uint16_t payload0;
   uint16_t payload1;
};

bool agx_ioto_profile_control_supported(enum agx_chip chip);

size_t agx_ioto_profile_control_size(enum agx_chip chip);

bool
agx_ioto_profile_control_pack(const struct agx_ioto_profile_control *profile,
                              uint8_t out[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE]);

/* Decode one record from a possibly longer stream and return its byte count. */
bool agx_ioto_profile_control_unpack(enum agx_chip chip, const void *stream,
                                     size_t available,
                                     struct agx_ioto_profile_control *profile,
                                     size_t *consumed);

#ifdef __cplusplus
}
#endif
