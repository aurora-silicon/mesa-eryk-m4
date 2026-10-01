/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_ioto_uniform_program.h"

#include <limits.h>
#include <string.h>

#define GPU0_CHUNK_BODY_BOUND 78u
#define GPU1_CHUNK_BODY_BOUND 64u

static bool
uniform_range_valid(const struct agx3_ioto_uniform_range *range)
{
   if (range == NULL || range->size_halfs == 0 ||
       (range->start_halfs & 1) != 0 || (range->size_halfs & 1) != 0 ||
       range->start_halfs > 512 ||
       range->size_halfs > 512 - range->start_halfs ||
       (range->address & 3) != 0)
      return false;

   const uint64_t bytes = (uint64_t)(range->size_halfs / 2) * 4;
   return bytes != 0 && range->address <= UINT64_MAX - (bytes - 1);
}

static bool
add_size(size_t *value, size_t add)
{
   if (*value > SIZE_MAX - add)
      return false;
   *value += add;
   return true;
}

bool
agx3_ioto_uniform_program_body_bound(
   enum agx_chip chip, const struct agx3_ioto_uniform_range *ranges,
   size_t range_count, size_t *bytes, size_t *chunks)
{
   const enum agx_ioto_load_encoding encoding =
      agx_ioto_load_target_encoding(chip);
   if (bytes == NULL || chunks == NULL ||
       encoding == AGX_IOTO_LOAD_ENCODING_UNSUPPORTED ||
       (range_count != 0 && ranges == NULL))
      return false;

   size_t staged_bytes =
      range_count == 0 && encoding == AGX_IOTO_LOAD_ENCODING_GPU0
         ? AGX_IOTO_LOAD_GPU0_ROUND_NOP_SIZE
         : 0;
   size_t staged_chunks = 0;
   const size_t per_chunk =
      encoding == AGX_IOTO_LOAD_ENCODING_GPU0 ? GPU0_CHUNK_BODY_BOUND
                                             : GPU1_CHUNK_BODY_BOUND;

   for (size_t i = 0; i < range_count; ++i) {
      if (!uniform_range_valid(&ranges[i]))
         return false;

      const size_t range_chunks = (ranges[i].size_halfs + 7u) / 8u;
      if (!add_size(&staged_chunks, range_chunks) ||
          range_chunks > SIZE_MAX / per_chunk ||
          !add_size(&staged_bytes, range_chunks * per_chunk))
         return false;
   }

   *bytes = staged_bytes;
   *chunks = staged_chunks;
   return true;
}

static bool
spans_overlap(const void *a, size_t a_size, const void *b, size_t b_size)
{
   if (a_size == 0 || b_size == 0)
      return false;
   if (a == NULL || b == NULL)
      return true;

   const uintptr_t a0 = (uintptr_t)a, b0 = (uintptr_t)b;
   if (a0 > UINTPTR_MAX - a_size || b0 > UINTPTR_MAX - b_size)
      return true;
   return a0 < b0 + b_size && b0 < a0 + a_size;
}

static bool
array_overlaps(const void *array, size_t count, size_t element_size,
               const void *output, size_t output_size)
{
   return count > SIZE_MAX / element_size ||
          spans_overlap(array, count * element_size, output, output_size);
}

static bool
workspace_overlaps(struct agx3_ioto_uniform_program_workspace workspace)
{
   if (workspace.load_capacity >
          SIZE_MAX / sizeof(struct agx_ioto_inflight_load) ||
       workspace.sort_capacity >
          SIZE_MAX / sizeof(struct agx_ioto_inflight_load) ||
       workspace.cache_capacity >
          SIZE_MAX / sizeof(struct agx_ioto_load_cache_entry))
      return true;

   const size_t loads_size =
      workspace.load_capacity * sizeof(struct agx_ioto_inflight_load);
   const size_t sort_size =
      workspace.sort_capacity * sizeof(struct agx_ioto_inflight_load);
   const size_t cache_size =
      workspace.cache_capacity * sizeof(struct agx_ioto_load_cache_entry);

   return spans_overlap(workspace.body, workspace.body_capacity,
                        workspace.loads, loads_size) ||
          spans_overlap(workspace.body, workspace.body_capacity, workspace.sort,
                        sort_size) ||
          spans_overlap(workspace.body, workspace.body_capacity,
                        workspace.cache, cache_size) ||
          spans_overlap(workspace.loads, loads_size, workspace.sort,
                        sort_size) ||
          spans_overlap(workspace.loads, loads_size, workspace.cache,
                        cache_size) ||
          spans_overlap(workspace.sort, sort_size, workspace.cache, cache_size);
}

