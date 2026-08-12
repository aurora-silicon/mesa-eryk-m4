/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Query heaps, query recording/resolution and predication for the baseline
 * D3D12 DDI. Runtime-private wrappers retain the renderer's native query
 * objects; all GPU work remains in the shared AGX command recorder.
 */

#include "d12_umd_query_funcs.h"

#include "d12_umd_internal.h"

#include <stdbool.h>
#include <string.h>

static void
d12_umd_query_error(struct d12_umd_device *device, HRESULT result)
{
   if (device && device->core_callbacks.pfnSetErrorCb)
      device->core_callbacks.pfnSetErrorCb(device->runtime_device, result);
}

static bool
d12_umd_query_heap_arguments_valid(
   const D3D12DDIARG_CREATE_QUERY_HEAP_0001 *arguments)
{
   if (!arguments || !arguments->Count || arguments->NodeMask > 1)
      return false;

   switch (arguments->Type) {
   case D3D12DDI_QUERY_HEAP_TYPE_OCCLUSION:
   case D3D12DDI_QUERY_HEAP_TYPE_TIMESTAMP:
   case D3D12DDI_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
   case D3D12DDI_QUERY_HEAP_TYPE_SO_STATISTICS:
      return true;
   default:
      return false;
   }
}

static bool
d12_umd_query_type_matches(D3D12DDI_QUERY_HEAP_TYPE heap_type,
                           D3D12DDI_QUERY_TYPE query_type)
{
   switch (heap_type) {
   case D3D12DDI_QUERY_HEAP_TYPE_OCCLUSION:
      return query_type == D3D12DDI_QUERY_TYPE_OCCLUSION ||
             query_type == D3D12DDI_QUERY_TYPE_BINARY_OCCLUSION;
   case D3D12DDI_QUERY_HEAP_TYPE_TIMESTAMP:
      return query_type == D3D12DDI_QUERY_TYPE_TIMESTAMP;
   case D3D12DDI_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
      return query_type == D3D12DDI_QUERY_TYPE_PIPELINE_STATISTICS;
   case D3D12DDI_QUERY_HEAP_TYPE_SO_STATISTICS:
      return query_type >= D3D12DDI_QUERY_TYPE_SO_STATISTICS_STREAM0 &&
             query_type <= D3D12DDI_QUERY_TYPE_SO_STATISTICS_STREAM3;
   default:
      return false;
   }
}

static bool
d12_umd_command_signature_arguments_valid(
   struct d12_umd_device *device,
   const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *arguments,
   struct d12_umd_root_signature **out_root_signature)
{
   struct d12_umd_root_signature *root_signature = NULL;
   SIZE_T packed_size = 0;
   bool changes_root_arguments = false;

   if (!device || !arguments || !arguments->NumArgumentDescs ||
       arguments->NumArgumentDescs > 65 || !arguments->pArgumentDescs ||
       arguments->NodeMask > 1)
      return false;
   if (arguments->hRootSignature.pDrvPrivate) {
      root_signature =
         d12_umd_root_signature_from_handle(arguments->hRootSignature);
      if (!root_signature || root_signature->device != device ||
          !root_signature->backend)
         return false;
   }

