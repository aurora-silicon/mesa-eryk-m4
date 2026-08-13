/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * DDI-owned command objects translated directly into the shared AGX command
 * recorder. The runtime supplies private storage; the renderer retains its
 * normal object and recording lifetime semantics behind opaque references.
 */

#include "d12_umd_command_funcs.h"

#include "d12_umd_command_core.h"
#include "d12_umd_internal.h"

#include <stdbool.h>
#include <string.h>

_Static_assert((uint32_t)D3D12DDI_COMMAND_LIST_TYPE_DIRECT ==
                  D12_UMD_DDI_COMMAND_LIST_DIRECT,
               "pinned DDI direct-list encoding changed");
_Static_assert((uint32_t)D3D12DDI_COMMAND_LIST_TYPE_BUNDLE ==
                  D12_UMD_DDI_COMMAND_LIST_BUNDLE,
               "pinned DDI bundle-list encoding changed");
_Static_assert((uint32_t)D3D12DDI_COMMAND_QUEUE_FLAG_3D ==
                  D12_UMD_DDI_QUEUE_3D,
               "pinned DDI 3D queue encoding changed");
_Static_assert((uint32_t)D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE ==
                  D12_UMD_DDI_QUEUE_COMPUTE,
               "pinned DDI compute queue encoding changed");
_Static_assert((uint32_t)D3D12DDI_COMMAND_QUEUE_FLAG_COPY ==
                  D12_UMD_DDI_QUEUE_COPY,
               "pinned DDI copy queue encoding changed");
_Static_assert((uint32_t)D3D12DDI_COMMAND_LIST_FLAG_ENABLE_MARKERS ==
                  D12_UMD_DDI_COMMAND_LIST_ENABLE_MARKERS,
               "pinned DDI marker-list flag encoding changed");
_Static_assert(
   (uint32_t)D3D12DDI_COMMAND_LIST_FLAG_0010_ENABLE_FULLPIPELINE_MARKERS ==
      D12_UMD_DDI_COMMAND_LIST_ENABLE_FULL_PIPELINE_MARKERS,
   "pinned DDI full-pipeline marker-list flag encoding changed");

static void
d12_umd_command_set_error(struct d12_umd_device *device, HRESULT result)
{
   d12_umd_report_device_error(device, result);
}

static void
d12_umd_command_list_set_error(struct d12_umd_command_list *list,
                               HRESULT result)
{
   d12_umd_report_command_list_error(list, result);
}

static bool
d12_umd_command_type(D3D12DDI_COMMAND_LIST_TYPE list_type,
                     D3D12DDI_COMMAND_QUEUE_FLAGS queue_flags,
                     enum d12_umd_renderer_command_type *renderer_type)
{
   return d12_umd_command_type_from_ddi((uint32_t)list_type,
                                        (uint32_t)queue_flags,
                                        renderer_type);
}

static SIZE_T APIENTRY
d12_umd_calc_private_command_allocator_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATECOMMANDALLOCATOR *arguments)
{
   enum d12_umd_renderer_command_type type;

   if (!d12_umd_device_from_handle(handle) || !arguments ||
       !d12_umd_command_type(arguments->Type, arguments->QueueFlags, &type))
      return 0;
   return sizeof(struct d12_umd_command_allocator);
}

static HRESULT APIENTRY
d12_umd_create_command_allocator(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATECOMMANDALLOCATOR *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_command_allocator *allocator;
   enum d12_umd_renderer_command_type type;
   HRESULT result;

   if (!device || !arguments || !arguments->hDrvCommandAllocator.pDrvPrivate ||
       !d12_umd_command_type(arguments->Type, arguments->QueueFlags, &type))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_command_allocator ||
       !device->renderer_ops->destroy_command_allocator)
      return E_NOTIMPL;

   allocator = (struct d12_umd_command_allocator *)
      arguments->hDrvCommandAllocator.pDrvPrivate;
   memset(allocator, 0, sizeof(*allocator));
   result = device->renderer_ops->create_command_allocator(
      device->backend, type, &allocator->backend);
   result = d12_umd_renderer_object_result(
      result, &allocator->backend,
      device->renderer_ops->destroy_command_allocator);
   if (FAILED(result))
      return result;

   allocator->signature = D12_UMD_COMMAND_ALLOCATOR_SIGNATURE;
   allocator->device = device;
   allocator->type = type;
   return S_OK;
}

