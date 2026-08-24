/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_barrier_core.h"

#include "asahi/lib/agx_vdm_barrier_core.h"

#include <string.h>

bool
d12_agx3_encode_vdm_barrier(enum agx_chip chip,
                            struct d12_agx3_vdm_barrier *out)
{
   struct agx_vdm_barrier_core shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !agx_vdm_barrier_core_pack(chip, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared VDM barrier storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_vdm_barrier(enum agx_chip chip, const void *bytes,
                            uint32_t size)
{
   return agx_vdm_barrier_core_unpack(chip, bytes, size);
}
