/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Portable address-to-entry arithmetic for AGX sparse image page tables.
 * Keep this independent of Mesa, Direct3D and generated pack headers so the
 * validation which precedes every page-table mutation is host-testable.
 */

#ifndef D12_SPARSE_RESIDENCY_CORE_H
#define D12_SPARSE_RESIDENCY_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX_SPARSE_PAGE_SIZE_B       UINT64_C(16384)
#define D12_AGX_SPARSE_PAGES_PER_FOLIO   UINT64_C(256)
#define D12_AGX_SPARSE_ELEMENTS_PER_FOLIO UINT64_C(512)
#define D12_AGX_SPARSE_ELEMENT_SIZE_B    UINT64_C(4)
#define D12_SPARSE_BUFFER_TILE_SIZE_B     UINT64_C(65536)

struct d12_sparse_registry_view {
   uint64_t records;
   uint32_t count;
   uint32_t capacity;
};

struct d12_sparse_registry_record {
   uint64_t base;
   uint64_t size;
   uint64_t residency;
};

_Static_assert(sizeof(struct d12_sparse_registry_view) == 16,
               "sparse registry view ABI");
_Static_assert(sizeof(struct d12_sparse_registry_record) == 24,
               "sparse registry record ABI");

/*
 * Standard tile shape, and the Tiled Resources Tier 2 packing guarantee.
 *
 * AGX's hardware tile is 16 KiB for every block size (layout.c
 * ail_get_max_tile_size), and D3D12's standard tile is 64 KiB
 * (D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES), so the standard tile is exactly
 * two hardware tiles on each axis.
 *
 * Tier 2 requires that "when the size of a texture mipmap level is an integer
 * multiple of the standard tile shape for its format, it is guaranteed to be
 * nonpacked".  AGX begins its mip tail at the first level smaller than one
 * HARDWARE tile on either axis, so any level that is a whole standard tile is
 * at least twice the packing threshold and cannot land in the tail.  That
 * argument only holds while the standard tile stays at least as large as the
 * hardware tile, which is what the predicate below pins down.
 */
#define D12_SPARSE_STANDARD_TILE_SCALE 2u
#define D12_SPARSE_HW_TILE_SIZE_B      UINT64_C(16384)

/* Mirrors ail_get_max_tile_size. False for a block size AGX does not tile. */
static inline bool
d12_sparse_hw_tile_el(uint32_t block_B, uint32_t *width_el,
                      uint32_t *height_el)
{
   uint32_t w, h;

   switch (block_B) {
   case 1:  w = 128; h = 128; break;
   case 2:  w = 128; h = 64;  break;
   case 4:  w = 64;  h = 64;  break;
   case 8:  w = 64;  h = 32;  break;
   case 16: w = 32;  h = 32;  break;
   case 32: w = 32;  h = 16;  break;
   case 64: w = 16;  h = 16;  break;
   default: return false;
   }

   if (width_el)
      *width_el = w;
   if (height_el)
      *height_el = h;
   return true;
}

static inline bool
d12_sparse_standard_tile_el(uint32_t block_B, uint32_t *width_el,
                            uint32_t *height_el)
{
   uint32_t w, h;

   if (!d12_sparse_hw_tile_el(block_B, &w, &h))
      return false;

   if (width_el)
      *width_el = w * D12_SPARSE_STANDARD_TILE_SCALE;
   if (height_el)
      *height_el = h * D12_SPARSE_STANDARD_TILE_SCALE;
   return true;
}

/*
 * True when a level whose element dimensions are an exact multiple of the
 * standard tile shape is guaranteed to clear AGX's packing threshold.  The
 * threshold is one hardware tile on each axis, and the loop that finds it
 * walks levels in decreasing size, so clearing it on this level means every
 * earlier level cleared it too and the mip tail starts later.
 */
static inline bool
d12_sparse_standard_tile_level_is_nonpacked(uint32_t block_B,
                                            uint32_t level_width_el,
                                            uint32_t level_height_el)
{
   uint32_t hw_w, hw_h, std_w, std_h;

   if (!d12_sparse_hw_tile_el(block_B, &hw_w, &hw_h) ||
       !d12_sparse_standard_tile_el(block_B, &std_w, &std_h))
      return false;
   if (!level_width_el || !level_height_el)
      return false;
   if (level_width_el % std_w || level_height_el % std_h)
      return false;

   return level_width_el >= hw_w && level_height_el >= hw_h;
}

/* Translate a page-aligned byte offset in an array image to its AGX sparse
 * page-table word. False means the offset or layout would address outside the
 * supplied image/table. The output is unchanged on failure. */
bool d12_sparse_residency_index(uint64_t resource_offset_B,
                                uint64_t layer_stride_B,
                                uint32_t layer_count,
                                uint32_t sparse_folios_per_layer,
                                uint64_t sparse_table_size_B,
                                uint32_t *index_el);

/* Translate a byte address produced by a UAV's PBE into the sparse word
 * relative to the view's prebiased map pointer. view_offset_B is relative to
 * the first view layer, while layer is the shader's array coordinate. */
bool d12_sparse_texture_access_index(uint64_t view_offset_B,
                                     uint64_t layer_stride_B,
                                     uint32_t layer,
                                     uint32_t view_layer_count,
                                     uint32_t sparse_folios_per_layer,
                                     uint64_t sparse_table_size_B,
                                     uint32_t *index_el);

bool d12_sparse_buffer_map_size(uint32_t tile_count, uint64_t alignment_B,
                                uint64_t *size_B);

/* Validate and 64 KiB-round one reserved buffer before any VA reservation or
 * uint32_t tile-count conversion. max_gpu_va_bits is the advertised
 * per-resource GPUVA width. */
bool d12_sparse_buffer_size(uint64_t width_B, uint32_t max_gpu_va_bits,
                            uint64_t *aligned_size_B,
                            uint32_t *tile_count);

bool d12_sparse_buffer_residency_index(uint64_t resource_offset_B,
                                       uint64_t resource_size_B,
                                       uint32_t tile_count,
                                       uint32_t *index_el);

/* Mirror the shader's at-most-two-word residency lookup for one buffer load.
 * view_phase_B is the view's byte position within its first 64 KiB tile. */
bool d12_sparse_buffer_access_tiles(uint32_t view_phase_B,
                                    uint64_t byte_offset_B,
                                    uint32_t access_size_B,
                                    uint64_t view_size_B,
                                    uint32_t map_word_count,
                                    uint32_t *first_index,
                                    uint32_t *last_index);

bool d12_sparse_registry_allocation_size(uint32_t capacity,
                                         uint64_t alignment_B,
                                         uint64_t *size_B);

/* Records must be sorted by base and non-overlapping. */
bool d12_sparse_registry_lookup(
   const struct d12_sparse_registry_record *records, uint32_t count,
   uint64_t address, uint32_t *index);

#ifdef __cplusplus
}
#endif

#endif
