/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_stream_terminate_core.h"

#include "asahi/lib/agx_vdm_stream_terminate_core.h"

#include <string.h>

bool
d12_agx3_encode_vdm_stream_terminate(enum agx_chip chip,
                                     struct d12_agx3_vdm_stream_terminate *out)
{
   struct agx_vdm_stream_terminate_core shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !agx_vdm_stream_terminate_core_pack(chip, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared VDM Stream Terminate storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_vdm_stream_terminate(enum agx_chip chip, const void *bytes,
                                     uint32_t size)
{
   return agx_vdm_stream_terminate_core_unpack(chip, bytes, size);
}
