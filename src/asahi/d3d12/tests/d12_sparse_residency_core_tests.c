/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_sparse_residency_core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;

static void
require_result(bool condition, const char *message)
{
   ++checks;
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

static void
require_index(uint64_t offset_B, uint64_t layer_stride_B,
              uint32_t layer_count, uint32_t folios_per_layer,
              uint64_t table_size_B, uint32_t expected,
              const char *message)
{
   uint32_t index = UINT32_MAX;
   require_result(d12_sparse_residency_index(
                     offset_B, layer_stride_B, layer_count,
                     folios_per_layer, table_size_B, &index) &&
                     index == expected,
                  message);
}

int
main(void)
{
   const uint64_t page = D12_AGX_SPARSE_PAGE_SIZE_B;
   const uint64_t layer_stride = 600 * page;
   const uint32_t layers = 3;
   const uint32_t folios = 3;
   const uint64_t table_size =
      (uint64_t)layers * folios *
      D12_AGX_SPARSE_ELEMENTS_PER_FOLIO *
      D12_AGX_SPARSE_ELEMENT_SIZE_B;

   require_index(0, layer_stride, layers, folios, table_size, 0,
                 "first page uses first sparse word");
   require_index(255 * page, layer_stride, layers, folios, table_size, 255,
                 "last page in first folio is contiguous");
   require_index(256 * page, layer_stride, layers, folios, table_size, 512,
                 "second folio skips its hardware-reserved half");
   require_index(599 * page, layer_stride, layers, folios, table_size, 1111,
                 "last page in a padded layer maps inside the third folio");
   require_index(layer_stride, layer_stride, layers, folios, table_size,
                 1536, "array layer advances by complete folios");
   require_index(2 * layer_stride + 256 * page, layer_stride, layers, folios,
                 table_size, 3584,
                 "folio arithmetic composes with the array layer");

   uint32_t sentinel = 0x12345678;
   require_result(!d12_sparse_residency_index(
                     1, layer_stride, layers, folios, table_size, &sentinel) &&
                     sentinel == 0x12345678,
                  "unaligned resource offset fails transactionally");
   require_result(!d12_sparse_residency_index(
                     0, layer_stride + 1, layers, folios, table_size,
                     &sentinel),
                  "unaligned layer stride is rejected");
   require_result(!d12_sparse_residency_index(
                     3 * layer_stride, layer_stride, layers, folios,
                     table_size, &sentinel),
                  "offset beyond the final layer is rejected");
   require_result(!d12_sparse_residency_index(
                     512 * page, layer_stride, layers, 2, table_size,
                     &sentinel),
                  "page beyond the declared folios is rejected");
   require_result(!d12_sparse_residency_index(
                     0, layer_stride, layers, folios,
                     D12_AGX_SPARSE_ELEMENT_SIZE_B - 1, &sentinel),
                  "mis-sized sparse table is rejected");
   require_result(!d12_sparse_residency_index(
                     0, layer_stride, layers, folios, 0, &sentinel),
                  "empty sparse table is rejected");
   require_result(!d12_sparse_residency_index(
                     0, layer_stride, layers, folios, table_size, NULL),
                  "null output is rejected");
   require_result(!d12_sparse_residency_index(
                     0, 0, layers, folios, table_size, &sentinel),
                  "zero layer stride is rejected");
   require_result(!d12_sparse_residency_index(
                     0, layer_stride, 0, folios, table_size, &sentinel),
                  "zero layer count is rejected");
   require_result(!d12_sparse_residency_index(
                     0, layer_stride, layers, 0, table_size, &sentinel),
                  "zero folio count is rejected");

   uint32_t texture_index = UINT32_MAX;
   require_result(d12_sparse_texture_access_index(
                     255 * page + 37, layer_stride, 0, layers, folios,
                     table_size, &texture_index) && texture_index == 255,
                  "texture byte address rounds down within its sparse page");
   require_result(d12_sparse_texture_access_index(
                     256 * page + 91, layer_stride, 0, layers, folios,
                     table_size, &texture_index) && texture_index == 512,
                  "texture byte address preserves the hardware folio gap");
   require_result(d12_sparse_texture_access_index(
                     layer_stride + 256 * page + 8, layer_stride, 1,
                     layers, folios, table_size, &texture_index) &&
                     texture_index == 2048,
                  "texture array layer composes with its mip byte offset");
   require_result(!d12_sparse_texture_access_index(
                     layer_stride - 1, layer_stride, 1, layers, folios,
                     table_size, &texture_index),
                  "texture address before its declared layer is rejected");
   require_result(!d12_sparse_texture_access_index(
                     layer_stride, layer_stride, layers, layers, folios,
                     table_size, &texture_index),
                  "texture layer outside its UAV view is rejected");
   require_result(!d12_sparse_texture_access_index(
                     2 * layer_stride, layer_stride, 1, layers, folios,
                     table_size, &texture_index),
                  "texture address outside the selected layer is rejected");

   uint64_t map_size = 0;
   require_result(d12_sparse_buffer_map_size(1, page, &map_size) &&
                     map_size == page,
                  "one buffer tile gets one aligned map page");
   require_result(d12_sparse_buffer_map_size(4097, page, &map_size) &&
                     map_size == 2 * page,
                  "buffer map size rounds up without truncation");
   require_result(!d12_sparse_buffer_map_size(0, page, &map_size),
                  "empty buffer map is rejected");
   require_result(!d12_sparse_buffer_map_size(1, page - 1, &map_size),
                  "non-power-of-two map alignment is rejected");

   uint64_t sparse_size = 0;
   uint32_t sparse_tiles = 0;
   require_result(d12_sparse_buffer_size(
                     1, 39, &sparse_size, &sparse_tiles) &&
                     sparse_size == D12_SPARSE_BUFFER_TILE_SIZE_B &&
                     sparse_tiles == 1,
                  "one-byte reserved buffer rounds to one tile");
   require_result(d12_sparse_buffer_size(
                     (UINT64_C(1) << 39), 39, &sparse_size,
                     &sparse_tiles) &&
                     sparse_size == (UINT64_C(1) << 39) &&
                     sparse_tiles == (UINT32_C(1) << 23),
                  "advertised 39-bit resource boundary is representable");
   require_result(!d12_sparse_buffer_size(
                     (UINT64_C(1) << 39) + 1, 39, &sparse_size,
                     &sparse_tiles),
                  "reserved buffer beyond advertised GPUVA width is rejected");
   require_result(!d12_sparse_buffer_size(
                     UINT64_MAX, 63, &sparse_size, &sparse_tiles),
                  "reserved buffer alignment overflow is rejected");
   require_result(!d12_sparse_buffer_size(
                     1, 64, &sparse_size, &sparse_tiles),
                  "unsupported full-width GPUVA limit is rejected");
   require_result(!d12_sparse_buffer_size(
                     1, 39, NULL, &sparse_tiles),
                  "reserved buffer size requires both outputs");

   uint32_t buffer_index = UINT32_MAX;
   require_result(d12_sparse_buffer_residency_index(
                     0, 3 * D12_SPARSE_BUFFER_TILE_SIZE_B, 3,
                     &buffer_index) && buffer_index == 0,
                  "buffer first byte selects first word");
   require_result(d12_sparse_buffer_residency_index(
                     D12_SPARSE_BUFFER_TILE_SIZE_B,
                     3 * D12_SPARSE_BUFFER_TILE_SIZE_B, 3,
                     &buffer_index) && buffer_index == 1,
                  "buffer tile boundary selects next word");
   require_result(!d12_sparse_buffer_residency_index(
                     3 * D12_SPARSE_BUFFER_TILE_SIZE_B,
                     3 * D12_SPARSE_BUFFER_TILE_SIZE_B, 3, &buffer_index),
                  "buffer end offset is out of range");

   uint32_t first = UINT32_MAX, last = UINT32_MAX;
   require_result(d12_sparse_buffer_access_tiles(
                     0, 4, 16, D12_SPARSE_BUFFER_TILE_SIZE_B, 1,
                     &first, &last) && first == 0 && last == 0,
                  "ordinary buffer load reads one residency word");
   require_result(d12_sparse_buffer_access_tiles(
                     D12_SPARSE_BUFFER_TILE_SIZE_B - 8, 0, 16,
                     2 * D12_SPARSE_BUFFER_TILE_SIZE_B, 2,
                     &first, &last) && first == 0 && last == 1,
                  "straddling buffer load checks both tiles");
   require_result(d12_sparse_buffer_access_tiles(
                     D12_SPARSE_BUFFER_TILE_SIZE_B - 8, 0, 16, 8, 1,
                     &first, &last) && first == 0 && last == 0,
                  "view end clamps the residency range");
   require_result(!d12_sparse_buffer_access_tiles(
                     0, 64, 4, 64, 1, &first, &last),
                  "out-of-view buffer load performs no map lookup");
   require_result(!d12_sparse_buffer_access_tiles(
                     D12_SPARSE_BUFFER_TILE_SIZE_B, 0, 4, 4, 1,
                     &first, &last),
                  "invalid view phase is rejected");

   uint64_t registry_size = 0;
   require_result(d12_sparse_registry_allocation_size(
                     1, page, &registry_size) && registry_size == page,
                  "one registry record gets one aligned page");
   require_result(d12_sparse_registry_allocation_size(
                     4097, page, &registry_size) &&
                     registry_size == 7 * page,
                  "registry growth beyond 4096 records stays representable");
   require_result(!d12_sparse_registry_allocation_size(
                     0, page, &registry_size),
                  "empty registry allocation is rejected");
   require_result(!d12_sparse_registry_allocation_size(
                     1, page - 1, &registry_size),
                  "registry alignment must be a power of two");

   const struct d12_sparse_registry_record registry[] = {
      {.base = UINT64_C(0x10000),
       .size = UINT64_C(0x20000),
       .residency = UINT64_C(0x90000)},
      {.base = UINT64_C(0x40000),
       .size = UINT64_C(0x10000),
       .residency = UINT64_C(0xa0000)},
      {.base = UINT64_C(0x80000),
       .size = UINT64_C(0x30000),
       .residency = UINT64_C(0xb0000)},
   };
   uint32_t registry_index = UINT32_MAX;
   require_result(d12_sparse_registry_lookup(
                     registry, 3, UINT64_C(0x10000), &registry_index) &&
                     registry_index == 0,
                  "registry lookup includes the first base");
   require_result(d12_sparse_registry_lookup(
                     registry, 3, UINT64_C(0x2ffff), &registry_index) &&
                     registry_index == 0,
                  "registry lookup includes the final byte");
   require_result(d12_sparse_registry_lookup(
                     registry, 3, UINT64_C(0x40020), &registry_index) &&
                     registry_index == 1,
                  "registry lookup finds an interior record");
   require_result(d12_sparse_registry_lookup(
                     registry, 3, UINT64_C(0xaffff), &registry_index) &&
                     registry_index == 2,
                  "registry lookup finds the final record boundary");
   require_result(!d12_sparse_registry_lookup(
                     registry, 3, UINT64_C(0x30000), &registry_index),
                  "registry lookup rejects a gap");
   require_result(!d12_sparse_registry_lookup(
                     registry, 3, UINT64_C(0xb0000), &registry_index),
                  "registry lookup excludes the final end address");

   const struct d12_sparse_registry_record overlapping[] = {
      {.base = UINT64_C(0x10000), .size = UINT64_C(0x20000)},
      {.base = UINT64_C(0x20000), .size = UINT64_C(0x10000)},
   };
   require_result(!d12_sparse_registry_lookup(
                     overlapping, 2, UINT64_C(0x20000), &registry_index),
                  "overlapping registry records are rejected");

   const struct d12_sparse_registry_record overflowing[] = {
      {.base = UINT64_MAX - 3, .size = 8},
   };
   require_result(!d12_sparse_registry_lookup(
                     overflowing, 1, UINT64_MAX - 1, &registry_index),
                  "overflowing registry records are rejected");
   require_result(!d12_sparse_registry_lookup(
                     registry, 0, UINT64_C(0x10000), &registry_index),
                  "empty registry lookup is rejected");
   require_result(sizeof(struct d12_sparse_registry_view) == 16 &&
                     sizeof(struct d12_sparse_registry_record) == 24,
                  "registry view and record widths match the shader ABI");

   puts("d12_sparse_residency_core_tests: PASS");
   /* Tiled Resources Tier 2: a level that is an integer multiple of the standard
    * tile shape must never be packed into the mip tail. */
   {
      static const uint32_t block_sizes[] = {1, 2, 4, 8, 16, 32, 64};

      for (unsigned i = 0; i < sizeof(block_sizes) / sizeof(block_sizes[0]);
           i++) {
         const uint32_t block_B = block_sizes[i];
         uint32_t hw_w = 0, hw_h = 0, std_w = 0, std_h = 0;

         require_result(d12_sparse_hw_tile_el(block_B, &hw_w, &hw_h),
                        "every AGX block size has a hardware tile");
         require_result(d12_sparse_standard_tile_el(block_B, &std_w, &std_h),
                        "every AGX block size has a standard tile");

         /* Every AGX hardware tile is 16 KiB and the standard tile is the
          * 64 KiB D3D12 tile, which is what makes the argument work. */
         require_result((uint64_t)hw_w * hw_h * block_B == D12_SPARSE_HW_TILE_SIZE_B,
                        "the AGX hardware tile is 16 KiB");
         require_result((uint64_t)std_w * std_h * block_B ==
               D12_SPARSE_HW_TILE_SIZE_B * D12_SPARSE_STANDARD_TILE_SCALE *
                  D12_SPARSE_STANDARD_TILE_SCALE,
                        "the standard tile is the 64 KiB D3D12 tile");
         require_result(std_w == hw_w * D12_SPARSE_STANDARD_TILE_SCALE,
                        "the standard tile is two hardware tiles wide");
         require_result(std_h == hw_h * D12_SPARSE_STANDARD_TILE_SCALE,
                        "the standard tile is two hardware tiles tall");

         /* Any whole number of standard tiles clears the packing threshold. */
         for (uint32_t tiles = 1; tiles <= 8; tiles++) {
            require_result(d12_sparse_standard_tile_level_is_nonpacked(
               block_B, std_w * tiles, std_h * tiles),
                        "a square multiple of the standard tile is nonpacked");
            require_result(d12_sparse_standard_tile_level_is_nonpacked(
               block_B, std_w, std_h * tiles),
                        "a tall multiple of the standard tile is nonpacked");
            require_result(d12_sparse_standard_tile_level_is_nonpacked(
               block_B, std_w * tiles, std_h),
                        "a wide multiple of the standard tile is nonpacked");
         }

         /* A level that is not a whole standard tile carries no guarantee,
          * and the guarantee is not claimed for one. */
         require_result(!d12_sparse_standard_tile_level_is_nonpacked(block_B,
                                                            std_w - 1, std_h),
                        "a partial-width level carries no guarantee");
         require_result(!d12_sparse_standard_tile_level_is_nonpacked(block_B, std_w,
                                                            std_h - 1),
                        "a partial-height level carries no guarantee");
         require_result(!d12_sparse_standard_tile_level_is_nonpacked(block_B, 0, std_h),
                        "an empty level carries no guarantee");
      }

      require_result(!d12_sparse_hw_tile_el(3, NULL, NULL),
                        "a non-power-of-two block size has no tile");
      require_result(!d12_sparse_hw_tile_el(128, NULL, NULL),
                        "an oversized block size has no tile");
      require_result(!d12_sparse_standard_tile_el(0, NULL, NULL),
                        "a zero block size has no standard tile");
   }

   printf("d12_sparse_residency_core_tests: %u checks passed\n", checks);
   return 0;
}