static void APIENTRY
d12_umd_destroy_command_allocator(
   D3D12DDI_HDEVICE handle, D3D12DDI_HCOMMANDALLOCATOR allocator_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_command_allocator *allocator =
      d12_umd_command_allocator_from_handle(allocator_handle);

   if (!device || !allocator || allocator->device != device)
      return;
   if (allocator->backend && device->renderer_ops &&
       device->renderer_ops->destroy_command_allocator)
      device->renderer_ops->destroy_command_allocator(allocator->backend);
   SecureZeroMemory(allocator, sizeof(*allocator));
}

static void APIENTRY
d12_umd_reset_command_allocator(D3D12DDI_HCOMMANDALLOCATOR allocator_handle)
{
   struct d12_umd_command_allocator *allocator =
      d12_umd_command_allocator_from_handle(allocator_handle);
   struct d12_umd_device *device;
   HRESULT result;

   if (!allocator)
      return;
   device = allocator->device;
   if (!device || !allocator->backend || !device->renderer_ops ||
       !device->renderer_ops->reset_command_allocator) {
      d12_umd_command_set_error(device, E_NOTIMPL);
      return;
   }

   result = device->renderer_ops->reset_command_allocator(allocator->backend);
   if (FAILED(result))
      d12_umd_command_set_error(device, result);
}

static bool
d12_umd_command_list_arguments_valid(
   struct d12_umd_device *device,
   const D3D12DDIARG_CREATE_COMMAND_LIST_0001 *arguments,
   struct d12_umd_command_allocator **out_allocator,
   enum d12_umd_renderer_command_type *out_type)
{
   struct d12_umd_command_allocator *allocator;
   enum d12_umd_renderer_command_type type;

   if (!device || !arguments ||
       (arguments->NodeMask != 0 && arguments->NodeMask != 1) ||
       !d12_umd_command_list_flags_valid(
          (uint32_t)arguments->CommandListFlags,
          device->interface_version == D3D12DDI_INTERFACE_VERSION_R1) ||
       !d12_umd_command_type(arguments->Type, arguments->QueueFlags, &type))
      return false;

   allocator = d12_umd_command_allocator_from_handle(
      arguments->hDrvCommandAllocator);
   if (!allocator || allocator->device != device || allocator->type != type)
      return false;

   if (out_allocator)
      *out_allocator = allocator;
   if (out_type)
      *out_type = type;
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_command_list_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_COMMAND_LIST_0001 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);

   return d12_umd_command_list_arguments_valid(device, arguments, NULL, NULL)
             ? sizeof(struct d12_umd_command_list)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_command_list(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_COMMAND_LIST_0001 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_command_allocator *allocator;
   struct d12_umd_command_list *list;
   enum d12_umd_renderer_command_type type;
   HRESULT result;

   if (!arguments || !arguments->hDrvCommandList.pDrvPrivate ||
       !arguments->hRTCommandList.handle ||
       !d12_umd_command_list_arguments_valid(device, arguments, &allocator,
                                              &type))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_command_list ||
       !device->renderer_ops->destroy_command_list)
      return E_NOTIMPL;

   list = (struct d12_umd_command_list *)arguments->hDrvCommandList.pDrvPrivate;
   memset(list, 0, sizeof(*list));
   result = device->renderer_ops->create_command_list(
      device->backend, type, allocator->backend, &list->backend);
   result = d12_umd_renderer_object_result(
      result, &list->backend, device->renderer_ops->destroy_command_list);
   if (FAILED(result))
      return result;

   list->signature = D12_UMD_COMMAND_LIST_SIGNATURE;
   list->device = device;
   list->allocator = allocator;
   list->type = type;
   list->runtime_list = arguments->hRTCommandList;
   list->id = arguments->ID;
   {
      /* FillDDITable's runtime handle selects the command-list dispatch table.
       * The D3D12 core layer requires the UMD to bind that exact handle before
       * returning the newly created command list to the runtime. */
      D3D12DDI_HRTTABLE runtime_table = {
         .handle = device->command_list_3d_runtime_table
                      ? InterlockedCompareExchangePointer(
                           device->command_list_3d_runtime_table, NULL, NULL)
                      : NULL,
      };
      if (!runtime_table.handle ||
          !device->core_callbacks.pfnSetCommandListDDITableCb) {
         device->renderer_ops->destroy_command_list(list->backend);
         SecureZeroMemory(list, sizeof(*list));
         return E_FAIL;
      }
      device->core_callbacks.pfnSetCommandListDDITableCb(
         arguments->hRTCommandList, runtime_table);
   }
   return S_OK;
}

