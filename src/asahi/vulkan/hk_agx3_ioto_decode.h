/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for strict Apple Ioto record decoding.
 */

#pragma once

#include "asahi/lib/agx_ioto_core.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decode a record only when the target grammar yields one unique form. */
bool hk_agx3_ioto_decode_record(enum agx_chip chip, const void *stream,
                                size_t available,
                                struct agx_ioto_record *record,
                                size_t *consumed);

/* Validate an expected form while preserving every opaque record byte. */
bool hk_agx3_ioto_validate_record(enum agx_chip chip,
                                  enum agx_ioto_kind expected,
                                  const void *stream, size_t available,
                                  struct agx_ioto_record *record,
                                  size_t *consumed);

#ifdef __cplusplus
}
#endif
