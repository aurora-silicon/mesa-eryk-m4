/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Versioned, pointer-free pipeline snapshots for the baseline D3D12 object
 * serialization DDI.  These snapshots contain the frontend descriptors and
 * shader containers needed to recreate a native AGX PSO without retaining
 * any runtime-owned DDI object.
 */
#pragma once

#include "../d12_frontend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D12_UMD_PIPELINE_SERIALIZATION_VERSION 3u

enum d12_umd_serialized_pipeline_kind {
   D12_UMD_SERIALIZED_PIPELINE_COMPUTE = 1,
   D12_UMD_SERIALIZED_PIPELINE_GRAPHICS = 2,
};

struct d12_umd_serialized_pipeline {
   enum d12_umd_serialized_pipeline_kind kind;
   bool has_root_signature;
   struct d12_frontend_root_signature_desc root_signature;
   struct d12_frontend_compute_pipeline_desc compute;
   struct d12_frontend_graphics_pipeline_desc graphics;

   /* Owned range storage is not directly reachable from an empty root
    * signature and therefore remains explicit for cleanup. */
   void *owned_root_ranges;
};

bool d12_umd_pipeline_serialize_compute(
   const struct d12_frontend_compute_pipeline_desc *pipeline,
   const struct d12_frontend_root_signature_desc *root_signature,
   void **out_blob, size_t *out_size);

bool d12_umd_pipeline_serialize_graphics(
   const struct d12_frontend_graphics_pipeline_desc *pipeline,
   const struct d12_frontend_root_signature_desc *root_signature,
   void **out_blob, size_t *out_size);

bool d12_umd_pipeline_serialization_valid(const void *blob, size_t size);

bool d12_umd_pipeline_deserialize(
   const void *blob, size_t size,
   struct d12_umd_serialized_pipeline *out_pipeline);

void d12_umd_serialized_pipeline_cleanup(
   struct d12_umd_serialized_pipeline *pipeline);
