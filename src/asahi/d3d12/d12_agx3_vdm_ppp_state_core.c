/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_ppp_state_core.h"

#include "asahi/lib/agx_vdm_ppp_state_core.h"

#include <string.h>

bool
d12_agx3_encode_vdm_ppp_state(enum agx_chip chip, uint64_t pointer,
                              uint32_t size_words,
                              struct d12_agx3_vdm_ppp_state *out)
{
   struct agx_vdm_ppp_state_core shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !agx_vdm_ppp_state_core_pack(chip, pointer, size_words, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared VDM PPP State storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_vdm_ppp_state(enum agx_chip chip, const void *bytes,
                              uint32_t size, uint64_t *pointer,
                              uint32_t *size_words)
{
   return agx_vdm_ppp_state_core_unpack(chip, bytes, size, pointer, size_words);
}