static bool
uniform_program_body_generate(
   enum agx_chip chip, const struct agx3_ioto_uniform_program_desc *desc,
   struct agx3_ioto_uniform_program_workspace workspace, size_t *written,
   size_t *copy_offset, struct agx3_ioto_uniform_template *cache)
{
   size_t body_bound, chunk_count;
   if (desc == NULL || written == NULL || copy_offset == NULL ||
       !agx3_ioto_uniform_program_body_bound(
          chip, desc != NULL ? desc->ranges : NULL,
          desc != NULL ? desc->range_count : 0, &body_bound, &chunk_count) ||
       body_bound > workspace.body_capacity ||
       (workspace.body_capacity != 0 && workspace.body == NULL) ||
       workspace_overlaps(workspace))
      return false;

   (void)chunk_count;
   struct agx_ioto_load_core core;
   if (!agx_ioto_load_core_init(
          &core, chip,
          (struct agx_ioto_load_sink){workspace.body, workspace.body_capacity,
                                      0},
          workspace.loads, workspace.load_capacity, workspace.cache,
          workspace.cache_capacity, desc->initial_gpr_dwords,
          desc->fast_path_enabled,
          agx_ioto_load_target_encoding(chip) == AGX_IOTO_LOAD_ENCODING_GPU0))
      return false;

   const struct agx_ioto_load_workspace sort = {
      workspace.sort,
      workspace.sort_capacity,
   };
   uint32_t cprog_dest_half = 0;
   uint16_t base_reloc[256] = {0};

   for (size_t i = 0; i < desc->range_count; ++i) {
      const struct agx3_ioto_uniform_range *range = &desc->ranges[i];
      uint32_t dword = 0;
      const uint32_t dword_count = range->size_halfs / 2;
      while (dword < dword_count) {
         const uint8_t count =
            (uint8_t)((dword_count - dword) < 4 ? dword_count - dword : 4);
         const uint8_t lane_mask = (uint8_t)((1u << (2u * count)) - 1u);
         if (cprog_dest_half > UINT16_MAX)
            return false;

         const struct agx_ioto_load_absolute load = {
            .native_slot = range->start_halfs / 2 + dword,
            .count_dwords = count,
            .address = range->address + (uint64_t)dword * 4,
            .lane_mask = lane_mask,
            .cprog_dest_half = (uint16_t)cprog_dest_half,
         };
         struct agx_ioto_load_allocation allocation;
         if (!agx3_ioto_load_absolute_native(&core, &load, sort, &allocation))
            return false;

         if (cache) {
            const struct agx_ioto_inflight_load *queued =
               &core.loads[core.load_count - 1];
            const unsigned index = cache->load_count++;
            struct agx3_ioto_uniform_reloc *reloc = &cache->loads[index];
            *reloc = (struct agx3_ioto_uniform_reloc){
               .range = i, .range_offset = dword * 4,
               .destination = allocation.temp_half,
               .delta = queued->byte_offset,
            };
            if (queued->base.half == allocation.temp_half) {
               uint8_t tmp[8];
               if (!agx_ioto_load_pack_ldimm(chip, allocation.temp_half,
                      load.address, tmp, &reloc->low_length) ||
                   !agx_ioto_load_pack_ldimm(chip, allocation.temp_half + 2,
                      load.address >> 32, tmp, &reloc->high_length))
                  return false;
               reloc->offset = core.sink.length - reloc->low_length -
                               reloc->high_length;
               base_reloc[queued->base.half] = index;
            }
            reloc->base = base_reloc[queued->base.half];
         }

         cprog_dest_half += 2u * (2u * count);
         dword += count;
      }
   }

   if (!agx3_ioto_load_finish_round_native(&core, true, sort))
      return false;

   /* The builder already knows record boundaries, including variable-size
    * COPYs and allocation rollovers. Do not decode its output on every draw. */
   *written = core.sink.length;
   *copy_offset = core.copy_offset;
   return true;
}

bool
agx3_ioto_uniform_program_body_generate(
   enum agx_chip chip, const struct agx3_ioto_uniform_program_desc *desc,
   struct agx3_ioto_uniform_program_workspace ws, size_t *written,
   size_t *copy_offset)
{
   return uniform_program_body_generate(chip, desc, ws, written, copy_offset, NULL);
}

