/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define D3D_UMD_INTERFACE_VERSION 0x5002
#include <windows.h>
#include <d3d12umddi.h>
#include <dxgiddi.h>

#include "d12_umd_agx_transport.h"
#include "d12_umd_kmt.h"
#include "d12_umd_pipeline_library.h"
#include "d12_umd_queue_runtime.h"
#include "d12_umd_renderer.h"

#define D12_UMD_DEVICE_SIGNATURE UINT32_C(0x44323141) /* 'A12D' */
#define D12_UMD_COMMAND_QUEUE_SIGNATURE UINT32_C(0x51323141) /* 'A12Q' */
#define D12_UMD_COMMAND_ALLOCATOR_SIGNATURE UINT32_C(0x41323141) /* 'A12A' */
#define D12_UMD_COMMAND_LIST_SIGNATURE UINT32_C(0x4c323141) /* 'A12L' */
#define D12_UMD_ROOT_SIGNATURE_SIGNATURE UINT32_C(0x53323141) /* 'A12S' */
#define D12_UMD_SHADER_SIGNATURE UINT32_C(0x58323141) /* 'A12X' */
#define D12_UMD_ELEMENT_LAYOUT_SIGNATURE UINT32_C(0x49323141) /* 'A12I' */
#define D12_UMD_BLEND_STATE_SIGNATURE UINT32_C(0x42323141) /* 'A12B' */
#define D12_UMD_DEPTH_STENCIL_STATE_SIGNATURE UINT32_C(0x44325341) /* 'AS2D' */
#define D12_UMD_RASTERIZER_STATE_SIGNATURE UINT32_C(0x5a323141) /* 'A12Z' */
#define D12_UMD_PIPELINE_STATE_SIGNATURE UINT32_C(0x50323141) /* 'A12P' */
#define D12_UMD_PIPELINE_LIBRARY_SIGNATURE UINT32_C(0x4c503241) /* 'A2PL' */
#define D12_UMD_HEAP_SIGNATURE UINT32_C(0x48323141) /* 'A12H' */
#define D12_UMD_RESOURCE_SIGNATURE UINT32_C(0x52323141) /* 'A12R' */
#define D12_UMD_DESCRIPTOR_HEAP_SIGNATURE UINT32_C(0x45323141) /* 'A12E' */
#define D12_UMD_FENCE_SIGNATURE UINT32_C(0x46323141) /* 'A12F' */
#define D12_UMD_QUERY_HEAP_SIGNATURE UINT32_C(0x59323141) /* 'A12Y' */
#define D12_UMD_COMMAND_SIGNATURE_SIGNATURE UINT32_C(0x47323141) /* 'A12G' */

struct d12_device;

/* Derived from agx_device.c's fixed compiler-visible VA layout: printf lives
 * at 2^36 and the 4 GiB USC heap is the next 4 GiB-aligned range. */
#define D12_UMD_USC_BASE UINT64_C(0x1100000000)
#define D12_UMD_USC_SIZE UINT64_C(0x100000000)

struct d12_umd_device {
   uint32_t signature;
   UINT interface_version;
   UINT runtime_version;
   D3D12DDI_HRTDEVICE runtime_device;
   D3D12DDI_HDEVICE driver_device;
   PVOID volatile *command_list_3d_runtime_table;
   DXGI_DDI_BASE_CALLBACKS *volatile *dxgi_callbacks;
   D3DDDI_DEVICECALLBACKS kernel_callbacks;
   D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 core_callbacks;
   struct d12_umd_kmt kmt;
   struct d12_umd_agx_transport agx_transport;
   struct d12_device *backend;
   const struct d12_umd_renderer_ops *renderer_ops;
   volatile LONG unimplemented_hits;
   SRWLOCK queue_lock;
   SRWLOCK heap_lock;
   SRWLOCK rotation_lock;
   struct d12_umd_command_queue *queues;
   struct d12_umd_heap *heaps;
   uint32_t queue_count;
   D3DGPU_VIRTUAL_ADDRESS usc_base;
   uint64_t usc_size;
};

struct d12_umd_command_queue {
   uint32_t signature;
   struct d12_umd_device *device;
   enum d12_umd_renderer_command_type type;
   struct d12_umd_queue_runtime runtime;
   void *backend;
   SRWLOCK dxgi_blt_lock;
   void *dxgi_blt_allocator;
   void *dxgi_blt_list;
   struct d12_umd_command_queue *next;
};

struct d12_umd_command_allocator {
   uint32_t signature;
   struct d12_umd_device *device;
   enum d12_umd_renderer_command_type type;
   void *backend;
};