   for (UINT index = 0; index < arguments->NumArgumentDescs; ++index) {
      const D3D12DDI_INDIRECT_ARGUMENT_DESC *desc =
         &arguments->pArgumentDescs[index];
      SIZE_T element_size;
      bool terminal = false;

      switch (desc->Type) {
      case D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW:
         element_size = sizeof(D3D12DDI_DRAW_ARGUMENTS);
         terminal = true;
         break;
      case D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
         element_size = sizeof(D3D12DDI_DRAW_INDEXED_ARGUMENTS);
         terminal = true;
         break;
      case D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH:
         element_size = sizeof(D3D12DDI_DISPATCH_ARGUMENTS);
         terminal = true;
         break;
      case D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT:
         if (!root_signature || !desc->Constant.Num32BitValuesToSet ||
             desc->Constant.RootParameterIndex >=
                root_signature->desc.num_parameters ||
             desc->Constant.Num32BitValuesToSet >
                (SIZE_MAX - packed_size) / sizeof(uint32_t))
            return false;
         {
            const struct d12_frontend_root_parameter *parameter =
               &root_signature
                   ->desc.parameters[desc->Constant.RootParameterIndex];
            if (parameter->parameter_type !=
                   D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS ||
                desc->Constant.DestOffsetIn32BitValues >
                   parameter->constants.num_32bit_values ||
                desc->Constant.Num32BitValuesToSet >
                   parameter->constants.num_32bit_values -
                      desc->Constant.DestOffsetIn32BitValues)
               return false;
         }
         element_size = (SIZE_T)desc->Constant.Num32BitValuesToSet *
                        sizeof(uint32_t);
         changes_root_arguments = true;
         break;
      case D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
      case D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
      case D3D12DDI_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW: {
         UINT root_parameter_index;
         D3D12DDI_ROOT_PARAMETER_TYPE expected;
         if (desc->Type ==
             D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW) {
            root_parameter_index =
               desc->ConstantBufferView.RootParameterIndex;
            expected = D3D12DDI_ROOT_PARAMETER_TYPE_CBV;
         } else if (desc->Type ==
                    D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW) {
            root_parameter_index =
               desc->ShaderResourceView.RootParameterIndex;
            expected = D3D12DDI_ROOT_PARAMETER_TYPE_SRV;
         } else {
            root_parameter_index =
               desc->UnorderedAccessView.RootParameterIndex;
            expected = D3D12DDI_ROOT_PARAMETER_TYPE_UAV;
         }
         if (!root_signature ||
             root_parameter_index >= root_signature->desc.num_parameters ||
             root_signature->desc.parameters[root_parameter_index]
                   .parameter_type != (uint32_t)expected)
            return false;
         element_size = sizeof(uint64_t);
         changes_root_arguments = true;
         break;
      }
      default:
         return false;
      }
      if (terminal && index + 1 != arguments->NumArgumentDescs)
         return false;
      if (element_size > SIZE_MAX - packed_size)
         return false;
      packed_size += element_size;
   }
   D3D12DDI_INDIRECT_ARGUMENT_TYPE terminal_type =
      arguments->pArgumentDescs[arguments->NumArgumentDescs - 1].Type;
   if ((terminal_type != D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW &&
        terminal_type != D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED &&
        terminal_type != D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH) ||
       ((root_signature != NULL) != changes_root_arguments) ||
       arguments->ByteStride < packed_size || (arguments->ByteStride & 3))
      return false;
   if (out_root_signature)
      *out_root_signature = root_signature;
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_query_heap_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_QUERY_HEAP_0001 *arguments)
{
   return d12_umd_device_from_handle(handle) &&
                 d12_umd_query_heap_arguments_valid(arguments)
             ? sizeof(struct d12_umd_query_heap)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_query_heap(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_QUERY_HEAP_0001 *arguments,
   D3D12DDI_HQUERYHEAP heap_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_query_heap *heap = heap_handle.pDrvPrivate;
   HRESULT result;

   if (!device || !heap || !d12_umd_query_heap_arguments_valid(arguments))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_query_heap)
      return E_NOTIMPL;

   memset(heap, 0, sizeof(*heap));
   result = device->renderer_ops->create_query_heap(
      device->backend, (uint32_t)arguments->Type, arguments->Count,
      arguments->NodeMask, &heap->backend);
   if (FAILED(result))
      return result;
   if (!heap->backend)
      return E_FAIL;

   heap->signature = D12_UMD_QUERY_HEAP_SIGNATURE;
   heap->device = device;
   heap->type = arguments->Type;
   heap->count = arguments->Count;
   return S_OK;
}

static void APIENTRY
d12_umd_destroy_query_heap(D3D12DDI_HDEVICE handle,
                           D3D12DDI_HQUERYHEAP heap_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_query_heap *heap =
      d12_umd_query_heap_from_handle(heap_handle);

   if (!device || !heap || heap->device != device)
      return;
   if (heap->backend && device->renderer_ops &&
       device->renderer_ops->destroy_query_heap)
      device->renderer_ops->destroy_query_heap(heap->backend);
   SecureZeroMemory(heap, sizeof(*heap));
}

