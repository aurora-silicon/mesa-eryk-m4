/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "agx_chip.h"
#include "agx_ioto_preshader_core.h"
#include "agx_ioto_profile_core.h"
#include "agx_ioto_state_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_IOTO_PROGRAM_ALIGNMENT 64u
#define AGX_IOTO_PROGRAM_TERMINATE_SIZE 4u
#define AGX_IOTO_PROGRAM_TERMINATE_WORD UINT32_C(0x0000000e)

/*
 * Compose the bounded no-PIXREL program form recovered from Apple's AGX3
 * state-load encoder:
 *
 *    validated scheduled body
 *    SETPROFILECTL
 *    LoadShader state tail (SPECLM is omitted for tail type 1)
 *    exactly one terminal preshader suffix
 *
 * The body may contain only non-control records whose target-specific byte
 * boundaries can be decoded exactly; ambiguous encodings are rejected.
 * State-tail, profile, branch/call/return and pixel-protocol records are
 * rejected.  Program-kind and PIXREL modes are intentionally not exposed by
 * this bounded API.
 *
 * program_va is the address consumed separately by the AGX3 CDM/VDM pipeline
 * pointer and must therefore be a 48-bit, 64-byte-aligned GPU virtual address.
 * It is validation metadata and is not embedded in the program bytes.
 *
 * The operation is transactional: output and *written are unchanged on every
 * failure.  body may be NULL only when body_size is zero and may overlap
 * output on success.  preshader code may also overlap output.
 */
bool agx_ioto_program_compose_no_pixrel(
   enum agx_chip chip, uint64_t program_va, const void *body, size_t body_size,
   const struct agx_ioto_state_tail *tail,
   const struct agx_ioto_profile_control *profile,
   const struct agx_ioto_preshader_desc *preshader, void *output,
   size_t capacity, size_t *written);

/* Convenience form whose sole terminal suffix is little-endian 0x0000000e. */
bool agx_ioto_straight_line_program_compose(
   enum agx_chip chip, uint64_t program_va, const void *body, size_t body_size,
   const struct agx_ioto_state_tail *tail,
   const struct agx_ioto_profile_control *profile, void *output,
   size_t capacity, size_t *written);

#ifdef __cplusplus
}
#endif
