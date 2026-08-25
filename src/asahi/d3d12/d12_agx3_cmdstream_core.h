/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded D3D12 mapping into exact AGX3 CDM command-stream records.
 */

#pragma once

#include "asahi/genxml/agx3_cmdstream.h"
#include "asahi/lib/agx3_cdm_decode_core.h"
#include "asahi/lib/agx_ioto_core.h"
#include "asahi/lib/agx_ioto_profile_core.h"

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

struct d12_agx3_cdm_link_padding_encoding {
   uint8_t bytes[AGX3_CDM_LINK_PADDING_SIZE];
   uint8_t length;
};

struct d12_agx3_cdm_barrier_encoding {
   uint8_t bytes[AGX3_CDM_BARRIER_SIZE];
   uint8_t length;
};

struct d12_agx3_cdm_secondary_barrier_encoding {
   uint8_t bytes[AGX3_CDM_SECONDARY_BARRIER_SIZE];
   uint8_t length;
};

struct d12_agx3_cdm_stream_terminate_encoding {
   uint8_t bytes[AGX3_CDM_STREAM_TERMINATE_SIZE];
   uint8_t length;
};

struct d12_agx3_ioto_profile_encoding {
   uint8_t bytes[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
   uint8_t length;
};

/* Strict D3D12 bridge into the shared, byte-preserving Ioto classifier. */
static inline bool
d12_agx3_decode_ioto_record(enum agx_chip chip, const void *command,
                            size_t available, struct agx_ioto_record *record,
                            size_t *consumed)
{
   return agx_ioto_decode_one(chip, command, available, record, consumed);
}

/* Expected-kind validation is safe even when two opcode families overlap. */
static inline bool
d12_agx3_validate_ioto_record(enum agx_chip chip,
                              enum agx_ioto_kind expected,
                              const void *command, size_t available,
                              struct agx_ioto_record *record,
                              size_t *consumed)
{
   return agx_ioto_validate_kind(chip, expected, command, available, record,
                                 consumed);
}

/* Bounded D3D12 bridge for the mandatory AGX3 Ioto profile record. */
static inline bool
d12_agx3_encode_ioto_profile(
   enum agx_chip chip, uint16_t payload0, uint16_t payload1,
   struct d12_agx3_ioto_profile_encoding *out)
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

static inline bool
d12_agx3_decode_ioto_profile(enum agx_chip chip, const void *command,
                             uint32_t command_size, uint16_t *payload0,
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

/* Strictly classify one record from a potentially longer AGX3 CDM stream.
 * This is the shared Mesa decoder; it does not follow links or submit work. */
static inline bool
d12_agx3_decode_cdm_record(enum agx_chip chip, const void *command,
                           uint32_t available,
                           struct agx3_cdm_decoded_record *record)
{
   return agx3_cdm_decode_one(chip, command, available, record);
}

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

static inline bool
d12_agx3_encode_cdm_link_padding(
   const struct agx3_cdm_link_padding_info *info,
   struct d12_agx3_cdm_link_padding_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!agx3_cdm_link_padding_pack(info, out->bytes))
      return false;

   out->length = AGX3_CDM_LINK_PADDING_SIZE;
   return true;
}

static inline bool
d12_agx3_decode_cdm_link_padding(
   enum agx_chip chip, const void *command, uint32_t command_size,
   struct agx3_cdm_link_padding_info *info)
{
   if (!info)
      return false;

   memset(info, 0, sizeof(*info));
   return command_size == AGX3_CDM_LINK_PADDING_SIZE &&
          agx3_cdm_link_padding_unpack(chip, command, command_size, info);
}

static inline bool
d12_agx3_encode_cdm_barrier(
   const struct agx3_cdm_barrier_info *info,
   struct d12_agx3_cdm_barrier_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!agx3_cdm_barrier_pack(info, out->bytes))
      return false;

   out->length = AGX3_CDM_BARRIER_SIZE;
   return true;
}

static inline bool
d12_agx3_decode_cdm_barrier(enum agx_chip chip, const void *command,
                            uint32_t command_size,
                            struct agx3_cdm_barrier_info *info)
{
   if (!info)
      return false;

   memset(info, 0, sizeof(*info));
   return command_size == AGX3_CDM_BARRIER_SIZE &&
          agx3_cdm_barrier_unpack(chip, command, command_size, info);
}

static inline bool
d12_agx3_encode_cdm_substream_barrier(
   const struct agx3_cdm_substream_barrier_info *info,
   struct d12_agx3_cdm_barrier_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!agx3_cdm_substream_barrier_pack(info, out->bytes))
      return false;

   out->length = AGX3_CDM_BARRIER_SIZE;
   return true;
}

static inline bool
d12_agx3_decode_cdm_substream_barrier(
   enum agx_chip chip, const void *command, uint32_t command_size,
   struct agx3_cdm_substream_barrier_info *info)
{
   if (!info)
      return false;

   memset(info, 0, sizeof(*info));
   return command_size == AGX3_CDM_BARRIER_SIZE &&
          agx3_cdm_substream_barrier_unpack(chip, command, command_size,
                                            info);
}

static inline bool
d12_agx3_encode_cdm_secondary_barrier(
   enum agx_chip chip, struct d12_agx3_cdm_secondary_barrier_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!agx3_cdm_secondary_barrier_pack(chip, out->bytes))
      return false;

   out->length = AGX3_CDM_SECONDARY_BARRIER_SIZE;
   return true;
}

static inline bool
d12_agx3_validate_cdm_secondary_barrier(enum agx_chip chip,
                                        const void *command,
                                        uint32_t command_size)
{
   return command_size == AGX3_CDM_SECONDARY_BARRIER_SIZE &&
          agx3_cdm_secondary_barrier_validate(chip, command, command_size);
}

static inline bool
d12_agx3_encode_cdm_stream_terminate(
   enum agx_chip chip, struct d12_agx3_cdm_stream_terminate_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!agx3_cdm_stream_terminate_pack(chip, out->bytes))
      return false;

   out->length = AGX3_CDM_STREAM_TERMINATE_SIZE;
   return true;
}

static inline bool
d12_agx3_validate_cdm_stream_terminate(enum agx_chip chip,
                                       const void *command,
                                       uint32_t command_size)
{
   return command_size == AGX3_CDM_STREAM_TERMINATE_SIZE &&
          agx3_cdm_stream_terminate_validate(chip, command, command_size);
}
