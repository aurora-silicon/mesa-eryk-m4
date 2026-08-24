/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded D3D12 mapping into exact AGX3 CDM command-stream records.
 */

#pragma once

#include "asahi/genxml/agx3_cmdstream.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct d12_agx3_cdm_launch_encoding {
   uint8_t bytes[AGX3_CDM_LAUNCH_SIZE];
   uint8_t length;
};

struct d12_agx3_cdm_stream_link_encoding {
   uint8_t bytes[AGX3_CDM_STREAM_LINK_SIZE];
   uint8_t length;
};

/* Host representation only.  These helpers do not emit a live command list,
 * bind a queue, submit work, open an AGX3 device, or expose a D3D feature. */
static inline bool
d12_agx3_encode_cdm_launch(
   const struct agx3_cdm_launch_info *info,
   struct d12_agx3_cdm_launch_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!agx3_cdm_launch_pack(info, out->bytes))
      return false;

   out->length = AGX3_CDM_LAUNCH_SIZE;
   return true;
}

static inline bool
d12_agx3_decode_cdm_launch(enum agx_chip chip, const void *command,
                           uint32_t command_size,
                           struct agx3_cdm_launch_info *info)
{
   if (!info)
      return false;

   memset(info, 0, sizeof(*info));
   return command_size == AGX3_CDM_LAUNCH_SIZE &&
          agx3_cdm_launch_unpack(chip, command, command_size, info);
}

static inline bool
d12_agx3_encode_cdm_stream_link(
   const struct agx3_cdm_stream_link_info *info,
   struct d12_agx3_cdm_stream_link_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!agx3_cdm_stream_link_pack(info, out->bytes))
      return false;

   out->length = AGX3_CDM_STREAM_LINK_SIZE;
   return true;
}

static inline bool
d12_agx3_decode_cdm_stream_link(
   enum agx_chip chip, const void *command, uint32_t command_size,
   struct agx3_cdm_stream_link_info *info)
{
   if (!info)
      return false;

   memset(info, 0, sizeof(*info));
   return command_size == AGX3_CDM_STREAM_LINK_SIZE &&
          agx3_cdm_stream_link_unpack(chip, command, command_size, info);
}
