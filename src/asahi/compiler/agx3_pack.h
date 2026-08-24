/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Largest instruction emitted by the currently proven AGX3 subset. */
#define AGX3_LDIMM_MAX_LENGTH 8

struct agx3_encoded_ldimm {
   uint8_t bytes[AGX3_LDIMM_MAX_LENGTH];
   uint8_t length;
};

/*
 * Encode the source-grounded 32-bit-register form of AGX3 `ldimm`.
 *
 * `dest` is a 16-bit half-register index, matching agx_index.value and the
 * register fields recovered from the rp_0..rp_64 probes.  A 32-bit destination
 * must therefore be even.  This deliberately does not expose the other
 * operand-kind forms, the 10-byte encoding points, or M5's unobserved ninth
 * destination bit.
 *
 * Returns false, without producing an instruction, for every unsupported or
 * malformed request.  In particular AGX2/UNKNOWN encodings, odd destinations,
 * and destinations outside Mesa's 256-half-register file fail closed.
 */
bool agx3_pack_ldimm_u32(enum agx_isa_encoding encoding, uint16_t dest,
                         uint32_t imm, struct agx3_encoded_ldimm *out);

/*
 * Decode exactly the same bounded semantic subset.  Unknown/reserved bits must
 * be zero, so a future form is rejected instead of being mistaken for u32.
 */
bool agx3_unpack_ldimm_u32(enum agx_isa_encoding encoding,
                           const uint8_t *bytes, size_t available,
                           uint16_t *dest, uint32_t *imm, uint8_t *length);

#ifdef __cplusplus
}
#endif
