/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_umd_pipeline_library.h"
#include "../d12_umd_pipeline_serialization.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
make_pipeline(uint8_t tag, void **out_blob, size_t *out_size)
{
   const uint8_t shader[] = {0x44, 0x58, 0x42, 0x43, tag};
   const struct d12_frontend_compute_pipeline_desc desc = {
      .shader = {.data = shader, .size = sizeof(shader)},
   };
   assert(d12_umd_pipeline_serialize_compute(&desc, NULL, out_blob,
                                             out_size));
}

static void
test_round_trip(void)
{
   struct d12_umd_pipeline_library_core source;
   struct d12_umd_pipeline_library_core decoded;
   struct d12_umd_pipeline_library_entry *entry = NULL;
   void *first = NULL;
   void *second = NULL;
   void *serialized = NULL;
   void *serialized_again = NULL;
   size_t first_size = 0;
   size_t second_size = 0;
   size_t serialized_size = 0;
   size_t serialized_again_size = 0;

   make_pipeline(1, &first, &first_size);
   make_pipeline(2, &second, &second_size);
   d12_umd_pipeline_library_init(&source);
   d12_umd_pipeline_library_init(&decoded);

   assert(d12_umd_pipeline_library_add(&source, 9, first, first_size,
                                       &entry) ==
          D12_UMD_PIPELINE_LIBRARY_OK);
   assert(entry && entry->index == 9);
   entry->backend = (void *)(uintptr_t)0x1234;
   assert(d12_umd_pipeline_library_add(&source, 2, second, second_size,
                                       NULL) ==
          D12_UMD_PIPELINE_LIBRARY_OK);
   assert(source.count == 2 && source.entries[0].index == 2 &&
          source.entries[1].index == 9);
   assert(d12_umd_pipeline_library_add(&source, 9, first, first_size,
                                       NULL) ==
          D12_UMD_PIPELINE_LIBRARY_DUPLICATE);
   assert(d12_umd_pipeline_library_find(&source, 9)->backend ==
          (void *)(uintptr_t)0x1234);
   assert(!d12_umd_pipeline_library_find(&source, 8));

   assert(d12_umd_pipeline_library_serialized_size(&source,
                                                    &serialized_size));
   serialized = malloc(serialized_size);
   assert(serialized);
   assert(d12_umd_pipeline_library_serialize(&source, serialized,
                                              serialized_size) ==
          D12_UMD_PIPELINE_LIBRARY_OK);
   assert(d12_umd_pipeline_library_import(&decoded, serialized,
                                           serialized_size) ==
          D12_UMD_PIPELINE_LIBRARY_OK);
   assert(decoded.count == 2);
   assert(!d12_umd_pipeline_library_find(&decoded, 9)->backend);
   assert(d12_umd_pipeline_library_find(&decoded, 2)->blob_size ==
          second_size);
   assert(!memcmp(d12_umd_pipeline_library_find(&decoded, 2)->blob,
                  second, second_size));

   assert(d12_umd_pipeline_library_serialized_size(
      &decoded, &serialized_again_size));
   assert(serialized_again_size == serialized_size);
   serialized_again = malloc(serialized_again_size);
   assert(serialized_again);
   assert(d12_umd_pipeline_library_serialize(
             &decoded, serialized_again, serialized_again_size) ==
          D12_UMD_PIPELINE_LIBRARY_OK);
   assert(!memcmp(serialized, serialized_again, serialized_size));

   ((uint8_t *)serialized)[serialized_size - 1] ^= 0x40;
   {
      struct d12_umd_pipeline_library_core corrupt;
      d12_umd_pipeline_library_init(&corrupt);
      assert(d12_umd_pipeline_library_import(&corrupt, serialized,
                                              serialized_size) ==
             D12_UMD_PIPELINE_LIBRARY_INVALID);
      assert(!corrupt.entries && !corrupt.count);
   }

   free(serialized_again);
   free(serialized);
   free(second);
   free(first);
   d12_umd_pipeline_library_cleanup(&decoded);
   d12_umd_pipeline_library_cleanup(&source);
}

static void
test_empty_library(void)
{
   struct d12_umd_pipeline_library_core source;
   struct d12_umd_pipeline_library_core decoded;
   size_t size = 0;
   void *blob;

   d12_umd_pipeline_library_init(&source);
   d12_umd_pipeline_library_init(&decoded);
   assert(d12_umd_pipeline_library_serialized_size(&source, &size));
   assert(size == 40);
   blob = malloc(size);
   assert(blob);
   assert(d12_umd_pipeline_library_serialize(&source, blob, size) ==
          D12_UMD_PIPELINE_LIBRARY_OK);
   assert(d12_umd_pipeline_library_import(&decoded, blob, size) ==
          D12_UMD_PIPELINE_LIBRARY_OK);
   assert(!decoded.count);
   free(blob);
   d12_umd_pipeline_library_cleanup(&decoded);
   d12_umd_pipeline_library_cleanup(&source);
}

int
main(void)
{
   test_round_trip();
   test_empty_library();
   puts("d12_umd_pipeline_library_tests: PASS");
   return 0;
}

