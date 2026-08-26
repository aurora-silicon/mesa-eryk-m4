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
#define AGX_IOTO_G17S_MINIMAL_PROFILE_LOADER_SIZE 52

/*
 * Bounded canonical subset of Ioto SETPROFILECTL_0.  payload0/payload1 hold
 * already-positioned operand bits, not complete encoded halfwords.  Packing
 * rejects every bit outside the recovered field scatters instead of emitting
 * values into fixed or unattributed positions.
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

/* Build the exact direct HAL300 SETPROFILECTL operand tuple recovered from
 * G17S.  This record is used only when Apple's profile selects the direct
 * path; the ordinary minimal compute profile uses the loader below instead. */
bool agx_ioto_g17s_profile_control_build(
   uint8_t value, bool mode_table_bit, bool high_bit,
   struct agx_ioto_profile_control *profile);

/* Build the exact classic SETPROFILECTL tuple emitted by the A18 Pro/G17P
 * compiler path.  Apple's four-way value is permuted through {2, 0, 1, 3};
 * table_bit and high_bit are the independently recovered profile selectors.
 * `profile` is unchanged on failure. */
bool agx_ioto_g17p_compiler_profile_control_build(
   uint8_t value, bool table_bit, bool high_bit,
   struct agx_ioto_profile_control *profile);

/* Exact minimal-profile loader emitted by G17S after the state tail.  A
 * zero-payload SETPROFILECTL is not an equivalent encoding. */
bool agx_ioto_g17s_minimal_profile_loader_pack(
   void *output, size_t capacity);

/* Decode one record from a possibly longer stream and return its byte count. */
bool agx_ioto_profile_control_unpack(enum agx_chip chip, const void *stream,
                                     size_t available,
                                     struct agx_ioto_profile_control *profile,
                                     size_t *consumed);

#ifdef __cplusplus
}
#endif