static bool
uniform_template_apply(enum agx_chip chip,
   const struct agx3_ioto_uniform_program_desc *desc,
   struct agx3_ioto_uniform_program_workspace ws, size_t *written,
   size_t *copy_offset, const struct agx3_ioto_uniform_template *cache)
{
   if (!cache->valid || cache->chip != chip ||
       cache->initial_gprs != desc->initial_gpr_dwords ||
       cache->fast_path != desc->fast_path_enabled ||
       cache->range_count != desc->range_count || cache->length > ws.body_capacity)
      return false;
   for (unsigned i = 0; i < cache->range_count; ++i) {
      if (cache->ranges[i].start_halfs != desc->ranges[i].start_halfs ||
          cache->ranges[i].size_halfs != desc->ranges[i].size_halfs)
         return false;
   }
   memcpy(ws.body, cache->body, cache->length);
   for (unsigned i = 0; i < cache->load_count; ++i) {
      const struct agx3_ioto_uniform_reloc *r = &cache->loads[i];
      const uint64_t address = desc->ranges[r->range].address + r->range_offset;
      if (!r->low_length) {
         const struct agx3_ioto_uniform_reloc *b = &cache->loads[r->base];
         const uint64_t base = desc->ranges[b->range].address + b->range_offset;
         if (address < base || address - base != (uint64_t)r->delta)
            return false;
      } else {
         uint8_t lo[8], hi[8], low_length, high_length;
         if (!agx_ioto_load_pack_ldimm(chip, r->destination, address, lo, &low_length) ||
             !agx_ioto_load_pack_ldimm(chip, r->destination + 2, address >> 32,
                                       hi, &high_length) ||
             low_length != r->low_length || high_length != r->high_length)
            return false;
         memcpy(ws.body + r->offset, lo, low_length);
         memcpy(ws.body + r->offset + low_length, hi, high_length);
      }
   }
   *written = cache->length;
   *copy_offset = cache->copy_offset;
   return true;
}

bool
agx3_ioto_uniform_program_body_generate_cached(
   enum agx_chip chip, const struct agx3_ioto_uniform_program_desc *desc,
   struct agx3_ioto_uniform_program_workspace ws, size_t *written,
   size_t *copy_offset, struct agx3_ioto_uniform_template *cache)
{
   size_t bound, chunks;
   if (!cache)
      return uniform_program_body_generate(chip, desc, ws, written, copy_offset, NULL);
   if (!desc || !written || !copy_offset || !ws.body || workspace_overlaps(ws) ||
       !agx3_ioto_uniform_program_body_bound(chip, desc->ranges, desc->range_count,
                                             &bound, &chunks))
      return false;
   if (uniform_template_apply(chip, desc, ws, written, copy_offset, cache))
      return true;
   cache->valid = false;
   cache->load_count = 0;
   if (desc->range_count > AGX3_IOTO_TEMPLATE_RANGES ||
       chunks > AGX3_IOTO_TEMPLATE_LOADS || bound > AGX3_IOTO_TEMPLATE_BYTES)
      return uniform_program_body_generate(chip, desc, ws, written, copy_offset, NULL);
   if (!uniform_program_body_generate(chip, desc, ws, written, copy_offset, cache))
      return false;
   cache->chip = chip;
   cache->initial_gprs = desc->initial_gpr_dwords;
   cache->fast_path = desc->fast_path_enabled;
   cache->range_count = desc->range_count;
   if (desc->range_count)
      memcpy(cache->ranges, desc->ranges, desc->range_count * sizeof(desc->ranges[0]));
   cache->length = *written;
   cache->copy_offset = *copy_offset;
   memcpy(cache->body, ws.body, *written);
   cache->valid = true;
   return true;
}

bool
agx3_ioto_uniform_program_generate(
   enum agx_chip chip, const struct agx3_ioto_uniform_program_desc *desc,
   struct agx3_ioto_uniform_program_workspace workspace, void *output,
   size_t capacity, size_t *written)
{
   size_t body_bound, chunk_count;
   if (desc == NULL || output == NULL || written == NULL ||
       !agx3_ioto_uniform_program_body_bound(
          chip, desc != NULL ? desc->ranges : NULL,
          desc != NULL ? desc->range_count : 0, &body_bound, &chunk_count) ||
       body_bound > workspace.body_capacity ||
       (workspace.body_capacity != 0 && workspace.body == NULL) ||
       workspace_overlaps(workspace) ||
       spans_overlap(workspace.body, workspace.body_capacity, output,
                     capacity) ||
       array_overlaps(workspace.loads, workspace.load_capacity,
                      sizeof(workspace.loads[0]), output, capacity) ||
       array_overlaps(workspace.sort, workspace.sort_capacity,
                      sizeof(workspace.sort[0]), output, capacity) ||
       array_overlaps(workspace.cache, workspace.cache_capacity,
                      sizeof(workspace.cache[0]), output, capacity))
      return false;

   (void)body_bound;
   (void)chunk_count;
   size_t body_size, copy_offset;
   if (!agx3_ioto_uniform_program_body_generate(chip, desc, workspace,
                                                &body_size, &copy_offset))
      return false;
   (void)copy_offset;

   const struct agx_ioto_profile_control profile = {
      .chip = chip,
      .payload0 = desc->profile_payload0,
      .payload1 = desc->profile_payload1,
   };
   return agx_ioto_program_compose_no_pixrel(
      chip, desc->program_va, workspace.body, body_size, &desc->tail, &profile,
      &desc->preshader, output, capacity, written);
}