static void APIENTRY
d12_umd_destroy_command_list(D3D12DDI_HDEVICE handle,
                             D3D12DDI_HCOMMANDLIST list_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   if (!device || !list || list->device != device)
      return;
   if (list->backend && device->renderer_ops &&
       device->renderer_ops->destroy_command_list)
      device->renderer_ops->destroy_command_list(list->backend);
   SecureZeroMemory(list, sizeof(*list));
}

static void APIENTRY
d12_umd_close_command_list(D3D12DDI_HCOMMANDLIST list_handle)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_device *device;
   HRESULT result;

   if (!list)
      return;
   device = list->device;
   if (!device || !list->backend || !device->renderer_ops ||
       !device->renderer_ops->close_command_list) {
      d12_umd_command_list_set_error(list, E_NOTIMPL);
      return;
   }

   result = device->renderer_ops->close_command_list(list->backend);
   if (FAILED(result))
      d12_umd_command_list_set_error(list, result);
}

static void APIENTRY
d12_umd_reset_command_list(
   D3D12DDI_HCOMMANDLIST list_handle,
   const D3D12DDIARG_RESETCOMMANDLIST *arguments)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_command_allocator *allocator;
   struct d12_umd_device *device;
   HRESULT result;

   if (!list)
      return;
   device = list->device;
   allocator = arguments ? d12_umd_command_allocator_from_handle(
                              arguments->hDrvCommandAllocator)
                         : NULL;
   if (!arguments || !device || !allocator || allocator->device != device ||
       allocator->type != list->type ||
       !d12_umd_command_list_flags_valid(
          (uint32_t)arguments->CommandListFlags,
          device->interface_version == D3D12DDI_INTERFACE_VERSION_R1)) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   if (!list->backend || !allocator->backend || !device->renderer_ops ||
       !device->renderer_ops->reset_command_list) {
      d12_umd_command_list_set_error(list, E_NOTIMPL);
      return;
   }

   result = device->renderer_ops->reset_command_list(list->backend,
                                                      allocator->backend);
   if (FAILED(result)) {
      d12_umd_command_list_set_error(list, result);
      return;
   }
   list->allocator = allocator;
   list->slot = arguments->Slot;
   list->id = arguments->ID;
}

static void APIENTRY
d12_umd_draw_instanced(D3D12DDI_HCOMMANDLIST list_handle,
                       UINT vertex_count_per_instance, UINT instance_count,
                       UINT start_vertex, UINT start_instance)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   if (!list || !list->device || !list->backend) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   if (!list->device->renderer_ops ||
       !list->device->renderer_ops->draw_instanced) {
      d12_umd_command_list_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->draw_instanced(
      list->backend, vertex_count_per_instance, instance_count, start_vertex,
      start_instance);
}

static void APIENTRY
d12_umd_draw_indexed_instanced(
   D3D12DDI_HCOMMANDLIST list_handle, UINT index_count_per_instance,
   UINT instance_count, UINT start_index, INT base_vertex, UINT start_instance)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   if (!list || !list->device || !list->backend) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   if (!list->device->renderer_ops ||
       !list->device->renderer_ops->draw_indexed_instanced) {
      d12_umd_command_list_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->draw_indexed_instanced(
      list->backend, index_count_per_instance, instance_count, start_index,
      base_vertex, start_instance);
}

static void APIENTRY
d12_umd_dispatch(D3D12DDI_HCOMMANDLIST list_handle, UINT group_count_x,
                 UINT group_count_y, UINT group_count_z)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   if (!list || !list->device || !list->backend) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   if (!list->device->renderer_ops ||
       !list->device->renderer_ops->dispatch) {
      d12_umd_command_list_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->dispatch(list->backend, group_count_x,
                                         group_count_y, group_count_z);
}

static void APIENTRY
d12_umd_execute_bundle(D3D12DDI_HCOMMANDLIST list_handle,
                       D3D12DDI_HCOMMANDLIST bundle_handle)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_command_list *bundle =
      d12_umd_command_list_from_handle(bundle_handle);

   if (!list || !list->device || !bundle || !bundle->device ||
       list->device != bundle->device || !list->backend ||
       !bundle->backend || list->type != D12_UMD_RENDERER_COMMAND_DIRECT ||
       bundle->type != D12_UMD_RENDERER_COMMAND_BUNDLE ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->execute_bundle) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->execute_bundle(list->backend,
                                               bundle->backend);
}