struct d12_umd_command_list {
   uint32_t signature;
   struct d12_umd_device *device;
   struct d12_umd_command_allocator *allocator;
   enum d12_umd_renderer_command_type type;
   D3D12DDI_HRTCOMMANDLIST runtime_list;
   uint64_t id;
   uint32_t slot;
   void *backend;
};

struct d12_umd_root_signature {
   uint32_t signature;
   struct d12_umd_device *device;
   void *backend;
   struct d12_frontend_root_signature_desc desc;
   struct d12_frontend_descriptor_range *ranges;
};

struct d12_umd_shader {
   uint32_t signature;
   struct d12_umd_device *device;
   enum d12_frontend_shader_stage stage;
   void *dxbc;
   size_t dxbc_size;
   struct d12_frontend_stream_output_declaration *so_declarations;
   uint32_t *so_strides;
   uint32_t so_declaration_count;
   uint32_t so_stride_count;
   uint32_t so_rasterized_stream;
};

struct d12_umd_element_layout {
   uint32_t signature;
   struct d12_umd_device *device;
   uint32_t count;
   struct d12_frontend_input_element elements[];
};

struct d12_umd_blend_state {
   uint32_t signature;
   struct d12_umd_device *device;
   struct d12_frontend_blend_desc desc;
};

struct d12_umd_depth_stencil_state {
   uint32_t signature;
   struct d12_umd_device *device;
   struct d12_frontend_depth_stencil_desc desc;
};

struct d12_umd_rasterizer_state {
   uint32_t signature;
   struct d12_umd_device *device;
   struct d12_frontend_rasterizer_desc desc;
};

struct d12_umd_pipeline_state {
   uint32_t signature;
   struct d12_umd_device *device;
   void *backend;
   void *serialization;
   size_t serialization_size;
};

struct d12_umd_pipeline_library {
   uint32_t signature;
   struct d12_umd_device *device;
   SRWLOCK lock;
   struct d12_umd_pipeline_library_core core;
};

struct d12_umd_heap {
   uint32_t signature;
   struct d12_umd_device *device;
   void *backend;
   struct d12_umd_allocation allocation;
   HANDLE runtime_resource;
   D3D12DDI_CPU_PAGE_PROPERTY cpu_page_property;
   SRWLOCK map_lock;
   void *mapping;
   uint32_t map_count;
   struct d12_umd_heap *next;
};

struct d12_umd_fence_entry {
   struct d12_frontend_fence_placement placement;
   struct d12_umd_heap *current_heap;
};

struct d12_umd_fence {
   uint32_t signature;
   struct d12_umd_device *device;
   void *backend;
   uint32_t count;
   struct d12_umd_fence_entry entries[1];
};

struct d12_umd_resource {
   uint32_t signature;
   struct d12_umd_device *device;
   struct d12_umd_heap *heap;
   void *backend;
   D3D12DDIARG_CREATERESOURCE_0003 desc;
   D3D12DDIARG_ROW_MAJOR_RESOURCE_LAYOUT row_major;
};

struct d12_umd_descriptor_heap {
   uint32_t signature;
   struct d12_umd_device *device;
   void *backend;
   D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 desc;
   uint64_t cpu_start;
   uint64_t gpu_start;
};

struct d12_umd_query_heap {
   uint32_t signature;
   struct d12_umd_device *device;
   void *backend;
   D3D12DDI_QUERY_HEAP_TYPE type;
   uint32_t count;
};

struct d12_umd_command_signature {
   uint32_t signature;
   struct d12_umd_device *device;
   void *backend;
};

static inline struct d12_umd_query_heap *
d12_umd_query_heap_from_handle(D3D12DDI_HQUERYHEAP handle)
{
   struct d12_umd_query_heap *heap = handle.pDrvPrivate;
   return heap && heap->signature == D12_UMD_QUERY_HEAP_SIGNATURE ? heap
                                                                  : NULL;
}

static inline struct d12_umd_command_signature *
d12_umd_command_signature_from_handle(D3D12DDI_HCOMMANDSIGNATURE handle)
{
   struct d12_umd_command_signature *signature = handle.pDrvPrivate;
   return signature &&
                 signature->signature == D12_UMD_COMMAND_SIGNATURE_SIGNATURE
             ? signature
             : NULL;
}

static inline struct d12_umd_descriptor_heap *
d12_umd_descriptor_heap_from_handle(D3D12DDI_HDESCRIPTORHEAP handle)
{
   struct d12_umd_descriptor_heap *heap =
      (struct d12_umd_descriptor_heap *)handle.pDrvPrivate;
   return heap && heap->signature == D12_UMD_DESCRIPTOR_HEAP_SIGNATURE
             ? heap
             : NULL;
}