static SIZE_T APIENTRY
d12_umd_calc_private_command_signature_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);

   return d12_umd_command_signature_arguments_valid(device, arguments, NULL)
             ? sizeof(struct d12_umd_command_signature)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_command_signature(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *arguments,
   D3D12DDI_HCOMMANDSIGNATURE signature_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_command_signature *signature =
      signature_handle.pDrvPrivate;
   struct d12_umd_root_signature *root_signature;
   struct d12_frontend_indirect_argument_desc translated[65];
   struct d12_frontend_command_signature_desc desc;
   HRESULT result;

   if (!signature || !d12_umd_command_signature_arguments_valid(
                       device, arguments, &root_signature))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_command_signature)
      return E_NOTIMPL;

   for (UINT index = 0; index < arguments->NumArgumentDescs; ++index) {
      const D3D12DDI_INDIRECT_ARGUMENT_DESC *source =
         &arguments->pArgumentDescs[index];
      translated[index] = (struct d12_frontend_indirect_argument_desc){
         .type = (uint32_t)source->Type,
      };
      if (source->Type == D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT) {
         translated[index].root_parameter_index =
            source->Constant.RootParameterIndex;
         translated[index].destination_offset =
            source->Constant.DestOffsetIn32BitValues;
         translated[index].value_count =
            source->Constant.Num32BitValuesToSet;
      } else if (source->Type ==
                 D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW) {
         translated[index].root_parameter_index =
            source->ConstantBufferView.RootParameterIndex;
      } else if (source->Type ==
                 D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW) {
         translated[index].root_parameter_index =
            source->ShaderResourceView.RootParameterIndex;
      } else if (source->Type ==
                 D3D12DDI_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW) {
         translated[index].root_parameter_index =
            source->UnorderedAccessView.RootParameterIndex;
      }
   }
   desc = (struct d12_frontend_command_signature_desc){
      .byte_stride = arguments->ByteStride,
      .argument_count = arguments->NumArgumentDescs,
      .arguments = translated,
      .root_signature = root_signature ? root_signature->backend : NULL,
      .node_mask = arguments->NodeMask,
   };
   memset(signature, 0, sizeof(*signature));
   result = device->renderer_ops->create_command_signature(
      device->backend, &desc, &signature->backend);
   if (FAILED(result))
      return result;
   if (!signature->backend)
      return E_FAIL;
   signature->signature = D12_UMD_COMMAND_SIGNATURE_SIGNATURE;
   signature->device = device;
   return S_OK;
}

static void APIENTRY
d12_umd_destroy_command_signature(
   D3D12DDI_HDEVICE handle, D3D12DDI_HCOMMANDSIGNATURE signature_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_command_signature *signature =
      d12_umd_command_signature_from_handle(signature_handle);

   if (!device || !signature || signature->device != device)
      return;
   if (signature->backend && device->renderer_ops &&
       device->renderer_ops->destroy_command_signature)
      device->renderer_ops->destroy_command_signature(signature->backend);
   SecureZeroMemory(signature, sizeof(*signature));
}

static struct d12_umd_query_heap *
d12_umd_command_query_heap(struct d12_umd_command_list *list,
                           D3D12DDI_HQUERYHEAP handle,
                           D3D12DDI_QUERY_TYPE type, UINT index)
{
   struct d12_umd_query_heap *heap = d12_umd_query_heap_from_handle(handle);

   return list && heap && heap->device == list->device && heap->backend &&
                 index < heap->count &&
                 d12_umd_query_type_matches(heap->type, type)
             ? heap
             : NULL;
}