static void APIENTRY
d12_umd_clear_root_arguments(D3D12DDI_HCOMMANDLIST list_handle)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   if (!list || !list->device || !list->backend ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->clear_root_arguments) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->clear_root_arguments(list->backend);
}

static void APIENTRY
d12_umd_set_marker(D3D12DDI_HCOMMANDLIST list_handle, UINT64 marker)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   (void)marker;
   if (!list || !list->backend)
      d12_umd_command_list_set_error(list, E_INVALIDARG);
}

static void APIENTRY
d12_umd_set_topology(D3D12DDI_HCOMMANDLIST list_handle,
                     D3D12DDI_PRIMITIVE_TOPOLOGY topology)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   if (!list || !list->device || !list->backend) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   if (!list->device->renderer_ops ||
       !list->device->renderer_ops->set_topology) {
      d12_umd_command_list_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->set_topology(list->backend,
                                             (uint32_t)topology);
}

static void APIENTRY
d12_umd_set_viewports(D3D12DDI_HCOMMANDLIST list_handle, UINT count,
                      const D3D12DDI_VIEWPORT *viewports)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   float translated[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE][6];

   if (!list || !list->device || !list->backend ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->set_viewports ||
       count > D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE ||
       (count && !viewports)) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }

   for (UINT index = 0; index < count; ++index) {
      translated[index][0] = viewports[index].TopLeftX;
      translated[index][1] = viewports[index].TopLeftY;
      translated[index][2] = viewports[index].Width;
      translated[index][3] = viewports[index].Height;
      translated[index][4] = viewports[index].MinDepth;
      translated[index][5] = viewports[index].MaxDepth;
   }
   list->device->renderer_ops->set_viewports(list->backend, count,
                                              translated);
}

static void APIENTRY
d12_umd_set_scissors(D3D12DDI_HCOMMANDLIST list_handle, UINT count,
                     const D3D12DDI_RECT *rects)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   int32_t translated[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE][4];

   if (!list || !list->device || !list->backend ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->set_scissors ||
       count > D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE ||
       (count && !rects)) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }

   for (UINT index = 0; index < count; ++index) {
      translated[index][0] = rects[index].left;
      translated[index][1] = rects[index].top;
      translated[index][2] = rects[index].right;
      translated[index][3] = rects[index].bottom;
   }
   list->device->renderer_ops->set_scissors(list->backend, count, translated);
}

static void APIENTRY
d12_umd_set_blend_factor(D3D12DDI_HCOMMANDLIST list_handle,
                         const FLOAT factor[4])
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   if (!list || !list->device || !list->backend || !factor) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   if (!list->device->renderer_ops ||
       !list->device->renderer_ops->set_blend_factor) {
      d12_umd_command_list_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->set_blend_factor(list->backend, factor);
}

static void APIENTRY
d12_umd_set_stencil_ref(D3D12DDI_HCOMMANDLIST list_handle, UINT reference)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);

   if (!list || !list->device || !list->backend) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   if (!list->device->renderer_ops ||
       !list->device->renderer_ops->set_stencil_ref) {
      d12_umd_command_list_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->set_stencil_ref(list->backend, reference);
}

#if D12_UMD_TARGETS_WDDM23
static void APIENTRY
d12_umd_atomic_copy_buffer_region_unsupported(
   D3D12DDI_HCOMMANDLIST list_handle,
   D3D12DDIARG_BUFFER_PLACEMENT destination,
   D3D12DDIARG_BUFFER_PLACEMENT source, UINT64 bytes)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   (void)destination;
   (void)source;
   (void)bytes;
   d12_umd_command_list_set_error(list, E_NOTIMPL);
}

static void APIENTRY
d12_umd_depth_bounds_unsupported(D3D12DDI_HCOMMANDLIST list_handle,
                                 FLOAT minimum, FLOAT maximum)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   (void)minimum;
   (void)maximum;
   d12_umd_command_list_set_error(list, E_NOTIMPL);
}

static void APIENTRY
d12_umd_sample_positions_unsupported(
   D3D12DDI_HCOMMANDLIST list_handle, UINT samples_per_pixel,
   UINT pixels, D3D12DDI_SAMPLE_POSITION *positions)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   (void)samples_per_pixel;
   (void)pixels;
   (void)positions;
   d12_umd_command_list_set_error(list, E_NOTIMPL);
}

static void APIENTRY
d12_umd_protected_session_unsupported(
   D3D12DDI_HCOMMANDLIST list_handle,
   D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   (void)session;
   d12_umd_command_list_set_error(list, E_NOTIMPL);
}