static inline struct d12_umd_fence *
d12_umd_fence_from_handle(D3D12DDI_HFENCE handle)
{
   struct d12_umd_fence *fence =
      (struct d12_umd_fence *)handle.pDrvPrivate;
   return fence && fence->signature == D12_UMD_FENCE_SIGNATURE ? fence
                                                                : NULL;
}

static inline struct d12_umd_device *
d12_umd_device_from_handle(D3D12DDI_HDEVICE handle)
{
   struct d12_umd_device *device =
      (struct d12_umd_device *)handle.pDrvPrivate;
   return device && device->signature == D12_UMD_DEVICE_SIGNATURE ? device
                                                                  : NULL;
}

static inline struct d12_umd_command_allocator *
d12_umd_command_allocator_from_handle(D3D12DDI_HCOMMANDALLOCATOR handle)
{
   struct d12_umd_command_allocator *allocator =
      (struct d12_umd_command_allocator *)handle.pDrvPrivate;
   return allocator &&
                 allocator->signature == D12_UMD_COMMAND_ALLOCATOR_SIGNATURE
             ? allocator
             : NULL;
}

static inline struct d12_umd_command_list *
d12_umd_command_list_from_handle(D3D12DDI_HCOMMANDLIST handle)
{
   struct d12_umd_command_list *list =
      (struct d12_umd_command_list *)handle.pDrvPrivate;
   return list && list->signature == D12_UMD_COMMAND_LIST_SIGNATURE ? list
                                                                    : NULL;
}

static inline struct d12_umd_root_signature *
d12_umd_root_signature_from_handle(D3D12DDI_HROOTSIGNATURE handle)
{
   struct d12_umd_root_signature *signature =
      (struct d12_umd_root_signature *)handle.pDrvPrivate;
   return signature &&
                 signature->signature == D12_UMD_ROOT_SIGNATURE_SIGNATURE
             ? signature
             : NULL;
}

static inline struct d12_umd_shader *
d12_umd_shader_from_handle(D3D12DDI_HSHADER handle)
{
   struct d12_umd_shader *shader =
      (struct d12_umd_shader *)handle.pDrvPrivate;
   return shader && shader->signature == D12_UMD_SHADER_SIGNATURE ? shader
                                                                  : NULL;
}

static inline struct d12_umd_element_layout *
d12_umd_element_layout_from_handle(D3D12DDI_HELEMENTLAYOUT handle)
{
   struct d12_umd_element_layout *layout = handle.pDrvPrivate;
   return layout && layout->signature == D12_UMD_ELEMENT_LAYOUT_SIGNATURE
             ? layout
             : NULL;
}

static inline struct d12_umd_blend_state *
d12_umd_blend_state_from_handle(D3D12DDI_HBLENDSTATE handle)
{
   struct d12_umd_blend_state *state = handle.pDrvPrivate;
   return state && state->signature == D12_UMD_BLEND_STATE_SIGNATURE ? state
                                                                     : NULL;
}

static inline struct d12_umd_depth_stencil_state *
d12_umd_depth_stencil_state_from_handle(D3D12DDI_HDEPTHSTENCILSTATE handle)
{
   struct d12_umd_depth_stencil_state *state = handle.pDrvPrivate;
   return state &&
                 state->signature == D12_UMD_DEPTH_STENCIL_STATE_SIGNATURE
             ? state
             : NULL;
}

static inline struct d12_umd_rasterizer_state *
d12_umd_rasterizer_state_from_handle(D3D12DDI_HRASTERIZERSTATE handle)
{
   struct d12_umd_rasterizer_state *state = handle.pDrvPrivate;
   return state && state->signature == D12_UMD_RASTERIZER_STATE_SIGNATURE
             ? state
             : NULL;
}

static inline struct d12_umd_pipeline_state *
d12_umd_pipeline_state_from_handle(D3D12DDI_HPIPELINESTATE handle)
{
   struct d12_umd_pipeline_state *state = handle.pDrvPrivate;
   return state && state->signature == D12_UMD_PIPELINE_STATE_SIGNATURE
             ? state
             : NULL;
}

static inline struct d12_umd_pipeline_library *
d12_umd_pipeline_library_from_handle(D3D12DDI_HPIPELINELIBRARY handle)
{
   struct d12_umd_pipeline_library *library = handle.pDrvPrivate;
   return library &&
                 library->signature == D12_UMD_PIPELINE_LIBRARY_SIGNATURE
             ? library
             : NULL;
}