static void APIENTRY
d12_umd_begin_query(D3D12DDI_HCOMMANDLIST list_handle,
                    D3D12DDI_HQUERYHEAP heap_handle,
                    D3D12DDI_QUERY_TYPE type, UINT index)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_query_heap *heap =
      d12_umd_command_query_heap(list, heap_handle, type, index);

   if (!list || !list->backend || !heap ||
       heap->type == D3D12DDI_QUERY_HEAP_TYPE_TIMESTAMP ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->begin_query) {
      d12_umd_query_error(list ? list->device : NULL, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->begin_query(
      list->backend, heap->backend, (uint32_t)type, index);
}

static void APIENTRY
d12_umd_end_query(D3D12DDI_HCOMMANDLIST list_handle,
                  D3D12DDI_HQUERYHEAP heap_handle,
                  D3D12DDI_QUERY_TYPE type, UINT index)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_query_heap *heap =
      d12_umd_command_query_heap(list, heap_handle, type, index);

   if (!list || !list->backend || !heap || !list->device->renderer_ops ||
       !list->device->renderer_ops->end_query) {
      d12_umd_query_error(list ? list->device : NULL, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->end_query(
      list->backend, heap->backend, (uint32_t)type, index);
}

static void APIENTRY
d12_umd_resolve_query_data(D3D12DDI_HCOMMANDLIST list_handle,
                           D3D12DDI_HQUERYHEAP heap_handle,
                           D3D12DDI_QUERY_TYPE type, UINT first, UINT count,
                           D3D12DDI_HRESOURCE destination_handle,
                           UINT64 destination_offset)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_query_heap *heap =
      d12_umd_query_heap_from_handle(heap_handle);
   struct d12_umd_resource *destination = destination_handle.pDrvPrivate;

   if (!list || !list->backend || !heap || heap->device != list->device ||
       !heap->backend || !count || first >= heap->count ||
       count > heap->count - first ||
       !d12_umd_query_type_matches(heap->type, type) || !destination ||
       destination->signature != D12_UMD_RESOURCE_SIGNATURE ||
       destination->device != list->device || !destination->backend ||
       destination->desc.ResourceType != D3D12DDI_RT_BUFFER ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->resolve_query_data) {
      d12_umd_query_error(list ? list->device : NULL, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->resolve_query_data(
      list->backend, heap->backend, (uint32_t)type, first, count,
      destination->backend, destination_offset);
}

static void APIENTRY
d12_umd_set_predication(D3D12DDI_HCOMMANDLIST list_handle,
                        D3D12DDI_HRESOURCE resource_handle, UINT64 offset,
                        D3D12DDI_PREDICATION_OP operation)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_resource *resource = resource_handle.pDrvPrivate;

   if (!list || !list->backend || (uint32_t)operation > 1 ||
       (resource &&
        (resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
         resource->device != list->device || !resource->backend ||
         resource->desc.ResourceType != D3D12DDI_RT_BUFFER ||
         (offset & 7) || offset > resource->desc.Width ||
         sizeof(uint64_t) > resource->desc.Width - offset)) ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->set_predication) {
      d12_umd_query_error(list ? list->device : NULL, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->set_predication(
      list->backend, resource ? resource->backend : NULL, offset,
      (uint32_t)operation);
}

static void APIENTRY
d12_umd_execute_indirect(D3D12DDI_HCOMMANDLIST list_handle,
                         D3D12DDI_HCOMMANDSIGNATURE signature_handle,
                         UINT maximum_count,
                         D3D12DDIARG_BUFFER_PLACEMENT argument_placement,
                         D3D12DDIARG_BUFFER_PLACEMENT count_placement)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_command_signature *signature =
      d12_umd_command_signature_from_handle(signature_handle);
   struct d12_umd_resource *argument =
      argument_placement.BaseAddress.UMD.hResource.pDrvPrivate;
   struct d12_umd_resource *count =
      count_placement.BaseAddress.UMD.hResource.pDrvPrivate;

   if (!maximum_count)
      return;
   if (!list || !list->backend || !signature ||
       signature->device != list->device || !signature->backend ||
       !argument || argument->signature != D12_UMD_RESOURCE_SIGNATURE ||
       argument->device != list->device || !argument->backend ||
       argument->desc.ResourceType != D3D12DDI_RT_BUFFER ||
       (count &&
        (count->signature != D12_UMD_RESOURCE_SIGNATURE ||
         count->device != list->device || !count->backend ||
         count->desc.ResourceType != D3D12DDI_RT_BUFFER)) ||
       (!count && count_placement.BaseAddress.UMD.Offset) ||
       !list->device->renderer_ops ||
       !list->device->renderer_ops->execute_indirect) {
      d12_umd_query_error(list ? list->device : NULL, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->execute_indirect(
      list->backend, signature->backend, maximum_count, argument->backend,
      argument_placement.BaseAddress.UMD.Offset,
      count ? count->backend : NULL, count_placement.BaseAddress.UMD.Offset);
}

void
d12_umd_install_query_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table)
{
   table->pfnCalcPrivateQueryHeapSize =
      d12_umd_calc_private_query_heap_size;
   table->pfnCreateQueryHeap = d12_umd_create_query_heap;
   table->pfnDestroyQueryHeap = d12_umd_destroy_query_heap;
   table->pfnCalcPrivateCommandSignatureSize =
      d12_umd_calc_private_command_signature_size;
   table->pfnCreateCommandSignature = d12_umd_create_command_signature;
   table->pfnDestroyCommandSignature = d12_umd_destroy_command_signature;
}

void
d12_umd_install_query_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table)
{
   table->pfnBeginQuery = d12_umd_begin_query;
   table->pfnEndQuery = d12_umd_end_query;
   table->pfnResolveQueryData = d12_umd_resolve_query_data;
   table->pfnSetPredication = d12_umd_set_predication;
   table->pfnExecuteIndirect = d12_umd_execute_indirect;
}
