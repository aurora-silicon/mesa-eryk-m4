/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_pipeline_library.h"

#include "d12_umd_pipeline_serialization.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t d12_umd_pipeline_library_magic[8] = {
   'D', '1', '2', 'A', 'G', 'X', 'L', 'B',
};

struct d12_umd_pipeline_library_blob_header {
   uint8_t magic[8];
   uint32_t version;
   uint32_t header_size;
   uint64_t total_size;
   uint64_t hash;
   uint32_t entry_count;
   uint32_t reserved;
};

struct d12_umd_pipeline_library_blob_entry {
   uint32_t index;
   uint32_t reserved;
   uint64_t blob_size;
   uint64_t blob_hash;
};

_Static_assert(sizeof(struct d12_umd_pipeline_library_blob_header) == 40,
               "pipeline-library header width changed");
_Static_assert(sizeof(struct d12_umd_pipeline_library_blob_entry) == 24,
               "pipeline-library entry width changed");

static uint64_t
d12_umd_pipeline_library_hash(const void *data, size_t size,
                              size_t zero_start, size_t zero_size)
{
   const uint8_t *bytes = data;
   uint64_t hash = UINT64_C(14695981039346656037);

   for (size_t index = 0; index < size; ++index) {
      const uint8_t value = index >= zero_start &&
                                  index - zero_start < zero_size
                               ? 0
                               : bytes[index];
      hash ^= value;
      hash *= UINT64_C(1099511628211);
   }
   return hash;
}

static uint64_t
d12_umd_pipeline_library_blob_hash(const void *data, size_t size)
{
   return d12_umd_pipeline_library_hash(data, size, SIZE_MAX, 0);
}

void
d12_umd_pipeline_library_init(struct d12_umd_pipeline_library_core *library)
{
   if (library)
      memset(library, 0, sizeof(*library));
}

void
d12_umd_pipeline_library_cleanup(
   struct d12_umd_pipeline_library_core *library)
{
   if (!library)
      return;
   for (uint32_t index = 0; index < library->count; ++index)
      free(library->entries[index].blob);
   free(library->entries);
   memset(library, 0, sizeof(*library));
}

static uint32_t
d12_umd_pipeline_library_lower_bound(
   const struct d12_umd_pipeline_library_core *library, uint32_t index)
{
   uint32_t low = 0;
   uint32_t high = library ? library->count : 0;

   while (low < high) {
      const uint32_t middle = low + (high - low) / 2;
      if (library->entries[middle].index < index)
         low = middle + 1;
      else
         high = middle;
   }
   return low;
}

struct d12_umd_pipeline_library_entry *
d12_umd_pipeline_library_find(struct d12_umd_pipeline_library_core *library,
                              uint32_t index)
{
   const uint32_t position =
      d12_umd_pipeline_library_lower_bound(library, index);
   return library && position < library->count &&
                 library->entries[position].index == index
             ? &library->entries[position]
             : NULL;
}

const struct d12_umd_pipeline_library_entry *
d12_umd_pipeline_library_find_const(
   const struct d12_umd_pipeline_library_core *library, uint32_t index)
{
   const uint32_t position =
      d12_umd_pipeline_library_lower_bound(library, index);
   return library && position < library->count &&
                 library->entries[position].index == index
             ? &library->entries[position]
             : NULL;
}

enum d12_umd_pipeline_library_result
d12_umd_pipeline_library_add(
   struct d12_umd_pipeline_library_core *library, uint32_t index,
   const void *blob, size_t blob_size,
   struct d12_umd_pipeline_library_entry **out_entry)
{
   uint32_t position;
   void *copy;

   if (out_entry)
      *out_entry = NULL;
   if (!library || !blob || !blob_size ||
       blob_size > D12_UMD_PIPELINE_LIBRARY_MAX_BYTES ||
       !d12_umd_pipeline_serialization_valid(blob, blob_size))
      return D12_UMD_PIPELINE_LIBRARY_INVALID;
   if (library->count >= D12_UMD_PIPELINE_LIBRARY_MAX_ENTRIES)
      return D12_UMD_PIPELINE_LIBRARY_OUT_OF_MEMORY;

   position = d12_umd_pipeline_library_lower_bound(library, index);
   if (position < library->count &&
       library->entries[position].index == index)
      return D12_UMD_PIPELINE_LIBRARY_DUPLICATE;

   copy = malloc(blob_size);
   if (!copy)
      return D12_UMD_PIPELINE_LIBRARY_OUT_OF_MEMORY;
   memcpy(copy, blob, blob_size);

   if (library->count == library->capacity) {
      uint32_t capacity = library->capacity ? library->capacity * 2u : 8u;
      void *entries;
      if (capacity < library->capacity ||
          capacity > D12_UMD_PIPELINE_LIBRARY_MAX_ENTRIES)
         capacity = D12_UMD_PIPELINE_LIBRARY_MAX_ENTRIES;
      entries = realloc(library->entries,
                        (size_t)capacity * sizeof(library->entries[0]));
      if (!entries) {
         free(copy);
         return D12_UMD_PIPELINE_LIBRARY_OUT_OF_MEMORY;
      }
      library->entries = entries;
      library->capacity = capacity;
   }

   memmove(&library->entries[position + 1], &library->entries[position],
           (size_t)(library->count - position) * sizeof(library->entries[0]));
   library->entries[position] = (struct d12_umd_pipeline_library_entry){
      .index = index,
      .blob = copy,
      .blob_size = blob_size,
   };
   ++library->count;
   if (out_entry)
      *out_entry = &library->entries[position];
   return D12_UMD_PIPELINE_LIBRARY_OK;
}

