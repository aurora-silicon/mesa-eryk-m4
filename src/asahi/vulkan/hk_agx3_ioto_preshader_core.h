/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for the exact bounded AGX3 Ioto terminal suffix.
 */

#pragma once

#include "asahi/lib/agx_ioto_preshader_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hk_agx3_ioto_preshader_encoding {
   enum agx_ioto_preshader_mode mode;
   size_t length;
   size_t branch_offset;
   uint8_t bytes[AGX_IOTO_PRESHADER_MAX_SUFFIX_SIZE];
};

bool hk_agx3_ioto_preshader_program_size(uint32_t start, uint32_t end32,
                                         uint64_t alternate_end,
                                         size_t *size);

/*
 * Byte-generation bridge only; success does not admit a physical device.
 * A non-absent result replaces the straight-line 0e terminal after the
 * profile/tail prefix.  It must not be appended after that terminal.
 * `out` is unchanged on every failure.
 */
bool hk_agx3_ioto_preshader_generate(
   enum agx_chip chip, const struct agx_ioto_preshader_desc *desc,
   struct hk_agx3_ioto_preshader_encoding *out);

#ifdef __cplusplus
}
#endif
