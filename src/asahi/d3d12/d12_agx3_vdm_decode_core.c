/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_decode_core.h"

bool
d12_agx3_decode_vdm_record(enum agx_chip chip, const void *command,
                           uint32_t available,
                           struct agx_vdm_decoded_record *record)
{
   return agx_vdm_decode_one(chip, command, available, record);
}