bool
d12_umd_pipeline_library_serialized_size(
   const struct d12_umd_pipeline_library_core *library, size_t *out_size)
{
   size_t size = sizeof(struct d12_umd_pipeline_library_blob_header);

   if (!library || !out_size ||
       library->count > D12_UMD_PIPELINE_LIBRARY_MAX_ENTRIES)
      return false;
   for (uint32_t index = 0; index < library->count; ++index) {
      const size_t blob_size = library->entries[index].blob_size;
      if (!library->entries[index].blob || !blob_size ||
          blob_size > D12_UMD_PIPELINE_LIBRARY_MAX_BYTES ||
          size > D12_UMD_PIPELINE_LIBRARY_MAX_BYTES -
                    sizeof(struct d12_umd_pipeline_library_blob_entry) ||
          blob_size > D12_UMD_PIPELINE_LIBRARY_MAX_BYTES - size -
                         sizeof(struct d12_umd_pipeline_library_blob_entry))
         return false;
      size += sizeof(struct d12_umd_pipeline_library_blob_entry) + blob_size;
   }
   *out_size = size;
   return true;
}

enum d12_umd_pipeline_library_result
d12_umd_pipeline_library_serialize(
   const struct d12_umd_pipeline_library_core *library,
   void *data, size_t size)
{
   struct d12_umd_pipeline_library_blob_header header = {0};
   size_t required;
   uint8_t *cursor = data;

   if (!data || !d12_umd_pipeline_library_serialized_size(library,
                                                           &required) ||
       size != required)
      return D12_UMD_PIPELINE_LIBRARY_INVALID;

   memcpy(header.magic, d12_umd_pipeline_library_magic, sizeof(header.magic));
   header.version = D12_UMD_PIPELINE_LIBRARY_VERSION;
   header.header_size = sizeof(header);
   header.total_size = required;
   header.entry_count = library->count;
   memcpy(cursor, &header, sizeof(header));
   cursor += sizeof(header);

   for (uint32_t index = 0; index < library->count; ++index) {
      const struct d12_umd_pipeline_library_entry *source =
         &library->entries[index];
      const struct d12_umd_pipeline_library_blob_entry entry = {
         .index = source->index,
         .blob_size = source->blob_size,
         .blob_hash = d12_umd_pipeline_library_blob_hash(source->blob,
                                                         source->blob_size),
      };
      memcpy(cursor, &entry, sizeof(entry));
      cursor += sizeof(entry);
      memcpy(cursor, source->blob, source->blob_size);
      cursor += source->blob_size;
   }

   ((struct d12_umd_pipeline_library_blob_header *)data)->hash =
      d12_umd_pipeline_library_hash(
         data, required,
         offsetof(struct d12_umd_pipeline_library_blob_header, hash),
         sizeof(header.hash));
   return D12_UMD_PIPELINE_LIBRARY_OK;
}

enum d12_umd_pipeline_library_result
d12_umd_pipeline_library_import(
   struct d12_umd_pipeline_library_core *library,
   const void *data, size_t size)
{
   struct d12_umd_pipeline_library_blob_header header;
   const uint8_t *cursor;
   const uint8_t *end;
   struct d12_umd_pipeline_library_core imported;
   uint32_t previous_index = 0;

   if (!library || library->count || library->entries || !data ||
       size < sizeof(header) || size > D12_UMD_PIPELINE_LIBRARY_MAX_BYTES)
      return D12_UMD_PIPELINE_LIBRARY_INVALID;
   memcpy(&header, data, sizeof(header));
   if (memcmp(header.magic, d12_umd_pipeline_library_magic,
              sizeof(header.magic)) ||
       header.version != D12_UMD_PIPELINE_LIBRARY_VERSION ||
       header.header_size != sizeof(header) || header.total_size != size ||
       header.reserved ||
       header.entry_count > D12_UMD_PIPELINE_LIBRARY_MAX_ENTRIES ||
       header.hash != d12_umd_pipeline_library_hash(
                         data, size,
                         offsetof(struct d12_umd_pipeline_library_blob_header,
                                  hash),
                         sizeof(header.hash)))
      return D12_UMD_PIPELINE_LIBRARY_INVALID;

   d12_umd_pipeline_library_init(&imported);
   cursor = (const uint8_t *)data + sizeof(header);
   end = (const uint8_t *)data + size;
   for (uint32_t ordinal = 0; ordinal < header.entry_count; ++ordinal) {
      struct d12_umd_pipeline_library_blob_entry entry;
      enum d12_umd_pipeline_library_result result;

      if ((size_t)(end - cursor) < sizeof(entry))
         goto invalid;
      memcpy(&entry, cursor, sizeof(entry));
      cursor += sizeof(entry);
      if (entry.reserved || !entry.blob_size ||
          entry.blob_size > (uint64_t)(end - cursor) ||
          entry.blob_size > SIZE_MAX ||
          (ordinal && entry.index <= previous_index) ||
          entry.blob_hash != d12_umd_pipeline_library_blob_hash(
                                cursor, (size_t)entry.blob_size))
         goto invalid;
      result = d12_umd_pipeline_library_add(
         &imported, entry.index, cursor, (size_t)entry.blob_size, NULL);
      if (result != D12_UMD_PIPELINE_LIBRARY_OK) {
         d12_umd_pipeline_library_cleanup(&imported);
         return result == D12_UMD_PIPELINE_LIBRARY_OUT_OF_MEMORY
                   ? result
                   : D12_UMD_PIPELINE_LIBRARY_INVALID;
      }
      previous_index = entry.index;
      cursor += entry.blob_size;
   }
   if (cursor != end)
      goto invalid;

   *library = imported;
   return D12_UMD_PIPELINE_LIBRARY_OK;

invalid:
   d12_umd_pipeline_library_cleanup(&imported);
   return D12_UMD_PIPELINE_LIBRARY_INVALID;
}

