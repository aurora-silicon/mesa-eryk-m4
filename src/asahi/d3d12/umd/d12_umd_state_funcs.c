/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 root-signature and root-argument DDI translation. Creation performs
 * the one bounded deep copy needed to cross from WDK-owned structures into
 * the frontend-neutral renderer seam. Command recording is a direct call
 * through the native AGX backend object and allocates no memory.
 */

#include "d12_umd_state_funcs.h"

#include "d12_umd_internal.h"

#include <stdbool.h>
#include <string.h>

#define D12_UMD_MAX_ROOT_PARAMETERS 64u
#define D12_UMD_MAX_ROOT_RANGES 1024u
#define D12_UMD_MAX_STATIC_SAMPLERS 256u
#define D12_UMD_MAX_ROOT_DWORDS 64u
#define D12_UMD_ROOT_SIGNATURE_FLAGS_1_0_MASK 0x7fu

_Static_assert(D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE == 0,
               "pinned DDI descriptor-table encoding changed");
_Static_assert(D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS == 1,
               "pinned DDI root-constants encoding changed");
_Static_assert(D3D12DDI_ROOT_PARAMETER_TYPE_CBV == 2,
               "pinned DDI root-CBV encoding changed");
_Static_assert(D3D12DDI_ROOT_PARAMETER_TYPE_SRV == 3,
               "pinned DDI root-SRV encoding changed");
_Static_assert(D3D12DDI_ROOT_PARAMETER_TYPE_UAV == 4,
               "pinned DDI root-UAV encoding changed");
_Static_assert(D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV == 0,
               "pinned DDI SRV-range encoding changed");
_Static_assert(D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV == 1,
               "pinned DDI UAV-range encoding changed");
_Static_assert(D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV == 2,
               "pinned DDI CBV-range encoding changed");
_Static_assert(D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER == 3,
               "pinned DDI sampler-range encoding changed");

static void
d12_umd_state_set_error(struct d12_umd_command_list *list, HRESULT result)
{
   d12_umd_report_command_list_error(list, result);
}

