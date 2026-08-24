/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_sparse_residency_core.h"

#include <limits.h>
#include <stddef.h>

bool
d12_sparse_residency_index(uint64_t resource_offset_B,
                           uint64_t layer_stride_B, uint32_t layer_count,
                           uint32_t sparse_folios_per_layer,
                           uint64_t sparse_table_size_B, uint32_t *index_el)
{
   if (!index_el || !layer_stride_B || !layer_count ||
       !sparse_folios_per_layer ||
       (resource_offset_B % D12_AGX_SPARSE_PAGE_SIZE_B) != 0 ||
       (layer_stride_B % D12_AGX_SPARSE_PAGE_SIZE_B) != 0 ||
       (sparse_table_size_B % D12_AGX_SPARSE_ELEMENT_SIZE_B) != 0)
      return false;

   const uint64_t layer = resource_offset_B / layer_stride_B;
   if (layer >= layer_count)
      return false;

   const uint64_t offset_in_layer = resource_offset_B % layer_stride_B;
   const uint64_t page = offset_in_layer / D12_AGX_SPARSE_PAGE_SIZE_B;
   const uint64_t folio = page / D12_AGX_SPARSE_PAGES_PER_FOLIO;
   if (folio >= sparse_folios_per_layer)
      return false;

   const uint64_t index_in_folio =
      page % D12_AGX_SPARSE_PAGES_PER_FOLIO;
   const uint64_t layer_folio =
      layer * sparse_folios_per_layer + folio;
   if (layer_folio >
       (UINT64_MAX - index_in_folio) /
          D12_AGX_SPARSE_ELEMENTS_PER_FOLIO)
      return false;

   const uint64_t index =
      layer_folio * D12_AGX_SPARSE_ELEMENTS_PER_FOLIO + index_in_folio;
   if (index > UINT32_MAX ||
       index >= sparse_table_size_B / D12_AGX_SPARSE_ELEMENT_SIZE_B)
      return false;

   *index_el = (uint32_t)index;
   return true;
}

bool
d12_sparse_texture_access_index(uint64_t view_offset_B,
                                uint64_t layer_stride_B, uint32_t layer,
                                uint32_t view_layer_count,
                                uint32_t sparse_folios_per_layer,
                                uint64_t sparse_table_size_B,
                                uint32_t *index_el)
{
   if (!index_el || !layer_stride_B || layer >= view_layer_count ||
       !sparse_folios_per_layer ||
       layer > UINT64_MAX / layer_stride_B)
      return false;

   const uint64_t layer_base_B = (uint64_t)layer * layer_stride_B;
   if (view_offset_B < layer_base_B ||
       view_offset_B - layer_base_B >= layer_stride_B)
      return false;

   const uint64_t offset_in_layer_B = view_offset_B - layer_base_B;
   const uint64_t page_offset_B =
      offset_in_layer_B & ~(D12_AGX_SPARSE_PAGE_SIZE_B - 1);
   if (layer_base_B > UINT64_MAX - page_offset_B)
      return false;

   return d12_sparse_residency_index(
      layer_base_B + page_offset_B, layer_stride_B, view_layer_count,
      sparse_folios_per_layer, sparse_table_size_B, index_el);
}

bool
d12_sparse_buffer_map_size(uint32_t tile_count, uint64_t alignment_B,
                           uint64_t *size_B)
{
   if (!size_B || !tile_count || !alignment_B ||
       (alignment_B & (alignment_B - 1)) != 0)
      return false;

   const uint64_t bytes = (uint64_t)tile_count * sizeof(uint32_t);
   if (bytes > UINT64_MAX - (alignment_B - 1))
      return false;
   *size_B = (bytes + alignment_B - 1) & ~(alignment_B - 1);
   return *size_B != 0;
}

