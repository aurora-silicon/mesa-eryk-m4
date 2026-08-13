/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_query_core.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                     \
   do {                                                                      \
      if (!(condition)) {                                                    \
         fprintf(stderr, "d12_query_core_tests: failed at line %d\n",        \
                 __LINE__);                                                  \
         exit(1);                                                            \
      }                                                                      \
   } while (0)

int
main(void)
{
   CHECK(d12_query_core_heap_supported(D12_QUERY_CORE_HEAP_OCCLUSION));
   CHECK(d12_query_core_heap_supported(D12_QUERY_CORE_HEAP_TIMESTAMP));
   CHECK(d12_query_core_heap_supported(
      D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP));
   CHECK(!d12_query_core_heap_supported(4));
   CHECK(!d12_query_core_heap_supported(6));

   CHECK(d12_query_core_type_matches(D12_QUERY_CORE_HEAP_TIMESTAMP,
                                     D12_QUERY_CORE_TYPE_TIMESTAMP));
   CHECK(d12_query_core_type_matches(
      D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP,
      D12_QUERY_CORE_TYPE_TIMESTAMP));
   CHECK(!d12_query_core_type_matches(
      D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP,
      D12_QUERY_CORE_TYPE_OCCLUSION));
   CHECK(d12_query_core_type_matches(
      D12_QUERY_CORE_HEAP_SO_STATISTICS,
      D12_QUERY_CORE_TYPE_SO_STATISTICS_STREAM3));

   CHECK(d12_query_core_is_timestamp_heap(D12_QUERY_CORE_HEAP_TIMESTAMP));
   CHECK(d12_query_core_is_timestamp_heap(
      D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP));
   CHECK(!d12_query_core_is_timestamp_heap(D12_QUERY_CORE_HEAP_OCCLUSION));

   CHECK(d12_query_core_timestamp_command_compatible(
      D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP,
      D12_QUERY_CORE_COMMAND_COPY));
   CHECK(!d12_query_core_timestamp_command_compatible(
      D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP,
      D12_QUERY_CORE_COMMAND_DIRECT));
   CHECK(d12_query_core_timestamp_command_compatible(
      D12_QUERY_CORE_HEAP_TIMESTAMP, D12_QUERY_CORE_COMMAND_DIRECT));
   CHECK(d12_query_core_timestamp_command_compatible(
      D12_QUERY_CORE_HEAP_TIMESTAMP, D12_QUERY_CORE_COMMAND_COMPUTE));
   CHECK(!d12_query_core_timestamp_command_compatible(
      D12_QUERY_CORE_HEAP_TIMESTAMP, D12_QUERY_CORE_COMMAND_COPY));

   puts("d12_query_core_tests: PASS");
   return 0;
}
