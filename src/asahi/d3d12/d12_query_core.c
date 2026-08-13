/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_query_core.h"

bool
d12_query_core_heap_supported(uint32_t heap_type)
{
   switch (heap_type) {
   case D12_QUERY_CORE_HEAP_OCCLUSION:
   case D12_QUERY_CORE_HEAP_TIMESTAMP:
   case D12_QUERY_CORE_HEAP_PIPELINE_STATISTICS:
   case D12_QUERY_CORE_HEAP_SO_STATISTICS:
   case D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP:
      return true;
   default:
      return false;
   }
}

bool
d12_query_core_type_matches(uint32_t heap_type, uint32_t query_type)
{
   switch (heap_type) {
   case D12_QUERY_CORE_HEAP_OCCLUSION:
      return query_type == D12_QUERY_CORE_TYPE_OCCLUSION ||
             query_type == D12_QUERY_CORE_TYPE_BINARY_OCCLUSION;
   case D12_QUERY_CORE_HEAP_TIMESTAMP:
   case D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP:
      return query_type == D12_QUERY_CORE_TYPE_TIMESTAMP;
   case D12_QUERY_CORE_HEAP_PIPELINE_STATISTICS:
      return query_type == D12_QUERY_CORE_TYPE_PIPELINE_STATISTICS;
   case D12_QUERY_CORE_HEAP_SO_STATISTICS:
      return query_type >= D12_QUERY_CORE_TYPE_SO_STATISTICS_STREAM0 &&
             query_type <= D12_QUERY_CORE_TYPE_SO_STATISTICS_STREAM3;
   default:
      return false;
   }
}

bool
d12_query_core_is_timestamp_heap(uint32_t heap_type)
{
   return heap_type == D12_QUERY_CORE_HEAP_TIMESTAMP ||
          heap_type == D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP;
}

bool
d12_query_core_timestamp_command_compatible(uint32_t heap_type,
                                            uint32_t command_type)
{
   if (heap_type == D12_QUERY_CORE_HEAP_COPY_QUEUE_TIMESTAMP)
      return command_type == D12_QUERY_CORE_COMMAND_COPY;
   if (heap_type == D12_QUERY_CORE_HEAP_TIMESTAMP)
      return command_type == D12_QUERY_CORE_COMMAND_DIRECT ||
             command_type == D12_QUERY_CORE_COMMAND_COMPUTE;
   return true;
}
