/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * AGX3 direct-absolute scalar SCS load/store encoders and decoders.
 *
 * This intentionally models only the address-space(5) form recovered with
 * controlled Apple G17P/G17S compiler probes: immediate index zero, immediate
 * base zero, scale zero, one enabled component, and a signed 16-bit byte
 * displacement.  Register-indexed and uniform-base traversal forms are not
 * represented and therefore fail closed.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx3_mem_core.h"
#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX3_SCS_MAX_LENGTH 16u

struct agx3_scs_desc {
   uint16_t data; /* load destination / store value, half-register units */
   enum agx3_mem_width width;
   int16_t byte_offset;

   uint8_t slot;       /* load only: 1..6 (b), 1..8 (c) */
   uint16_t wait_mask; /* bit i waits for scoreboard slot i + 1 */
   bool value_kill;    /* store only; witnessed only on AGX3c */

   /* AGX3c ldscs f76=2.  Its placement is exact but policy is not; normal
    * lowering leaves this clear while decode/replay preserves Apple vectors.
    */
   bool c_load_marker;
};

bool agx3_scs_encode_load(enum agx_isa_encoding enc,
                          const struct agx3_scs_desc *desc,
                          uint8_t out[AGX3_SCS_MAX_LENGTH], uint8_t *length);
bool agx3_scs_encode_store(enum agx_isa_encoding enc,
                           const struct agx3_scs_desc *desc,
                           uint8_t out[AGX3_SCS_MAX_LENGTH], uint8_t *length);

bool agx3_scs_decode_load(enum agx_isa_encoding enc, const uint8_t *bytes,
                          size_t length, struct agx3_scs_desc *out);
bool agx3_scs_decode_store(enum agx_isa_encoding enc, const uint8_t *bytes,
                           size_t length, struct agx3_scs_desc *out);

#ifdef __cplusplus
}
#endif