static bool
d12_umd_root_signature_arguments_valid(
   const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 *arguments,
   uint32_t *out_range_count)
{
   const D3D12DDI_ROOT_SIGNATURE *desc;
   uint32_t range_count = 0;
   uint32_t dword_count = 0;

   if (!arguments || !arguments->pRootSignature ||
       (arguments->NodeMask != 0 && arguments->NodeMask != 1))
      return false;

   desc = arguments->pRootSignature;
   if (desc->NumParameters > D12_UMD_MAX_ROOT_PARAMETERS ||
       desc->NumStaticSamplers > D12_UMD_MAX_STATIC_SAMPLERS ||
       (desc->NumParameters && !desc->pRootParameters) ||
       (desc->NumStaticSamplers && !desc->pStaticSamplers) ||
       ((uint32_t)desc->Flags & ~D12_UMD_ROOT_SIGNATURE_FLAGS_1_0_MASK))
      return false;

   for (UINT index = 0; index < desc->NumParameters; ++index) {
      const D3D12DDI_ROOT_PARAMETER *parameter =
         &desc->pRootParameters[index];

      if ((uint32_t)parameter->ParameterType >
             D3D12DDI_ROOT_PARAMETER_TYPE_UAV ||
          (uint32_t)parameter->ShaderVisibility >
             D3D12DDI_SHADER_VISIBILITY_PIXEL)
         return false;

      switch (parameter->ParameterType) {
      case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
         if ((parameter->DescriptorTable.NumDescriptorRanges &&
              !parameter->DescriptorTable.pDescriptorRanges) ||
             parameter->DescriptorTable.NumDescriptorRanges >
                D12_UMD_MAX_ROOT_RANGES - range_count)
            return false;
         for (UINT range_index = 0;
              range_index < parameter->DescriptorTable.NumDescriptorRanges;
              ++range_index) {
            const D3D12DDI_DESCRIPTOR_RANGE *range =
               &parameter->DescriptorTable.pDescriptorRanges[range_index];
            if ((uint32_t)range->RangeType >
                   D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER ||
                range->NumDescriptors == 0)
               return false;
         }
         range_count += parameter->DescriptorTable.NumDescriptorRanges;
         if (dword_count == D12_UMD_MAX_ROOT_DWORDS)
            return false;
         ++dword_count;
         break;
      case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         if (parameter->Constants.Num32BitValues == 0 ||
             parameter->Constants.Num32BitValues >
                D12_UMD_MAX_ROOT_DWORDS - dword_count)
            return false;
         dword_count += parameter->Constants.Num32BitValues;
         break;
      case D3D12DDI_ROOT_PARAMETER_TYPE_CBV:
      case D3D12DDI_ROOT_PARAMETER_TYPE_SRV:
      case D3D12DDI_ROOT_PARAMETER_TYPE_UAV:
         if (dword_count > D12_UMD_MAX_ROOT_DWORDS - 2u)
            return false;
         dword_count += 2;
         break;
      default:
         return false;
      }
   }

   for (UINT index = 0; index < desc->NumStaticSamplers; ++index) {
      const D3D12DDI_STATIC_SAMPLER *sampler = &desc->pStaticSamplers[index];
      if ((uint32_t)sampler->ShaderVisibility >
             D3D12DDI_SHADER_VISIBILITY_PIXEL ||
          (uint32_t)sampler->BorderColor >
             D3D12DDI_STATIC_BORDER_COLOR_OPAQUE_WHITE)
         return false;
   }

   if (out_range_count)
      *out_range_count = range_count;
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_root_signature_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 *arguments)
{
   return d12_umd_device_from_handle(handle) &&
                 d12_umd_root_signature_arguments_valid(arguments, NULL)
             ? sizeof(struct d12_umd_root_signature)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_root_signature(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 *arguments,
   D3D12DDI_HROOTSIGNATURE root_signature_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_root_signature *root_signature;
   struct d12_frontend_root_parameter *parameters = NULL;
   struct d12_frontend_descriptor_range *ranges = NULL;
   struct d12_frontend_static_sampler *samplers = NULL;
   struct d12_frontend_root_signature_desc translated;
   const D3D12DDI_ROOT_SIGNATURE *desc;
   uint32_t range_count;
   uint32_t next_range = 0;
   HRESULT result;

   if (!device || !root_signature_handle.pDrvPrivate ||
       !d12_umd_root_signature_arguments_valid(arguments, &range_count))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_root_signature ||
       !device->renderer_ops->destroy_root_signature)
      return E_NOTIMPL;

   desc = arguments->pRootSignature;
   if (desc->NumParameters) {
      parameters = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                             (SIZE_T)desc->NumParameters *
                                sizeof(*parameters));
   }
   if (range_count) {
      ranges = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         (SIZE_T)range_count * sizeof(*ranges));
   }
   if (desc->NumStaticSamplers) {
      samplers = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           (SIZE_T)desc->NumStaticSamplers *
                              sizeof(*samplers));
   }
   if ((desc->NumParameters && !parameters) || (range_count && !ranges) ||
       (desc->NumStaticSamplers && !samplers)) {
      result = E_OUTOFMEMORY;
      goto out;
   }

   for (UINT index = 0; index < desc->NumParameters; ++index) {
      const D3D12DDI_ROOT_PARAMETER *source =
         &desc->pRootParameters[index];
      struct d12_frontend_root_parameter *destination = &parameters[index];

      destination->parameter_type = (uint32_t)source->ParameterType;
      destination->shader_visibility = (uint32_t)source->ShaderVisibility;
      switch (source->ParameterType) {
      case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
         destination->table.num_ranges =
            source->DescriptorTable.NumDescriptorRanges;
         destination->table.ranges = source->DescriptorTable.NumDescriptorRanges
                                        ? &ranges[next_range]
                                        : NULL;
         for (UINT range_index = 0;
              range_index < source->DescriptorTable.NumDescriptorRanges;
              ++range_index) {
            const D3D12DDI_DESCRIPTOR_RANGE *source_range =
               &source->DescriptorTable.pDescriptorRanges[range_index];
            ranges[next_range++] = (struct d12_frontend_descriptor_range){
               .range_type = (uint32_t)source_range->RangeType,
               .num_descriptors = source_range->NumDescriptors,
               .base_shader_register = source_range->BaseShaderRegister,
               .register_space = source_range->RegisterSpace,
               .offset_in_descriptors =
                  source_range->OffsetInDescriptorsFromTableStart,
            };
         }
         break;
      case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         destination->constants.shader_register =
            source->Constants.ShaderRegister;
         destination->constants.register_space = source->Constants.RegisterSpace;
         destination->constants.num_32bit_values =
            source->Constants.Num32BitValues;
         break;
      case D3D12DDI_ROOT_PARAMETER_TYPE_CBV:
      case D3D12DDI_ROOT_PARAMETER_TYPE_SRV:
      case D3D12DDI_ROOT_PARAMETER_TYPE_UAV:
         destination->descriptor.shader_register =
            source->Descriptor.ShaderRegister;
         destination->descriptor.register_space =
            source->Descriptor.RegisterSpace;
         break;
      default:
         result = E_INVALIDARG;
         goto out;
      }
   }

   for (UINT index = 0; index < desc->NumStaticSamplers; ++index) {
      const D3D12DDI_STATIC_SAMPLER *source = &desc->pStaticSamplers[index];
      samplers[index] = (struct d12_frontend_static_sampler){
         .filter = (uint32_t)source->Filter,
         .address_u = (uint32_t)source->AddressU,
         .address_v = (uint32_t)source->AddressV,
         .address_w = (uint32_t)source->AddressW,
         .mip_lod_bias = source->MipLODBias,
         .max_anisotropy = source->MaxAnisotropy,
         .comparison_func = (uint32_t)source->ComparisonFunc,
         .border_color = (uint32_t)source->BorderColor,
         .min_lod = source->MinLOD,
         .max_lod = source->MaxLOD,
         .shader_register = source->ShaderRegister,
         .register_space = source->RegisterSpace,
         .shader_visibility = (uint32_t)source->ShaderVisibility,
      };
   }

   translated = (struct d12_frontend_root_signature_desc){
      .num_parameters = desc->NumParameters,
      .parameters = parameters,
      .num_static_samplers = desc->NumStaticSamplers,
      .static_samplers = samplers,
      .flags = (uint32_t)desc->Flags,
   };
   root_signature = root_signature_handle.pDrvPrivate;
   memset(root_signature, 0, sizeof(*root_signature));
   result = device->renderer_ops->create_root_signature(
      device->backend, &translated, &root_signature->backend);
   result = d12_umd_renderer_object_result(
      result, &root_signature->backend,
      device->renderer_ops->destroy_root_signature);
   if (FAILED(result))
      goto out;

   root_signature->signature = D12_UMD_ROOT_SIGNATURE_SIGNATURE;
   root_signature->device = device;
   root_signature->desc = translated;
   root_signature->ranges = ranges;
   parameters = NULL;
   ranges = NULL;
   samplers = NULL;
   result = S_OK;

