/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ioto_preshader_core.h"

#include "asahi/lib/agx_ioto_core.h"

#include <string.h>

bool
hk_agx3_ioto_preshader_program_size(uint32_t start, uint32_t end32,
                                    uint64_t alternate_end, size_t *size)
{
   return agx_ioto_preshader_program_size(start, end32, alternate_end, size);
}

bool
hk_agx3_ioto_preshader_generate(
   enum agx_chip chip, const struct agx_ioto_preshader_desc *desc,
   struct hk_agx3_ioto_preshader_encoding *out)
{
   if (!out || !agx_ioto_target_supported(chip))
      return false;

   struct agx_ioto_preshader_output shared;
   if (!agx_ioto_preshader_emit(chip, desc, &shared))
      return false;

   struct hk_agx3_ioto_preshader_encoding staged = {
      .mode = shared.mode,
      .length = shared.length,
      .branch_offset = shared.branch_offset,
   };
   memcpy(staged.bytes, shared.bytes, shared.length);
   *out = staged;
   return true;
}
