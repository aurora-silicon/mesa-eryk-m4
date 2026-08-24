/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_index_list_core.h"

#include "asahi/lib/agx_vdm_index_list_core.h"

#include <string.h>

bool
d12_agx3_encode_direct_nonindexed_index_list(
   enum agx_chip chip,
   const struct d12_agx3_direct_nonindexed_index_list_info *info,
   struct d12_agx3_direct_nonindexed_index_list *out)
{
   struct agx_vdm_index_list_direct_nonindexed_info shared_info;
   struct agx_vdm_index_list_direct_nonindexed shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !info)
      return false;

   shared_info.primitive = info->primitive;
   shared_info.count = info->count;
   shared_info.instances = info->instances;
   shared_info.start = info->start;

   if (!agx_vdm_index_list_direct_nonindexed_pack(chip, &shared_info,
                                                   &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared non-indexed storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_direct_nonindexed_index_list(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_direct_nonindexed_index_list_info *info)
{
   struct agx_vdm_index_list_direct_nonindexed_info shared_info;

   if (info)
      memset(info, 0, sizeof(*info));

   if (!info || !agx_vdm_index_list_direct_nonindexed_unpack(
                   chip, bytes, size, &shared_info))
      return false;

   info->primitive = shared_info.primitive;
   info->count = shared_info.count;
   info->instances = shared_info.instances;
   info->start = shared_info.start;
   return true;
}

bool
d12_agx3_encode_direct_indexed_index_list(
   enum agx_chip chip,
   const struct d12_agx3_direct_indexed_index_list_info *info,
   struct d12_agx3_direct_indexed_index_list *out)
{
   struct agx_vdm_index_list_direct_indexed_info shared_info;
   struct agx_vdm_index_list_direct_indexed shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !info)
      return false;

   shared_info.primitive = info->primitive;
   shared_info.restart_enable = info->restart_enable;
   shared_info.index_size =
      (enum agx_vdm_index_list_index_size)info->index_size;
   shared_info.index_buffer = info->index_buffer;
   shared_info.count = info->count;
   shared_info.instances = info->instances;
   shared_info.start = info->start;
   shared_info.index_buffer_size_word = info->index_buffer_size_word;

   if (!agx_vdm_index_list_direct_indexed_pack(chip, &shared_info, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared indexed storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_direct_indexed_index_list(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_direct_indexed_index_list_info *info)
{
   struct agx_vdm_index_list_direct_indexed_info shared_info;

   if (info)
      memset(info, 0, sizeof(*info));

   if (!info || !agx_vdm_index_list_direct_indexed_unpack(
                   chip, bytes, size, &shared_info))
      return false;

   info->primitive = shared_info.primitive;
   info->restart_enable = shared_info.restart_enable;
   info->index_size = (enum d12_agx3_index_size)shared_info.index_size;
   info->index_buffer = shared_info.index_buffer;
   info->count = shared_info.count;
   info->instances = shared_info.instances;
   info->start = shared_info.start;
   info->index_buffer_size_word = shared_info.index_buffer_size_word;
   return true;
}
