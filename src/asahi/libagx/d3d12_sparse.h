/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBAGX_D3D12_SPARSE_H
#define LIBAGX_D3D12_SPARSE_H

/* Keep these layouts in lockstep with d12_sparse_registry_view/record.
 * Records are dense and sorted by base. */
struct libagx_d3d12_sparse_registry_view {
   uint64_t records;
   uint32_t count;
   uint32_t capacity;
};

struct libagx_d3d12_sparse_registry_record {
   uint64_t base;
   uint64_t size;
   uint64_t residency;
};

typedef char libagx_d3d12_sparse_registry_view_must_be_16_bytes
   [sizeof(struct libagx_d3d12_sparse_registry_view) == 16 ? 1 : -1];
typedef char libagx_d3d12_sparse_registry_record_must_be_24_bytes
   [sizeof(struct libagx_d3d12_sparse_registry_record) == 24 ? 1 : -1];

static inline uint64_t
libagx_d3d12_sparse_read_address(uint64_t address,
                                 constant struct
                                    libagx_d3d12_sparse_registry_view *view,
                                 uint64_t sparse_ro_offset,
                                 uint64_t *residency)
{
   *residency = 0;
   if (!address || !view)
      return address;

   constant struct libagx_d3d12_sparse_registry_record *records =
      (constant struct libagx_d3d12_sparse_registry_record *)view->records;
   const uint32_t count = min(view->count, view->capacity);
   if (!records || !count)
      return address;

   uint32_t low = 0;
   uint32_t high = count;
   while (low < high) {
      const uint32_t middle = low + (high - low) / 2;
      const struct libagx_d3d12_sparse_registry_record record =
         records[middle];

      if (address < record.base) {
         high = middle;
      } else if (record.base && address - record.base < record.size) {
         if (record.residency)
            *residency = record.residency +
                         ((address - record.base) >> 16) * sizeof(uint32_t);
         return address + sparse_ro_offset;
      } else {
         low = middle + 1;
      }
   }

   return address;
}

#endif