bool
d12_sparse_buffer_size(uint64_t width_B, uint32_t max_gpu_va_bits,
                       uint64_t *aligned_size_B, uint32_t *tile_count)
{
   if (!aligned_size_B || !tile_count || !width_B ||
       !max_gpu_va_bits || max_gpu_va_bits >= 64)
      return false;

   const uint64_t max_size_B = UINT64_C(1) << max_gpu_va_bits;
   if (width_B > max_size_B ||
       width_B > UINT64_MAX - (D12_SPARSE_BUFFER_TILE_SIZE_B - 1))
      return false;

   const uint64_t aligned =
      (width_B + D12_SPARSE_BUFFER_TILE_SIZE_B - 1) &
      ~(D12_SPARSE_BUFFER_TILE_SIZE_B - 1);
   const uint64_t tiles = aligned / D12_SPARSE_BUFFER_TILE_SIZE_B;
   if (!aligned || tiles > UINT32_MAX)
      return false;

   *aligned_size_B = aligned;
   *tile_count = (uint32_t)tiles;
   return true;
}

bool
d12_sparse_buffer_residency_index(uint64_t resource_offset_B,
                                  uint64_t resource_size_B,
                                  uint32_t tile_count, uint32_t *index_el)
{
   if (!index_el || !resource_size_B || !tile_count ||
       resource_offset_B >= resource_size_B)
      return false;

   const uint64_t index =
      resource_offset_B / D12_SPARSE_BUFFER_TILE_SIZE_B;
   if (index >= tile_count || index > UINT32_MAX)
      return false;
   *index_el = (uint32_t)index;
   return true;
}

bool
d12_sparse_buffer_access_tiles(uint32_t view_phase_B,
                               uint64_t byte_offset_B,
                               uint32_t access_size_B,
                               uint64_t view_size_B,
                               uint32_t map_word_count,
                               uint32_t *first_index,
                               uint32_t *last_index)
{
   if (!first_index || !last_index ||
       view_phase_B >= D12_SPARSE_BUFFER_TILE_SIZE_B || !access_size_B ||
       byte_offset_B >= view_size_B || !map_word_count ||
       byte_offset_B > UINT64_MAX - (access_size_B - 1))
      return false;

   uint64_t last = byte_offset_B + access_size_B - 1;
   if (last >= view_size_B)
      last = view_size_B - 1;
   if (byte_offset_B > UINT64_MAX - view_phase_B ||
       last > UINT64_MAX - view_phase_B)
      return false;

   const uint64_t first =
      (view_phase_B + byte_offset_B) / D12_SPARSE_BUFFER_TILE_SIZE_B;
   const uint64_t final =
      (view_phase_B + last) / D12_SPARSE_BUFFER_TILE_SIZE_B;
   if (first >= map_word_count || final >= map_word_count ||
       first > UINT32_MAX || final > UINT32_MAX)
      return false;

   *first_index = (uint32_t)first;
   *last_index = (uint32_t)final;
   return true;
}

bool
d12_sparse_registry_allocation_size(uint32_t capacity, uint64_t alignment_B,
                                    uint64_t *size_B)
{
   if (!size_B || !capacity || !alignment_B ||
       (alignment_B & (alignment_B - 1)) != 0 ||
       (uint64_t)capacity >
          UINT64_MAX / sizeof(struct d12_sparse_registry_record))
      return false;

   const uint64_t bytes =
      (uint64_t)capacity * sizeof(struct d12_sparse_registry_record);
   if (bytes > UINT64_MAX - (alignment_B - 1))
      return false;
   *size_B = (bytes + alignment_B - 1) & ~(alignment_B - 1);
   return *size_B != 0;
}

bool
d12_sparse_registry_lookup(
   const struct d12_sparse_registry_record *records, uint32_t count,
   uint64_t address, uint32_t *index)
{
   if (!records || !count || !address || !index)
      return false;

   uint64_t previous_end = 0;
   for (uint32_t i = 0; i < count; ++i) {
      const struct d12_sparse_registry_record record = records[i];
      if (!record.base || !record.size ||
          record.base > UINT64_MAX - record.size ||
          (i && record.base < previous_end))
         return false;
      previous_end = record.base + record.size;
   }

   uint32_t low = 0;
   uint32_t high = count;
   while (low < high) {
      const uint32_t middle = low + (high - low) / 2;
      const struct d12_sparse_registry_record record = records[middle];

      if (address < record.base) {
         high = middle;
      } else if (address - record.base < record.size) {
         *index = middle;
         return true;
      } else {
         low = middle + 1;
      }
   }
   return false;
}