out:
   if (samplers)
      HeapFree(GetProcessHeap(), 0, samplers);
   if (ranges)
      HeapFree(GetProcessHeap(), 0, ranges);
   if (parameters)
      HeapFree(GetProcessHeap(), 0, parameters);
   return result;
}

static void APIENTRY
d12_umd_destroy_root_signature(D3D12DDI_HDEVICE handle,
                               D3D12DDI_HROOTSIGNATURE signature_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_root_signature *signature =
      d12_umd_root_signature_from_handle(signature_handle);

   if (!device || !signature || signature->device != device)
      return;
   if (signature->backend && device->renderer_ops &&
       device->renderer_ops->destroy_root_signature)
      device->renderer_ops->destroy_root_signature(signature->backend);
   if (signature->desc.static_samplers)
      HeapFree(GetProcessHeap(), 0,
               (void *)signature->desc.static_samplers);
   if (signature->ranges)
      HeapFree(GetProcessHeap(), 0, signature->ranges);
   if (signature->desc.parameters)
      HeapFree(GetProcessHeap(), 0, (void *)signature->desc.parameters);
   SecureZeroMemory(signature, sizeof(*signature));
}

static struct d12_umd_command_list *
d12_umd_root_command_list(D3D12DDI_HCOMMANDLIST handle)
{
   struct d12_umd_command_list *list = d12_umd_command_list_from_handle(handle);

   if (!list || !list->device || !list->backend ||
       !list->device->renderer_ops) {
      d12_umd_state_set_error(list, E_INVALIDARG);
      return NULL;
   }
   return list;
}

static void
d12_umd_set_root_signature(D3D12DDI_HCOMMANDLIST list_handle,
                           D3D12DDI_HROOTSIGNATURE signature_handle,
                           enum d12_frontend_root_pipeline pipeline)
{
   struct d12_umd_command_list *list = d12_umd_root_command_list(list_handle);
   struct d12_umd_root_signature *signature =
      d12_umd_root_signature_from_handle(signature_handle);

