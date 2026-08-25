/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_vdm_decode.h"

bool
hk_agx3_decode_vdm_record(enum agx_chip chip, const void *command,
                          size_t available,
                          struct agx_vdm_decoded_record *record)
{
   return agx_vdm_decode_one(chip, command, available, record);
}

bool
hk_agx3_encode_vdm_state_update_template(
   enum agx_chip chip, const struct agx_vdm_state_update_template_info *info,
   struct agx_vdm_state_update *out)
{
   return agx_vdm_state_update_template_pack(chip, info, out);
}