static void APIENTRY
d12_umd_write_buffer_immediate(
   D3D12DDI_HCOMMANDLIST list_handle, UINT count,
   const D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032 *parameters,
   const D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032 *modes)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_frontend_write_buffer_immediate_parameter local_parameters[32];
   struct d12_frontend_write_buffer_immediate_parameter *translated =
      local_parameters;
   uint32_t local_modes[32];
   uint32_t *translated_modes = local_modes;
   HRESULT result;

   if (!list || !list->device || !list->backend ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->write_buffer_immediate ||
       (count && !parameters)) {
      d12_umd_command_list_set_error(list, E_INVALIDARG);
      return;
   }
   if (count > ARRAYSIZE(local_parameters)) {
      if ((SIZE_T)count > SIZE_MAX / sizeof(*translated)) {
         d12_umd_command_list_set_error(list, E_OUTOFMEMORY);
         return;
      }
      translated = HeapAlloc(GetProcessHeap(), 0,
                             (SIZE_T)count * sizeof(*translated));
      translated_modes = HeapAlloc(GetProcessHeap(), 0,
                                   (SIZE_T)count * sizeof(*translated_modes));
      if (!translated || !translated_modes) {
         if (translated)
            HeapFree(GetProcessHeap(), 0, translated);
         if (translated_modes)
            HeapFree(GetProcessHeap(), 0, translated_modes);
         d12_umd_command_list_set_error(list, E_OUTOFMEMORY);
         return;
      }
   }
   for (UINT index = 0; index < count; ++index) {
      translated[index] =
         (struct d12_frontend_write_buffer_immediate_parameter){
            .destination = parameters[index].Dst,
            .value = parameters[index].Value,
         };
      translated_modes[index] = modes
         ? (uint32_t)modes[index]
         : D12_FRONTEND_WRITE_BUFFER_IMMEDIATE_DEFAULT;
   }
   result = list->device->renderer_ops->write_buffer_immediate(
      list->backend, count, translated, modes ? translated_modes : NULL);
   if (translated != local_parameters) {
      HeapFree(GetProcessHeap(), 0, translated);
      HeapFree(GetProcessHeap(), 0, translated_modes);
   }
   if (FAILED(result))
      d12_umd_command_list_set_error(list, result);
}
#endif

void
d12_umd_install_command_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table)
{
   table->pfnCalcPrivateCommandAllocatorSize =
      d12_umd_calc_private_command_allocator_size;
   table->pfnCreateCommandAllocator = d12_umd_create_command_allocator;
   table->pfnDestroyCommandAllocator = d12_umd_destroy_command_allocator;
   table->pfnResetCommandAllocator = d12_umd_reset_command_allocator;
   table->pfnCalcPrivateCommandListSize =
      d12_umd_calc_private_command_list_size;
   table->pfnCreateCommandList = d12_umd_create_command_list;
   table->pfnDestroyCommandList = d12_umd_destroy_command_list;
}

void
d12_umd_install_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table)
{
   table->pfnCloseCommandList = d12_umd_close_command_list;
   table->pfnResetCommandList = d12_umd_reset_command_list;
   table->pfnDrawInstanced = d12_umd_draw_instanced;
   table->pfnDrawIndexedInstanced = d12_umd_draw_indexed_instanced;
   table->pfnDispatch = d12_umd_dispatch;
   table->pfnExecuteBundle = d12_umd_execute_bundle;
   table->pfnSetMarker = d12_umd_set_marker;
   table->pfnClearRootArguments = d12_umd_clear_root_arguments;
   table->pfnIaSetTopology = d12_umd_set_topology;
   table->pfnRsSetViewports = d12_umd_set_viewports;
   table->pfnRsSetScissorRects = d12_umd_set_scissors;
   table->pfnOmSetBlendFactor = d12_umd_set_blend_factor;
   table->pfnOmSetStencilRef = d12_umd_set_stencil_ref;
}

#if D12_UMD_TARGETS_WDDM23
void
d12_umd_install_command_list_functions_0032(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0032 *table)
{
   table->pfnAtomicCopyBufferRegion =
      d12_umd_atomic_copy_buffer_region_unsupported;
   table->pfnOMSetDepthBounds = d12_umd_depth_bounds_unsupported;
   table->pfnSetSamplePositions = d12_umd_sample_positions_unsupported;
   table->pfnSetProtectedResourceSession =
      d12_umd_protected_session_unsupported;
   table->pfnWriteBufferImmediate = d12_umd_write_buffer_immediate;
}
#endif