   if (!list)
      return;
   if ((signature_handle.pDrvPrivate && !signature) ||
       (signature && signature->device != list->device)) {
      d12_umd_state_set_error(list, E_INVALIDARG);
      return;
   }
   if (!list->device->renderer_ops->set_root_signature) {
      d12_umd_state_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->set_root_signature(
      list->backend, pipeline, signature ? signature->backend : NULL);
}

static void APIENTRY
d12_umd_set_compute_root_signature(D3D12DDI_HCOMMANDLIST list,
                                   D3D12DDI_HROOTSIGNATURE signature)
{
   d12_umd_set_root_signature(list, signature, D12_FRONTEND_ROOT_COMPUTE);
}

static void APIENTRY
d12_umd_set_graphics_root_signature(D3D12DDI_HCOMMANDLIST list,
                                    D3D12DDI_HROOTSIGNATURE signature)
{
   d12_umd_set_root_signature(list, signature, D12_FRONTEND_ROOT_GRAPHICS);
}

static void
d12_umd_set_root_descriptor_table(
   D3D12DDI_HCOMMANDLIST list_handle, UINT parameter_index,
   D3D12DDI_GPU_DESCRIPTOR_HANDLE base_descriptor,
   enum d12_frontend_root_pipeline pipeline)
{
   struct d12_umd_command_list *list = d12_umd_root_command_list(list_handle);

   if (!list)
      return;
   if (!list->device->renderer_ops->set_root_descriptor_table) {
      d12_umd_state_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->set_root_descriptor_table(
      list->backend, pipeline, parameter_index, base_descriptor.ptr);
}

static void APIENTRY
d12_umd_set_compute_root_descriptor_table(
   D3D12DDI_HCOMMANDLIST list, UINT parameter_index,
   D3D12DDI_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
   d12_umd_set_root_descriptor_table(list, parameter_index, base_descriptor,
                                     D12_FRONTEND_ROOT_COMPUTE);
}

static void APIENTRY
d12_umd_set_graphics_root_descriptor_table(
   D3D12DDI_HCOMMANDLIST list, UINT parameter_index,
   D3D12DDI_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
   d12_umd_set_root_descriptor_table(list, parameter_index, base_descriptor,
                                     D12_FRONTEND_ROOT_GRAPHICS);
}

static void
d12_umd_set_root_constants(D3D12DDI_HCOMMANDLIST list_handle,
                           UINT parameter_index, UINT count,
                           const void *data, UINT destination_offset,
                           enum d12_frontend_root_pipeline pipeline)
{
   struct d12_umd_command_list *list = d12_umd_root_command_list(list_handle);

