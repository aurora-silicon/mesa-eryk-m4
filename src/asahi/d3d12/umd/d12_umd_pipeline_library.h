/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Pointer-free D3D12 DDI pipeline-library storage.  The Windows-facing layer
 * supplies synchronization and owns live renderer objects; this core owns
 * only validated canonical PSO snapshots and is host-unit-testable.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D12_UMD_PIPELINE_LIBRARY_VERSION 1u
#define D12_UMD_PIPELINE_LIBRARY_MAX_ENTRIES 65536u
#define D12_UMD_PIPELINE_LIBRARY_MAX_BYTES (512u * 1024u * 1024u)

enum d12_umd_pipeline_library_result {
   D12_UMD_PIPELINE_LIBRARY_OK = 0,
   D12_UMD_PIPELINE_LIBRARY_INVALID,
   D12_UMD_PIPELINE_LIBRARY_OUT_OF_MEMORY,
   D12_UMD_PIPELINE_LIBRARY_DUPLICATE,
   D12_UMD_PIPELINE_LIBRARY_NOT_FOUND,
};

struct d12_umd_pipeline_library_entry {
   uint32_t index;
   void *blob;
   size_t blob_size;

   /* Never serialized. The DDI layer retains/releases this renderer object
    * while holding its library lock. */
   void *backend;
};

struct d12_umd_pipeline_library_core {
   struct d12_umd_pipeline_library_entry *entries;
   uint32_t count;
   uint32_t capacity;
};

void d12_umd_pipeline_library_init(
   struct d12_umd_pipeline_library_core *library);
void d12_umd_pipeline_library_cleanup(
   struct d12_umd_pipeline_library_core *library);

enum d12_umd_pipeline_library_result d12_umd_pipeline_library_import(
   struct d12_umd_pipeline_library_core *library,
   const void *data, size_t size);
enum d12_umd_pipeline_library_result d12_umd_pipeline_library_add(
   struct d12_umd_pipeline_library_core *library, uint32_t index,
   const void *blob, size_t blob_size,
   struct d12_umd_pipeline_library_entry **out_entry);

struct d12_umd_pipeline_library_entry *d12_umd_pipeline_library_find(
   struct d12_umd_pipeline_library_core *library, uint32_t index);
const struct d12_umd_pipeline_library_entry *
d12_umd_pipeline_library_find_const(
   const struct d12_umd_pipeline_library_core *library, uint32_t index);

bool d12_umd_pipeline_library_serialized_size(
   const struct d12_umd_pipeline_library_core *library, size_t *out_size);
enum d12_umd_pipeline_library_result d12_umd_pipeline_library_serialize(
   const struct d12_umd_pipeline_library_core *library,
   void *data, size_t size);

