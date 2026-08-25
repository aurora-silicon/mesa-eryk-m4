/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ioto_profile_core.h"

#include <string.h>

bool
hk_agx3_ioto_profile_generate(enum agx_chip chip, uint16_t payload0,
                              uint16_t payload1,
                              struct hk_agx3_ioto_profile_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   const struct agx_ioto_profile_control profile = {
      .chip = chip,
      .payload0 = payload0,
      .payload1 = payload1,
   };
   if (!agx_ioto_profile_control_pack(&profile, out->bytes))
      return false;

   out->length = (uint8_t)agx_ioto_profile_control_size(chip);
   return true;
}

bool
hk_agx3_ioto_profile_decode(enum agx_chip chip, const void *command,
                            size_t command_size, uint16_t *payload0,
                            uint16_t *payload1)
{
   if (payload0)
      *payload0 = 0;
   if (payload1)
      *payload1 = 0;
   if (!payload0 || !payload1 ||
       command_size != agx_ioto_profile_control_size(chip))
      return false;

   struct agx_ioto_profile_control profile;
   size_t consumed;
   if (!agx_ioto_profile_control_unpack(chip, command, command_size, &profile,
                                        &consumed) ||
       consumed != command_size)
      return false;

   *payload0 = profile.payload0;
   *payload1 = profile.payload1;
   return true;
}