   if (!list)
      return;
   if ((count && !data) ||
       !list->device->renderer_ops->set_root_constants) {
      d12_umd_state_set_error(
         list, (count && !data) ? E_INVALIDARG : E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->set_root_constants(
      list->backend, pipeline, parameter_index, count, data,
      destination_offset);
}

static void APIENTRY
d12_umd_set_compute_root_constant(D3D12DDI_HCOMMANDLIST list,
                                  UINT parameter_index, UINT data,
                                  UINT destination_offset)
{
   d12_umd_set_root_constants(list, parameter_index, 1, &data,
                              destination_offset,
                              D12_FRONTEND_ROOT_COMPUTE);
}

static void APIENTRY
d12_umd_set_graphics_root_constant(D3D12DDI_HCOMMANDLIST list,
                                   UINT parameter_index, UINT data,
                                   UINT destination_offset)
{
   d12_umd_set_root_constants(list, parameter_index, 1, &data,
                              destination_offset,
                              D12_FRONTEND_ROOT_GRAPHICS);
}

static void APIENTRY
d12_umd_set_compute_root_constants(D3D12DDI_HCOMMANDLIST list,
                                   UINT parameter_index, UINT count,
                                   const void *data, UINT destination_offset)
{
   d12_umd_set_root_constants(list, parameter_index, count, data,
                              destination_offset,
                              D12_FRONTEND_ROOT_COMPUTE);
}

static void APIENTRY
d12_umd_set_graphics_root_constants(D3D12DDI_HCOMMANDLIST list,
                                    UINT parameter_index, UINT count,
                                    const void *data, UINT destination_offset)
{
   d12_umd_set_root_constants(list, parameter_index, count, data,
                              destination_offset,
                              D12_FRONTEND_ROOT_GRAPHICS);
}

static void
d12_umd_set_root_buffer_view(
   D3D12DDI_HCOMMANDLIST list_handle, UINT parameter_index,
   D3D12DDI_GPU_VIRTUAL_ADDRESS address,
   enum d12_frontend_root_pipeline pipeline,
   enum d12_frontend_root_buffer_type buffer_type)
{
   struct d12_umd_command_list *list = d12_umd_root_command_list(list_handle);

   if (!list)
      return;
   if (!list->device->renderer_ops->set_root_buffer_view) {
      d12_umd_state_set_error(list, E_NOTIMPL);
      return;
   }
   list->device->renderer_ops->set_root_buffer_view(
      list->backend, pipeline, buffer_type, parameter_index, address);
}

#define D12_UMD_DEFINE_ROOT_BUFFER_VIEW(name, pipeline, type)                 \
   static void APIENTRY name(D3D12DDI_HCOMMANDLIST list,                     \
                             UINT parameter_index,                           \
                             D3D12DDI_GPU_VIRTUAL_ADDRESS address)           \
   {                                                                          \
      d12_umd_set_root_buffer_view(list, parameter_index, address, pipeline,  \
                                   type);                                     \
   }

D12_UMD_DEFINE_ROOT_BUFFER_VIEW(d12_umd_set_compute_root_cbv,
                                D12_FRONTEND_ROOT_COMPUTE,
                                D12_FRONTEND_ROOT_CBV)
D12_UMD_DEFINE_ROOT_BUFFER_VIEW(d12_umd_set_graphics_root_cbv,
                                D12_FRONTEND_ROOT_GRAPHICS,
                                D12_FRONTEND_ROOT_CBV)
D12_UMD_DEFINE_ROOT_BUFFER_VIEW(d12_umd_set_compute_root_srv,
                                D12_FRONTEND_ROOT_COMPUTE,
                                D12_FRONTEND_ROOT_SRV)
D12_UMD_DEFINE_ROOT_BUFFER_VIEW(d12_umd_set_graphics_root_srv,
                                D12_FRONTEND_ROOT_GRAPHICS,
                                D12_FRONTEND_ROOT_SRV)
D12_UMD_DEFINE_ROOT_BUFFER_VIEW(d12_umd_set_compute_root_uav,
                                D12_FRONTEND_ROOT_COMPUTE,
                                D12_FRONTEND_ROOT_UAV)
D12_UMD_DEFINE_ROOT_BUFFER_VIEW(d12_umd_set_graphics_root_uav,
                                D12_FRONTEND_ROOT_GRAPHICS,
                                D12_FRONTEND_ROOT_UAV)

void
d12_umd_install_state_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table)
{
   table->pfnCalcPrivateRootSignatureSize =
      d12_umd_calc_private_root_signature_size;
   table->pfnCreateRootSignature = d12_umd_create_root_signature;
   table->pfnDestroyRootSignature = d12_umd_destroy_root_signature;
}

void
d12_umd_install_state_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table)
{
   table->pfnSetComputeRootSignature = d12_umd_set_compute_root_signature;
   table->pfnSetGraphicsRootSignature = d12_umd_set_graphics_root_signature;
   table->pfnSetComputeRootDescriptorTable =
      d12_umd_set_compute_root_descriptor_table;
   table->pfnSetGraphicsRootDescriptorTable =
      d12_umd_set_graphics_root_descriptor_table;
   table->pfnSetComputeRoot32BitConstant = d12_umd_set_compute_root_constant;
   table->pfnSetGraphicsRoot32BitConstant = d12_umd_set_graphics_root_constant;
   table->pfnSetComputeRoot32BitConstants = d12_umd_set_compute_root_constants;
   table->pfnSetGraphicsRoot32BitConstants =
      d12_umd_set_graphics_root_constants;
   table->pfnSetComputeRootConstantBufferView = d12_umd_set_compute_root_cbv;
   table->pfnSetGraphicsRootConstantBufferView =
      d12_umd_set_graphics_root_cbv;
   table->pfnSetComputeRootShaderResourceView = d12_umd_set_compute_root_srv;
   table->pfnSetGraphicsRootShaderResourceView =
      d12_umd_set_graphics_root_srv;
   table->pfnSetComputeRootUnorderedAccessView = d12_umd_set_compute_root_uav;
   table->pfnSetGraphicsRootUnorderedAccessView =
      d12_umd_set_graphics_root_uav;
}
