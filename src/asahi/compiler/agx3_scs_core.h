/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * AGX3 scalar SCS load/store encoders and decoders.
 *
 * The supported address-space(5) forms were recovered with controlled Apple
 * G17P/G17S compiler probes: direct-absolute, and a scalar register index
 * scaled by the access width plus a uniform-register base and signed 16-bit
 * byte displacement.  Register-base, other scales, and vector forms are not
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

enum agx3_scs_address_kind {
   /* base 0 + index 0 + byte_offset */
   AGX3_SCS_ADDRESS_DIRECT = 0,

   /* uniform[uniform_base] + gpr[index] * (1 << width) + byte_offset */
   AGX3_SCS_ADDRESS_UNIFORM_INDEXED = 1,
};

struct agx3_scs_desc {
   uint16_t data; /* load destination / store value, half-register units */
   enum agx3_mem_width width;
   int16_t byte_offset;

   enum agx3_scs_address_kind address_kind;
   uint16_t index;        /* scalar GPR, half-register units */
   uint16_t uniform_base; /* scalar uniform, half-register units */

   uint8_t slot;       /* load only: 1..6 (b), 1..8 (c) */
   uint16_t wait_mask; /* bit i waits for scoreboard slot i + 1 */
   bool value_kill;    /* store only; witnessed only on AGX3c */

   /* AGX3c narrow loads may zero-extend to the 32-bit destination.  This is
    * exact only for uniform-indexed W8/W16 loads; every other use fails.
    */
   bool zero_extend;

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
