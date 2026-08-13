/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * DDI shader objects retain a standards-shaped DXBC container built from
 * the runtime's raw token stream and explicit stage signatures. Compilation
 * remains deferred until PSO creation, matching D3D12's immutable pipeline
 * model and the existing AGX compiler cache boundary.
 */

#include "d12_umd_pipeline_funcs.h"

#include "../d12_dxbc_wrap.h"
#include "d12_umd_internal.h"
#include "d12_umd_pipeline_serialization.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void
d12_umd_pipeline_set_error(struct d12_umd_device *device, HRESULT result)
{
   if (device && device->core_callbacks.pfnSetErrorCb)
      device->core_callbacks.pfnSetErrorCb(device->runtime_device, result);
}

static bool
d12_umd_element_layout_valid(
   const D3D12DDIARG_CREATEELEMENTLAYOUT *arguments)
{
   if (!arguments || arguments->NumElements > D12_FRONTEND_MAX_INPUT_ELEMENTS ||
       (arguments->NumElements && !arguments->pVertexElements))
      return false;

   for (UINT index = 0; index < arguments->NumElements; ++index) {
      const D3D12DDIARG_INPUT_ELEMENT_DESC *element =
         &arguments->pVertexElements[index];
      if (element->InputSlot >= 32 || element->InputRegister >= 32 ||
          (uint32_t)element->InputSlotClass > 1 ||
          (element->InputSlotClass ==
              D3D12DDI_INPUT_CLASSIFICIATION_PER_VERTEX_DATA &&
           element->InstanceDataStepRate != 0))
         return false;
   }
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_element_layout_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEELEMENTLAYOUT *arguments)
{
   if (!d12_umd_device_from_handle(handle) ||
       !d12_umd_element_layout_valid(arguments))
      return 0;
   return sizeof(struct d12_umd_element_layout) +
          (SIZE_T)arguments->NumElements *
             sizeof(struct d12_frontend_input_element);
}

static void APIENTRY
d12_umd_create_element_layout(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEELEMENTLAYOUT *arguments,
   D3D12DDI_HELEMENTLAYOUT layout_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_element_layout *layout = layout_handle.pDrvPrivate;

   if (!device || !layout || !d12_umd_element_layout_valid(arguments)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }

   layout->signature = D12_UMD_ELEMENT_LAYOUT_SIGNATURE;
   layout->device = device;
   layout->count = arguments->NumElements;
   for (UINT index = 0; index < arguments->NumElements; ++index) {
      const D3D12DDIARG_INPUT_ELEMENT_DESC *source =
         &arguments->pVertexElements[index];
      layout->elements[index] = (struct d12_frontend_input_element){
         .input_slot = source->InputSlot,
         .aligned_byte_offset = source->AlignedByteOffset,
         .format = (uint32_t)source->Format,
         .input_slot_class = (uint32_t)source->InputSlotClass,
         .instance_data_step_rate = source->InstanceDataStepRate,
         .input_register = source->InputRegister,
      };
   }
}

static void APIENTRY
d12_umd_destroy_element_layout(D3D12DDI_HDEVICE handle,
                               D3D12DDI_HELEMENTLAYOUT layout_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_element_layout *layout =
      d12_umd_element_layout_from_handle(layout_handle);

   if (!device || !layout || layout->device != device)
      return;
   SecureZeroMemory(layout, sizeof(*layout) +
                               (SIZE_T)layout->count * sizeof(layout->elements[0]));
}

static void
d12_umd_translate_blend_desc(const D3D12DDI_BLEND_DESC *source,
                             struct d12_frontend_blend_desc *destination)
{
   memset(destination, 0, sizeof(*destination));
   destination->alpha_to_coverage_enable =
      source->AlphaToCoverageEnable ? 1u : 0u;
   destination->independent_blend_enable =
      source->IndependentBlendEnable ? 1u : 0u;
   for (UINT index = 0; index < D12_FRONTEND_MAX_RENDER_TARGETS; ++index) {
      const D3D12DDI_RENDER_TARGET_BLEND_DESC *src =
         &source->RenderTarget[index];
      struct d12_frontend_render_target_blend *dst =
         &destination->render_targets[index];
      *dst = (struct d12_frontend_render_target_blend){
         .blend_enable = src->BlendEnable ? 1u : 0u,
         .logic_op_enable = src->LogicOpEnable ? 1u : 0u,
         .src_blend = (uint32_t)src->SrcBlend,
         .dest_blend = (uint32_t)src->DestBlend,
         .blend_op = (uint32_t)src->BlendOp,
         .src_blend_alpha = (uint32_t)src->SrcBlendAlpha,
         .dest_blend_alpha = (uint32_t)src->DestBlendAlpha,
         .blend_op_alpha = (uint32_t)src->BlendOpAlpha,
         .logic_op = (uint32_t)src->LogicOp,
         .write_mask = src->RenderTargetWriteMask,
      };
   }
}

static SIZE_T APIENTRY
d12_umd_calc_private_blend_state_size(D3D12DDI_HDEVICE handle,
                                      const D3D12DDI_BLEND_DESC *desc)
{
   return d12_umd_device_from_handle(handle) && desc
             ? sizeof(struct d12_umd_blend_state)
             : 0;
}

static void APIENTRY
d12_umd_create_blend_state(D3D12DDI_HDEVICE handle,
                           const D3D12DDI_BLEND_DESC *desc,
                           D3D12DDI_HBLENDSTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_blend_state *state = state_handle.pDrvPrivate;

   if (!device || !desc || !state) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   memset(state, 0, sizeof(*state));
   state->signature = D12_UMD_BLEND_STATE_SIGNATURE;
   state->device = device;
   d12_umd_translate_blend_desc(desc, &state->desc);
}

static void APIENTRY
d12_umd_destroy_blend_state(D3D12DDI_HDEVICE handle,
                            D3D12DDI_HBLENDSTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_blend_state *state =
      d12_umd_blend_state_from_handle(state_handle);
   if (device && state && state->device == device)
      SecureZeroMemory(state, sizeof(*state));
}

static struct d12_frontend_depth_stencil_op
d12_umd_translate_depth_stencil_op(
   const D3D12DDI_DEPTH_STENCILOP_DESC *source)
{
   return (struct d12_frontend_depth_stencil_op){
      .stencil_fail_op = (uint32_t)source->StencilFailOp,
      .stencil_depth_fail_op = (uint32_t)source->StencilDepthFailOp,
      .stencil_pass_op = (uint32_t)source->StencilPassOp,
      .stencil_func = (uint32_t)source->StencilFunc,
   };
}

static SIZE_T APIENTRY
d12_umd_calc_private_depth_stencil_state_size(
   D3D12DDI_HDEVICE handle, const D3D12DDI_DEPTH_STENCIL_DESC *desc)
{
   return d12_umd_device_from_handle(handle) && desc
             ? sizeof(struct d12_umd_depth_stencil_state)
             : 0;
}

static void APIENTRY
d12_umd_create_depth_stencil_state(
   D3D12DDI_HDEVICE handle, const D3D12DDI_DEPTH_STENCIL_DESC *desc,
   D3D12DDI_HDEPTHSTENCILSTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_depth_stencil_state *state = state_handle.pDrvPrivate;

   if (!device || !desc || !state) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   memset(state, 0, sizeof(*state));
   state->signature = D12_UMD_DEPTH_STENCIL_STATE_SIGNATURE;
   state->device = device;
   state->desc = (struct d12_frontend_depth_stencil_desc){
      .depth_enable = desc->DepthEnable ? 1u : 0u,
      .depth_write_mask = (uint32_t)desc->DepthWriteMask,
      .depth_func = (uint32_t)desc->DepthFunc,
      .stencil_enable = desc->StencilEnable ? 1u : 0u,
      .front_enable = desc->FrontEnable ? 1u : 0u,
      .back_enable = desc->BackEnable ? 1u : 0u,
      .front_face_stencil_read_mask = desc->StencilReadMask,
      .front_face_stencil_write_mask = desc->StencilWriteMask,
      .back_face_stencil_read_mask = desc->StencilReadMask,
      .back_face_stencil_write_mask = desc->StencilWriteMask,
      .front_face = d12_umd_translate_depth_stencil_op(&desc->FrontFace),
      .back_face = d12_umd_translate_depth_stencil_op(&desc->BackFace),
   };
}

static void APIENTRY
d12_umd_destroy_depth_stencil_state(
   D3D12DDI_HDEVICE handle, D3D12DDI_HDEPTHSTENCILSTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_depth_stencil_state *state =
      d12_umd_depth_stencil_state_from_handle(state_handle);
   if (device && state && state->device == device)
      SecureZeroMemory(state, sizeof(*state));
}

static SIZE_T APIENTRY
d12_umd_calc_private_rasterizer_state_size(
   D3D12DDI_HDEVICE handle, const D3D12DDI_RASTERIZER_DESC *desc)
{
   return d12_umd_device_from_handle(handle) && desc
             ? sizeof(struct d12_umd_rasterizer_state)
             : 0;
}

static void APIENTRY
d12_umd_create_rasterizer_state(D3D12DDI_HDEVICE handle,
                                const D3D12DDI_RASTERIZER_DESC *desc,
                                D3D12DDI_HRASTERIZERSTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_rasterizer_state *state = state_handle.pDrvPrivate;

   if (!device || !desc || !state) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   memset(state, 0, sizeof(*state));
   state->signature = D12_UMD_RASTERIZER_STATE_SIGNATURE;
   state->device = device;
   state->desc = (struct d12_frontend_rasterizer_desc){
      .fill_mode = (uint32_t)desc->FillMode,
      .cull_mode = (uint32_t)desc->CullMode,
      .front_counter_clockwise = desc->FrontCounterClockwise ? 1u : 0u,
      .depth_bias = (float)desc->DepthBias,
      .depth_bias_clamp = desc->DepthBiasClamp,
      .slope_scaled_depth_bias = desc->SlopeScaledDepthBias,
      .depth_clip_enable = desc->DepthClipEnable ? 1u : 0u,
      .scissor_enable = desc->ScissorEnable ? 1u : 0u,
      .multisample_enable = desc->MultisampleEnable ? 1u : 0u,
      .antialiased_line_enable = desc->AntialiasedLineEnable ? 1u : 0u,
      .forced_sample_count = desc->ForcedSampleCount,
      .conservative_rasterization_mode =
         (uint32_t)desc->ConservativeRasterizationMode,
   };
}

static void APIENTRY
d12_umd_destroy_rasterizer_state(
   D3D12DDI_HDEVICE handle, D3D12DDI_HRASTERIZERSTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_rasterizer_state *state =
      d12_umd_rasterizer_state_from_handle(state_handle);
   if (device && state && state->device == device)
      SecureZeroMemory(state, sizeof(*state));
}

static bool
d12_umd_shader_root_valid(struct d12_umd_device *device,
                          D3D12DDI_HROOTSIGNATURE handle)
{
   struct d12_umd_root_signature *root_signature;

   if (!handle.pDrvPrivate)
      return true;
   root_signature = d12_umd_root_signature_from_handle(handle);
   return root_signature && root_signature->device == device;
}

static bool
d12_umd_translate_signature(
   const void *source, UINT count,
   bool has_stream,
   struct d12_frontend_signature_entry *destination,
   struct d12_frontend_signature *translated)
{
   if (!translated || count > D12_FRONTEND_MAX_SIGNATURE_ENTRIES ||
       (count && (!source || !destination)))
      return false;

   for (UINT index = 0; index < count; ++index) {
      D3D12DDIARG_SIGNATURE_ENTRY_0012 entry;
      memcpy(&entry,
             (const uint8_t *)source + (size_t)index * sizeof(entry),
             sizeof(entry));

      if ((uint32_t)entry.SystemValue > 22 ||
          entry.Register >= D12_FRONTEND_MAX_SIGNATURE_ENTRIES ||
          entry.Mask == 0 || entry.Mask > 0xf ||
          (uint32_t)entry.RegisterComponentType > 3 ||
          ((uint32_t)entry.MinPrecision != 0 &&
           (uint32_t)entry.MinPrecision != 1 &&
           (uint32_t)entry.MinPrecision != 2 &&
           (uint32_t)entry.MinPrecision != 4 &&
           (uint32_t)entry.MinPrecision != 5))
         return false;

      const uint32_t stream = has_stream ? entry.Stream : 0;
      if (stream >= D12_FRONTEND_MAX_SIGNATURE_STREAMS)
         return false;

      destination[index] = (struct d12_frontend_signature_entry){
         .system_value = (uint32_t)entry.SystemValue,
         .register_index = entry.Register,
         .mask = entry.Mask,
         .stream = stream,
         .component_type = (uint32_t)entry.RegisterComponentType,
         .min_precision = (uint32_t)entry.MinPrecision,
      };
   }
   translated->count = count;
   translated->entries = count ? destination : NULL;
   return true;
}

static bool
d12_umd_device_has_signature_streams(const struct d12_umd_device *device)
{
   return device &&
          device->interface_version == D3D12DDI_INTERFACE_VERSION_R1 &&
          device->runtime_version >= D3D12DDI_BUILD_VERSION_0012;
}

static bool
d12_umd_translate_stage_signatures(
   const struct d12_umd_device *device,
   const D3D12DDIARG_STAGE_IO_SIGNATURES *source,
   struct d12_frontend_shader_signatures *translated,
   struct d12_frontend_signature_entry *input,
   struct d12_frontend_signature_entry *output)
{
   memset(translated, 0, sizeof(*translated));
   if (!source)
      return true;
   const bool has_stream = d12_umd_device_has_signature_streams(device);
   return d12_umd_translate_signature(
             source->pInputSignatureDeprecated,
             source->NumInputSignatureEntries, has_stream, input,
             &translated->input) &&
          d12_umd_translate_signature(
             source->pOutputSignatureDeprecated,
             source->NumOutputSignatureEntries, has_stream, output,
             &translated->output);
}

static bool
d12_umd_translate_stream_output(
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT *arguments,
   const struct d12_frontend_signature *output_signature,
   struct d12_frontend_stream_output_declaration *translated)
{
   bool has_output = false;

   if (!arguments || !output_signature ||
       arguments->NumEntries > D12_FRONTEND_MAX_STREAM_OUTPUT_DECLARATIONS ||
       arguments->NumStrides > D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS ||
       (arguments->NumEntries &&
        (!arguments->pOutputStreamDecl || !translated)) ||
       (arguments->NumStrides && !arguments->BufferStridesInBytes) ||
       (arguments->RasterizedStream != UINT32_MAX &&
        arguments->RasterizedStream >= D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS))
      return false;

   for (UINT index = 0; index < arguments->NumStrides; ++index) {
      if ((arguments->BufferStridesInBytes[index] & 3u) ||
          arguments->BufferStridesInBytes[index] > UINT16_MAX)
         return false;
   }

   for (UINT index = 0; index < arguments->NumEntries; ++index) {
      const D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *source =
         &arguments->pOutputStreamDecl[index];
      const struct d12_frontend_signature_entry *signature = NULL;
      uint32_t mask = source->RegisterMask;
      uint32_t start_component = 0;
      uint32_t component_count = 0;

      if (source->Stream >= D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS ||
          source->OutputSlot >= arguments->NumStrides || !mask ||
          (mask & ~UINT32_C(0xf)))
         return false;
      while (!(mask & 1u)) {
         mask >>= 1;
         ++start_component;
      }
      while (mask & 1u) {
         mask >>= 1;
         ++component_count;
      }
      if (mask || !component_count)
         return false;

      if (source->RegisterIndex == UINT32_MAX) {
         translated[index] =
            (struct d12_frontend_stream_output_declaration){
               .stream = source->Stream,
               .output_slot = source->OutputSlot,
               .register_index = UINT32_MAX,
               .start_component = start_component,
               .component_count = component_count,
            };
         continue;
      }

      for (uint32_t entry = 0; entry < output_signature->count; ++entry) {
         if (output_signature->entries[entry].stream == source->Stream &&
             output_signature->entries[entry].register_index ==
                source->RegisterIndex) {
            signature = &output_signature->entries[entry];
            break;
         }
      }
      if (!signature ||
          (source->RegisterMask & ~signature->mask) != 0)
         return false;
      has_output = true;

      translated[index] =
         (struct d12_frontend_stream_output_declaration){
            .stream = source->Stream,
            .output_slot = source->OutputSlot,
            .register_index = source->RegisterIndex,
            .system_value = signature->system_value,
            .start_component = start_component,
            .component_count = component_count,
         };
   }
   return has_output;
}

static bool
d12_umd_translate_tessellation_signatures(
   const struct d12_umd_device *device,
   const D3D12DDIARG_TESSELLATION_IO_SIGNATURES *source,
   struct d12_frontend_shader_signatures *translated,
   struct d12_frontend_signature_entry *input,
   struct d12_frontend_signature_entry *output,
   struct d12_frontend_signature_entry *patch)
{
   memset(translated, 0, sizeof(*translated));
   if (!source)
      return true;
   const bool has_stream = d12_umd_device_has_signature_streams(device);
   return d12_umd_translate_signature(
             source->pInputSignatureDeprecated,
             source->NumInputSignatureEntries, has_stream, input,
             &translated->input) &&
          d12_umd_translate_signature(
             source->pOutputSignatureDeprecated,
             source->NumOutputSignatureEntries, has_stream, output,
             &translated->output) &&
          d12_umd_translate_signature(
             source->pPatchConstantSignatureDeprecated,
             source->NumPatchConstantSignatureEntries, has_stream, patch,
             &translated->patch);
}

static SIZE_T APIENTRY
d12_umd_calc_private_shader_size(
   D3D12DDI_HDEVICE handle, const UINT *tokens,
   D3D12DDI_HROOTSIGNATURE root_signature,
   const D3D12DDIARG_STAGE_IO_SIGNATURES *signatures)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_frontend_signature_entry
      input[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_signature_entry
      output[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_shader_signatures translated;

   return device && d12_dxbc_shader_valid(tokens, UINT32_MAX, NULL) &&
                 d12_umd_shader_root_valid(device, root_signature) &&
                 d12_umd_translate_stage_signatures(
                    device, signatures, &translated, input, output)
             ? sizeof(struct d12_umd_shader)
             : 0;
}

static SIZE_T APIENTRY
d12_umd_calc_private_geometry_shader_with_stream_output_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT *arguments,
   const D3D12DDIARG_STAGE_IO_SIGNATURES *signatures)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_frontend_signature_entry
      input[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_signature_entry
      output[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_shader_signatures translated_signatures;
   struct d12_frontend_stream_output_declaration
      translated_so[D12_FRONTEND_MAX_STREAM_OUTPUT_DECLARATIONS];

   return device && arguments &&
                 (arguments->pShaderCode == NULL ||
                  d12_dxbc_shader_valid(arguments->pShaderCode,
                                        D12_FRONTEND_SHADER_GEOMETRY,
                                        NULL)) &&
                 arguments->NumEntries != 0 &&
                 d12_umd_shader_root_valid(device,
                                           arguments->hRootSignature) &&
                 d12_umd_translate_stage_signatures(
                    device, signatures, &translated_signatures, input,
                    output) &&
                 d12_umd_translate_stream_output(
                    arguments, &translated_signatures.output, translated_so)
             ? sizeof(struct d12_umd_shader)
             : 0;
}

static SIZE_T APIENTRY
d12_umd_calc_private_tessellation_shader_size(
   D3D12DDI_HDEVICE handle, const UINT *tokens,
   D3D12DDI_HROOTSIGNATURE root_signature,
   const D3D12DDIARG_TESSELLATION_IO_SIGNATURES *signatures)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_frontend_signature_entry
      input[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_signature_entry
      output[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_signature_entry
      patch[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_shader_signatures translated;

   return device && d12_dxbc_shader_valid(tokens, UINT32_MAX, NULL) &&
                 d12_umd_shader_root_valid(device, root_signature) &&
                 d12_umd_translate_tessellation_signatures(
                    device, signatures, &translated, input, output, patch)
             ? sizeof(struct d12_umd_shader)
             : 0;
}

static void
d12_umd_create_shader(
   D3D12DDI_HDEVICE handle, const UINT *tokens,
   D3D12DDI_HROOTSIGNATURE root_signature, D3D12DDI_HSHADER shader_handle,
   const D3D12DDIARG_STAGE_IO_SIGNATURES *signatures,
   D3D12DDI_CREATE_SHADER_FLAGS flags,
   enum d12_frontend_shader_stage stage)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_frontend_signature_entry
      input[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_signature_entry
      output[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_shader_signatures translated;
   struct d12_umd_shader *shader;

   if (!device || !shader_handle.pDrvPrivate ||
       flags != D3D12DDI_CREATE_SHADER_FLAG_NONE ||
       !d12_dxbc_shader_valid(tokens, (uint32_t)stage, NULL) ||
       !d12_umd_shader_root_valid(device, root_signature) ||
       !d12_umd_translate_stage_signatures(device, signatures, &translated,
                                           input, output)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }

   shader = shader_handle.pDrvPrivate;
   memset(shader, 0, sizeof(*shader));
   if (!d12_dxbc_import_shader(tokens, (uint32_t)stage, &translated,
                               &shader->dxbc, &shader->dxbc_size)) {
      d12_umd_pipeline_set_error(device, E_OUTOFMEMORY);
      return;
   }
   shader->signature = D12_UMD_SHADER_SIGNATURE;
   shader->device = device;
   shader->stage = stage;
}

#define D12_UMD_DEFINE_SHADER_CREATE(name, stage)                            \
   static void APIENTRY name(                                                \
      D3D12DDI_HDEVICE device, const UINT *tokens,                           \
      D3D12DDI_HROOTSIGNATURE root_signature, D3D12DDI_HSHADER shader,       \
      const D3D12DDIARG_STAGE_IO_SIGNATURES *signatures,                     \
      D3D12DDI_CREATE_SHADER_FLAGS flags)                                    \
   {                                                                          \
      d12_umd_create_shader(device, tokens, root_signature, shader,           \
                            signatures, flags, stage);                        \
   }

D12_UMD_DEFINE_SHADER_CREATE(d12_umd_create_vertex_shader,
                             D12_FRONTEND_SHADER_VERTEX)
D12_UMD_DEFINE_SHADER_CREATE(d12_umd_create_pixel_shader,
                             D12_FRONTEND_SHADER_PIXEL)
D12_UMD_DEFINE_SHADER_CREATE(d12_umd_create_geometry_shader,
                             D12_FRONTEND_SHADER_GEOMETRY)

static void APIENTRY
d12_umd_create_geometry_shader_with_stream_output(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT *arguments,
   D3D12DDI_HSHADER shader_handle,
   const D3D12DDIARG_STAGE_IO_SIGNATURES *signatures,
   D3D12DDI_CREATE_SHADER_FLAGS flags)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_frontend_signature_entry
      input[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_signature_entry
      output[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_shader_signatures translated_signatures;
   struct d12_umd_shader *shader;

   if (!device || !arguments || !shader_handle.pDrvPrivate ||
       flags != D3D12DDI_CREATE_SHADER_FLAG_NONE ||
       !d12_umd_calc_private_geometry_shader_with_stream_output_size(
          handle, arguments, signatures) ||
       !d12_umd_translate_stage_signatures(
          device, signatures, &translated_signatures, input, output)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }

   shader = shader_handle.pDrvPrivate;
   memset(shader, 0, sizeof(*shader));
   shader->so_declarations = calloc(arguments->NumEntries,
                                    sizeof(*shader->so_declarations));
   shader->so_strides = calloc(arguments->NumStrides,
                               sizeof(*shader->so_strides));
   if (!shader->so_declarations ||
       (arguments->NumStrides && !shader->so_strides) ||
       !d12_umd_translate_stream_output(
          arguments, &translated_signatures.output,
          shader->so_declarations) ||
       (arguments->pShaderCode &&
       !d12_dxbc_import_shader(arguments->pShaderCode,
                               D12_FRONTEND_SHADER_GEOMETRY,
                               &translated_signatures, &shader->dxbc,
                               &shader->dxbc_size))) {
      free(shader->so_declarations);
      free(shader->so_strides);
      free(shader->dxbc);
      memset(shader, 0, sizeof(*shader));
      d12_umd_pipeline_set_error(device, E_OUTOFMEMORY);
      return;
   }
   if (arguments->NumStrides) {
      memcpy(shader->so_strides, arguments->BufferStridesInBytes,
             arguments->NumStrides * sizeof(*shader->so_strides));
   }
   shader->signature = D12_UMD_SHADER_SIGNATURE;
   shader->device = device;
   shader->stage = D12_FRONTEND_SHADER_GEOMETRY;
   shader->so_declaration_count = arguments->NumEntries;
   shader->so_stride_count = arguments->NumStrides;
   shader->so_rasterized_stream = arguments->RasterizedStream;
}

static void APIENTRY
d12_umd_create_compute_shader(D3D12DDI_HDEVICE device, const UINT *tokens,
                              D3D12DDI_HROOTSIGNATURE root_signature,
                              D3D12DDI_HSHADER shader,
                              D3D12DDI_CREATE_SHADER_FLAGS flags)
{
   d12_umd_create_shader(device, tokens, root_signature, shader, NULL, flags,
                         D12_FRONTEND_SHADER_COMPUTE);
}

static void
d12_umd_create_tessellation_shader(
   D3D12DDI_HDEVICE handle, const UINT *tokens,
   D3D12DDI_HROOTSIGNATURE root_signature, D3D12DDI_HSHADER shader_handle,
   const D3D12DDIARG_TESSELLATION_IO_SIGNATURES *signatures,
   D3D12DDI_CREATE_SHADER_FLAGS flags,
   enum d12_frontend_shader_stage stage)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_frontend_signature_entry
      input[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_signature_entry
      output[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_signature_entry
      patch[D12_FRONTEND_MAX_SIGNATURE_ENTRIES];
   struct d12_frontend_shader_signatures translated;
   struct d12_umd_shader *shader;

   if (!device || !shader_handle.pDrvPrivate ||
       flags != D3D12DDI_CREATE_SHADER_FLAG_NONE ||
       !d12_dxbc_shader_valid(tokens, (uint32_t)stage, NULL) ||
       !d12_umd_shader_root_valid(device, root_signature) ||
       !d12_umd_translate_tessellation_signatures(
          device, signatures, &translated, input, output, patch)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }

   shader = shader_handle.pDrvPrivate;
   memset(shader, 0, sizeof(*shader));
   if (!d12_dxbc_import_shader(tokens, (uint32_t)stage, &translated,
                               &shader->dxbc, &shader->dxbc_size)) {
      d12_umd_pipeline_set_error(device, E_OUTOFMEMORY);
      return;
   }
   shader->signature = D12_UMD_SHADER_SIGNATURE;
   shader->device = device;
   shader->stage = stage;
}

#define D12_UMD_DEFINE_TESS_SHADER_CREATE(name, stage)                       \
   static void APIENTRY name(                                                \
      D3D12DDI_HDEVICE device, const UINT *tokens,                           \
      D3D12DDI_HROOTSIGNATURE root_signature, D3D12DDI_HSHADER shader,       \
      const D3D12DDIARG_TESSELLATION_IO_SIGNATURES *signatures,              \
      D3D12DDI_CREATE_SHADER_FLAGS flags)                                    \
   {                                                                          \
      d12_umd_create_tessellation_shader(                                    \
         device, tokens, root_signature, shader, signatures, flags, stage);  \
   }

D12_UMD_DEFINE_TESS_SHADER_CREATE(d12_umd_create_hull_shader,
                                  D12_FRONTEND_SHADER_HULL)
D12_UMD_DEFINE_TESS_SHADER_CREATE(d12_umd_create_domain_shader,
                                  D12_FRONTEND_SHADER_DOMAIN)

static void APIENTRY
d12_umd_destroy_shader(D3D12DDI_HDEVICE handle,
                       D3D12DDI_HSHADER shader_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_shader *shader = d12_umd_shader_from_handle(shader_handle);

   if (!device || !shader || shader->device != device)
      return;
   free(shader->dxbc);
   free(shader->so_declarations);
   free(shader->so_strides);
   SecureZeroMemory(shader, sizeof(*shader));
}

static struct d12_umd_shader *
d12_umd_pipeline_shader(struct d12_umd_device *device,
                        D3D12DDI_HSHADER handle,
                        enum d12_frontend_shader_stage expected_stage)
{
   struct d12_umd_shader *shader = d12_umd_shader_from_handle(handle);
   return shader && shader->device == device && shader->stage == expected_stage
             ? shader
             : NULL;
}

static bool
d12_umd_pipeline_arguments_valid(
   struct d12_umd_device *device,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arguments,
   bool require_output)
{
   struct d12_umd_shader *compute;
   struct d12_umd_shader *vertex;
   struct d12_umd_shader *pixel;
   struct d12_umd_shader *geometry;
   struct d12_umd_shader *hull;
   struct d12_umd_shader *domain;
   struct d12_umd_root_signature *root_signature;
   struct d12_umd_blend_state *blend;
   struct d12_umd_rasterizer_state *rasterizer;
   struct d12_umd_depth_stencil_state *depth_stencil;
   struct d12_umd_element_layout *element_layout;

   if (!device || !arguments ||
       (require_output && !arguments->hDrvPipelineState.pDrvPrivate) ||
       (arguments->NodeMask != 0 && arguments->NodeMask != 1))
      return false;

   compute = d12_umd_pipeline_shader(device, arguments->hComputeShader,
                                     D12_FRONTEND_SHADER_COMPUTE);
   vertex = d12_umd_pipeline_shader(device, arguments->hVertexShader,
                                    D12_FRONTEND_SHADER_VERTEX);
   pixel = d12_umd_pipeline_shader(device, arguments->hPixelShader,
                                   D12_FRONTEND_SHADER_PIXEL);
   geometry = d12_umd_pipeline_shader(device, arguments->hGeometryShader,
                                      D12_FRONTEND_SHADER_GEOMETRY);
   hull = d12_umd_pipeline_shader(device, arguments->hHullShader,
                                  D12_FRONTEND_SHADER_HULL);
   domain = d12_umd_pipeline_shader(device, arguments->hDomainShader,
                                    D12_FRONTEND_SHADER_DOMAIN);
   root_signature =
      d12_umd_root_signature_from_handle(arguments->hRootSignature);
   blend = d12_umd_blend_state_from_handle(arguments->hBlendState);
   rasterizer =
      d12_umd_rasterizer_state_from_handle(arguments->hRasterizerState);
   depth_stencil = d12_umd_depth_stencil_state_from_handle(
      arguments->hDepthStencilState);
   element_layout =
      d12_umd_element_layout_from_handle(arguments->hElementLayout);

   if ((arguments->hRootSignature.pDrvPrivate && !root_signature) ||
       (root_signature && root_signature->device != device))
      return false;
   if (compute) {
      return !arguments->hVertexShader.pDrvPrivate &&
             !arguments->hPixelShader.pDrvPrivate &&
             !arguments->hGeometryShader.pDrvPrivate &&
             !arguments->hHullShader.pDrvPrivate &&
             !arguments->hDomainShader.pDrvPrivate;
   }

   if (!vertex || !blend || !rasterizer || !depth_stencil ||
       !element_layout || blend->device != device ||
       rasterizer->device != device || depth_stencil->device != device ||
       element_layout->device != device ||
       (arguments->hPixelShader.pDrvPrivate && !pixel) ||
       (arguments->hGeometryShader.pDrvPrivate && !geometry) ||
       (arguments->hHullShader.pDrvPrivate && !hull) ||
       (arguments->hDomainShader.pDrvPrivate && !domain) || (!hull != !domain) ||
       arguments->NumRenderTargets > D12_FRONTEND_MAX_RENDER_TARGETS ||
       (arguments->SampleDesc.Count != 1 && arguments->SampleDesc.Count != 2 &&
        arguments->SampleDesc.Count != 4) ||
       arguments->SampleDesc.Quality != 0 ||
       (uint32_t)arguments->IBStripCutValue >
          D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF ||
       arguments->PrimitiveTopologyType <
          D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE_POINT ||
       arguments->PrimitiveTopologyType >
          D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE_PATCH)
      return false;

   for (UINT index = arguments->NumRenderTargets;
        index < D12_FRONTEND_MAX_RENDER_TARGETS; ++index) {
      if (arguments->RTVFormats[index] != DXGI_FORMAT_UNKNOWN)
         return false;
   }
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_pipeline_state_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arguments)
{
   return d12_umd_pipeline_arguments_valid(d12_umd_device_from_handle(handle),
                                            arguments, false)
             ? sizeof(struct d12_umd_pipeline_state)
             : 0;
}

static struct d12_frontend_shader_bytecode
d12_umd_pipeline_shader_bytecode(struct d12_umd_shader *shader)
{
   return (struct d12_frontend_shader_bytecode){
      .data = shader ? shader->dxbc : NULL,
      .size = shader ? shader->dxbc_size : 0,
   };
}

static HRESULT
d12_umd_create_pipeline_state_with_flags(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arguments,
   uint32_t pipeline_flags)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_state *pipeline_state;
   struct d12_umd_shader *compute;
   struct d12_umd_root_signature *root_signature;
   void *serialization = NULL;
   size_t serialization_size = 0;
   HRESULT result;

   if (!d12_umd_pipeline_arguments_valid(device, arguments, true))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->destroy_pipeline_state)
      return E_NOTIMPL;

   pipeline_state = arguments->hDrvPipelineState.pDrvPrivate;
   memset(pipeline_state, 0, sizeof(*pipeline_state));
   compute = d12_umd_pipeline_shader(device, arguments->hComputeShader,
                                     D12_FRONTEND_SHADER_COMPUTE);
   if (compute && pipeline_flags)
      return E_INVALIDARG;
   root_signature =
      d12_umd_root_signature_from_handle(arguments->hRootSignature);
   if (compute) {
      const struct d12_frontend_compute_pipeline_desc desc = {
         .shader = d12_umd_pipeline_shader_bytecode(compute),
         .root_signature = root_signature ? root_signature->backend : NULL,
      };
      if (!device->renderer_ops->create_compute_pipeline_state)
         return E_NOTIMPL;
      if (!d12_umd_pipeline_serialize_compute(
             &desc, root_signature ? &root_signature->desc : NULL,
             &serialization, &serialization_size))
         return E_OUTOFMEMORY;
      result = device->renderer_ops->create_compute_pipeline_state(
         device->backend, &desc, &pipeline_state->backend);
   } else {
      struct d12_umd_blend_state *blend =
         d12_umd_blend_state_from_handle(arguments->hBlendState);
      struct d12_umd_shader *geometry =
         d12_umd_pipeline_shader(device, arguments->hGeometryShader,
                                 D12_FRONTEND_SHADER_GEOMETRY);
      struct d12_umd_rasterizer_state *rasterizer =
         d12_umd_rasterizer_state_from_handle(arguments->hRasterizerState);
      struct d12_umd_depth_stencil_state *depth_stencil =
         d12_umd_depth_stencil_state_from_handle(
            arguments->hDepthStencilState);
      struct d12_umd_element_layout *element_layout =
         d12_umd_element_layout_from_handle(arguments->hElementLayout);
      struct d12_frontend_graphics_pipeline_desc desc = {
         .vertex_shader = d12_umd_pipeline_shader_bytecode(
            d12_umd_pipeline_shader(device, arguments->hVertexShader,
                                    D12_FRONTEND_SHADER_VERTEX)),
         .pixel_shader = d12_umd_pipeline_shader_bytecode(
            d12_umd_pipeline_shader(device, arguments->hPixelShader,
                                    D12_FRONTEND_SHADER_PIXEL)),
         .geometry_shader = d12_umd_pipeline_shader_bytecode(
            d12_umd_pipeline_shader(device, arguments->hGeometryShader,
                                    D12_FRONTEND_SHADER_GEOMETRY)),
         .hull_shader = d12_umd_pipeline_shader_bytecode(
            d12_umd_pipeline_shader(device, arguments->hHullShader,
                                    D12_FRONTEND_SHADER_HULL)),
         .domain_shader = d12_umd_pipeline_shader_bytecode(
            d12_umd_pipeline_shader(device, arguments->hDomainShader,
                                    D12_FRONTEND_SHADER_DOMAIN)),
         .root_signature = root_signature ? root_signature->backend : NULL,
         .blend = blend->desc,
         .sample_mask = arguments->SampleMask,
         .rasterizer = rasterizer->desc,
         .depth_stencil = depth_stencil->desc,
         .element_layout = {
            .count = element_layout->count,
            .elements = element_layout->elements,
         },
         .stream_output = {
            .declaration_count =
               geometry ? geometry->so_declaration_count : 0,
            .declarations = geometry ? geometry->so_declarations : NULL,
            .stride_count = geometry ? geometry->so_stride_count : 0,
            .strides = geometry ? geometry->so_strides : NULL,
            .rasterized_stream =
               geometry ? geometry->so_rasterized_stream : 0,
         },
         .index_buffer_strip_cut_value =
            (uint32_t)arguments->IBStripCutValue,
         .primitive_topology_type =
            (uint32_t)arguments->PrimitiveTopologyType,
         .num_render_targets = arguments->NumRenderTargets,
         .dsv_format = (uint32_t)arguments->DSVFormat,
         .sample_count = arguments->SampleDesc.Count,
         .sample_quality = arguments->SampleDesc.Quality,
         .pipeline_flags = pipeline_flags,
      };
      for (UINT index = 0; index < D12_FRONTEND_MAX_RENDER_TARGETS; ++index)
         desc.rtv_formats[index] = (uint32_t)arguments->RTVFormats[index];
      if (!device->renderer_ops->create_graphics_pipeline_state)
         return E_NOTIMPL;
      if (!d12_umd_pipeline_serialize_graphics(
             &desc, root_signature ? &root_signature->desc : NULL,
             &serialization, &serialization_size))
         return E_OUTOFMEMORY;
      result = device->renderer_ops->create_graphics_pipeline_state(
         device->backend, &desc, &pipeline_state->backend);
   }

   result = d12_umd_renderer_object_result(
      result, &pipeline_state->backend,
      device->renderer_ops->destroy_pipeline_state);
   if (FAILED(result)) {
      free(serialization);
      return result;
   }
   pipeline_state->signature = D12_UMD_PIPELINE_STATE_SIGNATURE;
   pipeline_state->device = device;
   pipeline_state->serialization = serialization;
   pipeline_state->serialization_size = serialization_size;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_create_pipeline_state(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *arguments)
{
   return d12_umd_create_pipeline_state_with_flags(handle, arguments, 0);
}

static void APIENTRY
d12_umd_destroy_pipeline_state(D3D12DDI_HDEVICE handle,
                               D3D12DDI_HPIPELINESTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_state *state =
      d12_umd_pipeline_state_from_handle(state_handle);

   if (!device || !state || state->device != device)
      return;
   if (state->backend && device->renderer_ops &&
       device->renderer_ops->destroy_pipeline_state)
      device->renderer_ops->destroy_pipeline_state(state->backend);
   free(state->serialization);
   SecureZeroMemory(state, sizeof(*state));
}

static HRESULT APIENTRY
d12_umd_retrieve_shader_comment(
   D3D12DDI_HDEVICE handle, D3D12DDI_HPIPELINESTATE state_handle,
   WCHAR *buffer, SIZE_T *character_count_including_null)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_state *state =
      d12_umd_pipeline_state_from_handle(state_handle);
   char *comment = NULL;
   size_t comment_size = 0;
   int wide_count;
   SIZE_T required;
   HRESULT result;

   if (!device || !state || state->device != device || !state->backend ||
       !character_count_including_null)
      return E_INVALIDARG;
   if (!device->renderer_ops ||
       !device->renderer_ops->get_pipeline_state_comment)
      return E_NOTIMPL;

   result = device->renderer_ops->get_pipeline_state_comment(
      state->backend, &comment, &comment_size);
   if (FAILED(result))
      return result;
   if (!comment || !comment_size || comment_size > INT_MAX) {
      free(comment);
      return E_FAIL;
   }

   wide_count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, comment,
                                    (int)comment_size, NULL, 0);
   if (wide_count <= 0) {
      DWORD error = GetLastError();
      free(comment);
      return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
   }
   required = (SIZE_T)wide_count + 1;
   if (!buffer) {
      *character_count_including_null = required;
      free(comment);
      return S_OK;
   }
   if (*character_count_including_null < required) {
      *character_count_including_null = required;
      free(comment);
      return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
   }

   if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, comment,
                           (int)comment_size, buffer, wide_count) !=
       wide_count) {
      DWORD error = GetLastError();
      free(comment);
      return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
   }
   buffer[wide_count] = L'\0';
   *character_count_including_null = required;
   free(comment);
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_serialize_object(D3D12DDI_HDEVICE handle,
                         D3D12DDI_HANDLETYPE handle_type, HANDLE object,
                         SIZE_T *blob_size, const UINT **out_blob)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_state *pipeline = object;
   void *copy;

   if (!blob_size || !out_blob)
      return E_INVALIDARG;
   *blob_size = 0;
   *out_blob = NULL;
   if (!device || handle_type != D3D12DDI_HT_PIPELINE_STATE || !pipeline ||
       pipeline->signature != D12_UMD_PIPELINE_STATE_SIGNATURE ||
       pipeline->device != device || !pipeline->serialization ||
       !pipeline->serialization_size)
      return E_INVALIDARG;

   copy = malloc(pipeline->serialization_size);
   if (!copy)
      return E_OUTOFMEMORY;
   memcpy(copy, pipeline->serialization, pipeline->serialization_size);
   *blob_size = pipeline->serialization_size;
   *out_blob = copy;
   return S_OK;
}

static void APIENTRY
d12_umd_destroy_object_serialization(D3D12DDI_HDEVICE handle,
                                     SIZE_T blob_size,
                                     const UINT *blob)
{
   (void)handle;
   (void)blob_size;
   free((void *)blob);
}

static SIZE_T APIENTRY
d12_umd_calc_private_deserialized_object_size(
   D3D12DDI_HDEVICE handle, D3D12DDI_HANDLETYPE handle_type,
   SIZE_T blob_size, const UINT *blob)
{
   return d12_umd_device_from_handle(handle) &&
                 handle_type == D3D12DDI_HT_PIPELINE_STATE &&
                 d12_umd_pipeline_serialization_valid(blob, blob_size)
             ? sizeof(struct d12_umd_pipeline_state)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_deserialized_object(D3D12DDI_HDEVICE handle,
                                   D3D12DDI_HANDLETYPE handle_type,
                                   SIZE_T blob_size, const UINT *blob,
                                   void *object, void *runtime_object)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_state *pipeline = object;
   struct d12_umd_serialized_pipeline decoded;
   void *root_signature = NULL;
   void *backend = NULL;
   void *serialization = NULL;
   HRESULT result;

   (void)runtime_object;
   if (!device || handle_type != D3D12DDI_HT_PIPELINE_STATE || !blob ||
       !blob_size || !pipeline ||
       !d12_umd_pipeline_deserialize(blob, blob_size, &decoded))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->destroy_pipeline_state) {
      d12_umd_serialized_pipeline_cleanup(&decoded);
      return E_NOTIMPL;
   }

   serialization = malloc(blob_size);
   if (!serialization) {
      result = E_OUTOFMEMORY;
      goto out;
   }
   memcpy(serialization, blob, blob_size);
   if (decoded.has_root_signature) {
      if (!device->renderer_ops->create_root_signature ||
          !device->renderer_ops->destroy_root_signature) {
         result = E_NOTIMPL;
         goto out;
      }
      result = device->renderer_ops->create_root_signature(
         device->backend, &decoded.root_signature, &root_signature);
      result = d12_umd_renderer_object_result(
         result, &root_signature,
         device->renderer_ops->destroy_root_signature);
      if (FAILED(result))
         goto out;
   }

   if (decoded.kind == D12_UMD_SERIALIZED_PIPELINE_COMPUTE) {
      if (!device->renderer_ops->create_compute_pipeline_state) {
         result = E_NOTIMPL;
         goto out;
      }
      decoded.compute.root_signature = root_signature;
      result = device->renderer_ops->create_compute_pipeline_state(
         device->backend, &decoded.compute, &backend);
   } else {
      if (!device->renderer_ops->create_graphics_pipeline_state) {
         result = E_NOTIMPL;
         goto out;
      }
      decoded.graphics.root_signature = root_signature;
      result = device->renderer_ops->create_graphics_pipeline_state(
         device->backend, &decoded.graphics, &backend);
   }
   result = d12_umd_renderer_object_result(
      result, &backend, device->renderer_ops->destroy_pipeline_state);
   if (FAILED(result))
      goto out;

   memset(pipeline, 0, sizeof(*pipeline));
   pipeline->signature = D12_UMD_PIPELINE_STATE_SIGNATURE;
   pipeline->device = device;
   pipeline->backend = backend;
   pipeline->serialization = serialization;
   pipeline->serialization_size = blob_size;
   backend = NULL;
   serialization = NULL;
   result = S_OK;

out:
   if (backend && device->renderer_ops->destroy_pipeline_state)
      device->renderer_ops->destroy_pipeline_state(backend);
   if (root_signature && device->renderer_ops->destroy_root_signature)
      device->renderer_ops->destroy_root_signature(root_signature);
   free(serialization);
   d12_umd_serialized_pipeline_cleanup(&decoded);
   return result;
}

static HRESULT
d12_umd_pipeline_library_result_to_hresult(
   enum d12_umd_pipeline_library_result result)
{
   switch (result) {
   case D12_UMD_PIPELINE_LIBRARY_OK:
      return S_OK;
   case D12_UMD_PIPELINE_LIBRARY_OUT_OF_MEMORY:
      return E_OUTOFMEMORY;
   case D12_UMD_PIPELINE_LIBRARY_DUPLICATE:
   case D12_UMD_PIPELINE_LIBRARY_NOT_FOUND:
   case D12_UMD_PIPELINE_LIBRARY_INVALID:
   default:
      return E_INVALIDARG;
   }
}

static struct d12_umd_pipeline_library *
d12_umd_pipeline_library_reference(
   struct d12_umd_device *device,
   D3D12DDI_LIBRARY_REFERENCE_0010 reference)
{
   struct d12_umd_pipeline_library *library;

   if (!reference.hLibrary.pDrvPrivate)
      return NULL;
   library = d12_umd_pipeline_library_from_handle(reference.hLibrary);
   return library && library->device == device ? library : NULL;
}

static bool
d12_umd_pipeline_library_reference_valid(
   struct d12_umd_device *device,
   D3D12DDI_LIBRARY_REFERENCE_0010 reference)
{
   return !reference.hLibrary.pDrvPrivate ||
          d12_umd_pipeline_library_reference(device, reference);
}

_Static_assert(offsetof(D3D12DDIARG_CREATEELEMENTLAYOUT_0010,
                        LibraryReference) ==
                  sizeof(D3D12DDIARG_CREATEELEMENTLAYOUT),
               "R1 element-layout descriptor prefix changed");
_Static_assert(offsetof(D3D12DDI_BLEND_DESC_0010, LibraryReference) ==
                  sizeof(D3D12DDI_BLEND_DESC),
               "R1 blend descriptor prefix changed");
_Static_assert(offsetof(D3D12DDI_DEPTH_STENCIL_DESC_0010, LibraryReference) ==
                  64,
               "R1 depth-stencil descriptor prefix changed");
_Static_assert(offsetof(D3D12DDI_RASTERIZER_DESC_0010, LibraryReference) ==
                  sizeof(D3D12DDI_RASTERIZER_DESC),
               "R1 rasterizer descriptor prefix changed");

static SIZE_T APIENTRY
d12_umd_calc_private_element_layout_size_0010(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEELEMENTLAYOUT_0010 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDIARG_CREATEELEMENTLAYOUT baseline;

   if (!device || !arguments ||
       !d12_umd_pipeline_library_reference_valid(
          device, arguments->LibraryReference))
      return 0;
   memcpy(&baseline, arguments, sizeof(baseline));
   return d12_umd_calc_private_element_layout_size(handle, &baseline);
}

static void APIENTRY
d12_umd_create_element_layout_0010(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEELEMENTLAYOUT_0010 *arguments,
   D3D12DDI_HELEMENTLAYOUT layout)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDIARG_CREATEELEMENTLAYOUT baseline;

   if (!device || !arguments ||
       !d12_umd_pipeline_library_reference_valid(
          device, arguments->LibraryReference)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   memcpy(&baseline, arguments, sizeof(baseline));
   d12_umd_create_element_layout(handle, &baseline, layout);
}

/* The handle type names do not follow descriptor typedef names, so keep the
 * three compact wrappers explicit. */
static SIZE_T APIENTRY
d12_umd_calc_private_blend_state_size_0010(
   D3D12DDI_HDEVICE handle, const D3D12DDI_BLEND_DESC_0010 *desc)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_BLEND_DESC baseline;
   if (!device || !desc ||
       !d12_umd_pipeline_library_reference_valid(device,
                                                  desc->LibraryReference))
      return 0;
   memcpy(&baseline, desc, sizeof(baseline));
   return d12_umd_calc_private_blend_state_size(handle, &baseline);
}

static void APIENTRY
d12_umd_create_blend_state_0010(D3D12DDI_HDEVICE handle,
                                const D3D12DDI_BLEND_DESC_0010 *desc,
                                D3D12DDI_HBLENDSTATE state)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_BLEND_DESC baseline;
   if (!device || !desc ||
       !d12_umd_pipeline_library_reference_valid(device,
                                                  desc->LibraryReference)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   memcpy(&baseline, desc, sizeof(baseline));
   d12_umd_create_blend_state(handle, &baseline, state);
}

static SIZE_T APIENTRY
d12_umd_calc_private_depth_stencil_state_size_0010(
   D3D12DDI_HDEVICE handle, const D3D12DDI_DEPTH_STENCIL_DESC_0010 *desc)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_DEPTH_STENCIL_DESC baseline;
   if (!device || !desc ||
       !d12_umd_pipeline_library_reference_valid(device,
                                                  desc->LibraryReference))
      return 0;
   memcpy(&baseline, desc, sizeof(baseline));
   return d12_umd_calc_private_depth_stencil_state_size(handle, &baseline);
}

static void APIENTRY
d12_umd_create_depth_stencil_state_0010(
   D3D12DDI_HDEVICE handle, const D3D12DDI_DEPTH_STENCIL_DESC_0010 *desc,
   D3D12DDI_HDEPTHSTENCILSTATE state)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_DEPTH_STENCIL_DESC baseline;
   if (!device || !desc ||
       !d12_umd_pipeline_library_reference_valid(device,
                                                  desc->LibraryReference)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   memcpy(&baseline, desc, sizeof(baseline));
   d12_umd_create_depth_stencil_state(handle, &baseline, state);
}

static SIZE_T APIENTRY
d12_umd_calc_private_rasterizer_state_size_0010(
   D3D12DDI_HDEVICE handle, const D3D12DDI_RASTERIZER_DESC_0010 *desc)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_RASTERIZER_DESC baseline;
   if (!device || !desc ||
       !d12_umd_pipeline_library_reference_valid(device,
                                                  desc->LibraryReference))
      return 0;
   memcpy(&baseline, desc, sizeof(baseline));
   return d12_umd_calc_private_rasterizer_state_size(handle, &baseline);
}

static void APIENTRY
d12_umd_create_rasterizer_state_0010(
   D3D12DDI_HDEVICE handle, const D3D12DDI_RASTERIZER_DESC_0010 *desc,
   D3D12DDI_HRASTERIZERSTATE state)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_RASTERIZER_DESC baseline;
   if (!device || !desc ||
       !d12_umd_pipeline_library_reference_valid(device,
                                                  desc->LibraryReference)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   memcpy(&baseline, desc, sizeof(baseline));
   d12_umd_create_rasterizer_state(handle, &baseline, state);
}

static SIZE_T
d12_umd_calc_private_shader_size_0010_stage(
   D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_SHADER_0010 *arguments,
   bool tessellation)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   if (!device || !arguments ||
       !d12_umd_pipeline_library_reference_valid(
          device, arguments->LibraryReference))
      return 0;
   return tessellation
             ? d12_umd_calc_private_tessellation_shader_size(
                  handle, arguments->pShaderCode, arguments->hRootSignature,
                  arguments->IOSignatures.Tessellation)
             : d12_umd_calc_private_shader_size(
                  handle, arguments->pShaderCode, arguments->hRootSignature,
                  arguments->IOSignatures.Standard);
}

static SIZE_T APIENTRY
d12_umd_calc_private_shader_size_0010(
   D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_SHADER_0010 *arguments)
{
   return d12_umd_calc_private_shader_size_0010_stage(handle, arguments,
                                                       false);
}

static SIZE_T APIENTRY
d12_umd_calc_private_tessellation_shader_size_0010(
   D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_SHADER_0010 *arguments)
{
   return d12_umd_calc_private_shader_size_0010_stage(handle, arguments,
                                                       true);
}

static void
d12_umd_create_shader_0010_stage(
   D3D12DDI_HDEVICE handle, const D3D12DDIARG_CREATE_SHADER_0010 *arguments,
   D3D12DDI_HSHADER shader, enum d12_frontend_shader_stage stage,
   bool tessellation)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   if (!device || !arguments ||
       !d12_umd_pipeline_library_reference_valid(
          device, arguments->LibraryReference)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   if (tessellation) {
      d12_umd_create_tessellation_shader(
         handle, arguments->pShaderCode, arguments->hRootSignature, shader,
         arguments->IOSignatures.Tessellation, arguments->Flags, stage);
   } else {
      d12_umd_create_shader(handle, arguments->pShaderCode,
                            arguments->hRootSignature, shader,
                            arguments->IOSignatures.Standard,
                            arguments->Flags, stage);
   }
}

#define D12_UMD_DEFINE_R1_SHADER_CREATE(name, stage, tessellation)           \
   static void APIENTRY name(                                                \
      D3D12DDI_HDEVICE handle,                                               \
      const D3D12DDIARG_CREATE_SHADER_0010 *arguments,                       \
      D3D12DDI_HSHADER shader)                                               \
   {                                                                          \
      d12_umd_create_shader_0010_stage(handle, arguments, shader, stage,      \
                                        tessellation);                        \
   }

D12_UMD_DEFINE_R1_SHADER_CREATE(d12_umd_create_vertex_shader_0010,
                                D12_FRONTEND_SHADER_VERTEX, false)
D12_UMD_DEFINE_R1_SHADER_CREATE(d12_umd_create_pixel_shader_0010,
                                D12_FRONTEND_SHADER_PIXEL, false)
D12_UMD_DEFINE_R1_SHADER_CREATE(d12_umd_create_geometry_shader_0010,
                                D12_FRONTEND_SHADER_GEOMETRY, false)
D12_UMD_DEFINE_R1_SHADER_CREATE(d12_umd_create_compute_shader_0010,
                                D12_FRONTEND_SHADER_COMPUTE, false)
D12_UMD_DEFINE_R1_SHADER_CREATE(d12_umd_create_hull_shader_0010,
                                D12_FRONTEND_SHADER_HULL, true)
D12_UMD_DEFINE_R1_SHADER_CREATE(d12_umd_create_domain_shader_0010,
                                D12_FRONTEND_SHADER_DOMAIN, true)

static bool
d12_umd_translate_geometry_shader_with_stream_output_0010(
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 *source,
   D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT *destination)
{
   if (!source || !destination)
      return false;
   *destination = (D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT){
      .hRootSignature = source->CreateShader.hRootSignature,
      .pShaderCode = source->CreateShader.pShaderCode,
      .pOutputStreamDecl = source->pOutputStreamDecl,
      .NumEntries = source->NumEntries,
      .BufferStridesInBytes = source->BufferStridesInBytes,
      .NumStrides = source->NumStrides,
      .RasterizedStream = source->RasterizedStream,
   };
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_geometry_shader_with_stream_output_size_0010(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT baseline;
   if (!device || !arguments ||
       !d12_umd_pipeline_library_reference_valid(
          device, arguments->CreateShader.LibraryReference) ||
       !d12_umd_translate_geometry_shader_with_stream_output_0010(
          arguments, &baseline))
      return 0;
   return d12_umd_calc_private_geometry_shader_with_stream_output_size(
      handle, &baseline, arguments->CreateShader.IOSignatures.Standard);
}

static void APIENTRY
d12_umd_create_geometry_shader_with_stream_output_0010(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 *arguments,
   D3D12DDI_HSHADER shader)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT baseline;
   if (!device || !arguments ||
       !d12_umd_pipeline_library_reference_valid(
          device, arguments->CreateShader.LibraryReference) ||
       !d12_umd_translate_geometry_shader_with_stream_output_0010(
          arguments, &baseline)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   d12_umd_create_geometry_shader_with_stream_output(
      handle, &baseline, shader,
      arguments->CreateShader.IOSignatures.Standard,
      arguments->CreateShader.Flags);
}

static bool
d12_umd_translate_pipeline_state_0010(
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *source,
   D3D12DDI_HPIPELINESTATE output,
   D3D12DDIARG_CREATE_PIPELINE_STATE_0001 *destination)
{
   if (!source || !destination)
      return false;
   *destination = (D3D12DDIARG_CREATE_PIPELINE_STATE_0001){
      .hDrvPipelineState = output,
      .hComputeShader = source->hComputeShader,
      .hVertexShader = source->hVertexShader,
      .hPixelShader = source->hPixelShader,
      .hDomainShader = source->hDomainShader,
      .hHullShader = source->hHullShader,
      .hGeometryShader = source->hGeometryShader,
      .hRootSignature = source->hRootSignature,
      .hBlendState = source->hBlendState,
      .SampleMask = source->SampleMask,
      .hRasterizerState = source->hRasterizerState,
      .hDepthStencilState = source->hDepthStencilState,
      .hElementLayout = source->hElementLayout,
      .IBStripCutValue = source->IBStripCutValue,
      .PrimitiveTopologyType = source->PrimitiveTopologyType,
      .NumRenderTargets = source->NumRenderTargets,
      .DSVFormat = source->DSVFormat,
      .SampleDesc = source->SampleDesc,
      .NodeMask = source->NodeMask,
   };
   memcpy(destination->RTVFormats, source->RTVFormats,
          sizeof(destination->RTVFormats));
   return true;
}

static bool
d12_umd_pipeline_library_contains(
   struct d12_umd_pipeline_library *library, uint32_t index)
{
   bool found;
   AcquireSRWLockShared(&library->lock);
   found = d12_umd_pipeline_library_find_const(&library->core, index) != NULL;
   ReleaseSRWLockShared(&library->lock);
   return found;
}

static SIZE_T APIENTRY
d12_umd_calc_private_pipeline_state_size_0010(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_library *library;
   D3D12DDIARG_CREATE_PIPELINE_STATE_0001 baseline;

   if (!device || !arguments ||
       !d12_umd_pipeline_library_reference_valid(
          device, arguments->LibraryReference) ||
       !d12_umd_translate_pipeline_state_0010(
          arguments, (D3D12DDI_HPIPELINESTATE){0}, &baseline))
      return 0;
   library = d12_umd_pipeline_library_reference(
      device, arguments->LibraryReference);
   if (library &&
       !d12_umd_pipeline_library_contains(
          library, arguments->LibraryReference.PipelineIndex))
      return 0;
   return library ? sizeof(struct d12_umd_pipeline_state)
                  : d12_umd_calc_private_pipeline_state_size(handle,
                                                              &baseline);
}

static HRESULT
d12_umd_pipeline_library_load(
   D3D12DDI_HDEVICE handle, struct d12_umd_pipeline_library *library,
   uint32_t index, D3D12DDI_HPIPELINESTATE output)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_state *pipeline = output.pDrvPrivate;
   struct d12_umd_pipeline_library_entry *entry;
   void *blob = NULL;
   size_t blob_size = 0;
   HRESULT result;

   if (!device || !library || !pipeline || library->device != device ||
       !device->renderer_ops ||
       !device->renderer_ops->retain_pipeline_state ||
       !device->renderer_ops->destroy_pipeline_state)
      return E_INVALIDARG;

   AcquireSRWLockShared(&library->lock);
   entry = d12_umd_pipeline_library_find(&library->core, index);
   if (!entry) {
      ReleaseSRWLockShared(&library->lock);
      return E_INVALIDARG;
   }
   blob_size = entry->blob_size;
   blob = malloc(blob_size);
   if (!blob) {
      ReleaseSRWLockShared(&library->lock);
      return E_OUTOFMEMORY;
   }
   memcpy(blob, entry->blob, blob_size);
   if (entry->backend) {
      device->renderer_ops->retain_pipeline_state(entry->backend);
      memset(pipeline, 0, sizeof(*pipeline));
      pipeline->signature = D12_UMD_PIPELINE_STATE_SIGNATURE;
      pipeline->device = device;
      pipeline->backend = entry->backend;
      pipeline->serialization = blob;
      pipeline->serialization_size = blob_size;
      ReleaseSRWLockShared(&library->lock);
      return S_OK;
   }
   ReleaseSRWLockShared(&library->lock);

   result = d12_umd_create_deserialized_object(
      handle, D3D12DDI_HT_PIPELINE_STATE, blob_size, blob, pipeline, NULL);
   free(blob);
   if (FAILED(result))
      return result;

   AcquireSRWLockExclusive(&library->lock);
   entry = d12_umd_pipeline_library_find(&library->core, index);
   if (!entry) {
      ReleaseSRWLockExclusive(&library->lock);
      d12_umd_destroy_pipeline_state(handle, output);
      return E_INVALIDARG;
   }
   if (!entry->backend) {
      device->renderer_ops->retain_pipeline_state(pipeline->backend);
      entry->backend = pipeline->backend;
   } else {
      void *compiled = pipeline->backend;
      device->renderer_ops->retain_pipeline_state(entry->backend);
      pipeline->backend = entry->backend;
      device->renderer_ops->destroy_pipeline_state(compiled);
   }
   ReleaseSRWLockExclusive(&library->lock);
   return S_OK;
}

static HRESULT
d12_umd_create_pipeline_state_0010_with_flags(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *arguments,
   D3D12DDI_HPIPELINESTATE output, uint32_t pipeline_flags)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_library *library;
   D3D12DDIARG_CREATE_PIPELINE_STATE_0001 baseline;

   if (!device || !arguments || !output.pDrvPrivate ||
       !d12_umd_pipeline_library_reference_valid(
          device, arguments->LibraryReference) ||
       !d12_umd_translate_pipeline_state_0010(arguments, output, &baseline))
      return E_INVALIDARG;
   library = d12_umd_pipeline_library_reference(
      device, arguments->LibraryReference);
   return library ? d12_umd_pipeline_library_load(
                       handle, library,
                       arguments->LibraryReference.PipelineIndex, output)
                  : d12_umd_create_pipeline_state_with_flags(
                       handle, &baseline, pipeline_flags);
}

static HRESULT APIENTRY
d12_umd_create_pipeline_state_0010(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *arguments,
   D3D12DDI_HPIPELINESTATE output)
{
   return d12_umd_create_pipeline_state_0010_with_flags(
      handle, arguments, output, 0);
}

#if D12_UMD_TARGETS_WDDM23
_Static_assert(offsetof(D3D12DDI_DEPTH_STENCIL_DESC_0025,
                        DepthBoundsTestEnable) ==
                  sizeof(D3D12DDI_DEPTH_STENCIL_DESC_0010),
               "DDI 0025 depth/stencil prefix changed");
_Static_assert(offsetof(D3D12DDIARG_CREATE_SHADER_0026,
                        ShaderCodeHash) ==
                  sizeof(D3D12DDIARG_CREATE_SHADER_0010),
               "DDI 0026 shader prefix changed");

static SIZE_T APIENTRY
d12_umd_calc_private_depth_stencil_state_size_0025(
   D3D12DDI_HDEVICE handle,
   const D3D12DDI_DEPTH_STENCIL_DESC_0025 *desc)
{
   D3D12DDI_DEPTH_STENCIL_DESC_0010 release1;

   if (!desc || desc->DepthBoundsTestEnable)
      return 0;
   memcpy(&release1, desc, sizeof(release1));
   return d12_umd_calc_private_depth_stencil_state_size_0010(handle,
                                                              &release1);
}

static void APIENTRY
d12_umd_create_depth_stencil_state_0025(
   D3D12DDI_HDEVICE handle,
   const D3D12DDI_DEPTH_STENCIL_DESC_0025 *desc,
   D3D12DDI_HDEPTHSTENCILSTATE state)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_DEPTH_STENCIL_DESC_0010 release1;

   if (!desc || desc->DepthBoundsTestEnable) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   memcpy(&release1, desc, sizeof(release1));
   d12_umd_create_depth_stencil_state_0010(handle, &release1, state);
}

static void
d12_umd_shader_0026_to_0010(
   const D3D12DDIARG_CREATE_SHADER_0026 *source,
   D3D12DDIARG_CREATE_SHADER_0010 *destination)
{
   memcpy(destination, source, sizeof(*destination));
}

static SIZE_T
d12_umd_calc_private_shader_size_0026_stage(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_SHADER_0026 *arguments, bool tessellation)
{
   D3D12DDIARG_CREATE_SHADER_0010 release1;

   if (!arguments)
      return 0;
   d12_umd_shader_0026_to_0010(arguments, &release1);
   return d12_umd_calc_private_shader_size_0010_stage(
      handle, &release1, tessellation);
}

static SIZE_T APIENTRY
d12_umd_calc_private_shader_size_0026(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_SHADER_0026 *arguments)
{
   return d12_umd_calc_private_shader_size_0026_stage(handle, arguments,
                                                       false);
}

static SIZE_T APIENTRY
d12_umd_calc_private_tessellation_shader_size_0026(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_SHADER_0026 *arguments)
{
   return d12_umd_calc_private_shader_size_0026_stage(handle, arguments,
                                                       true);
}

static void
d12_umd_create_shader_0026_stage(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_SHADER_0026 *arguments,
   D3D12DDI_HSHADER shader, enum d12_frontend_shader_stage stage,
   bool tessellation)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDIARG_CREATE_SHADER_0010 release1;

   if (!arguments) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   d12_umd_shader_0026_to_0010(arguments, &release1);
   d12_umd_create_shader_0010_stage(handle, &release1, shader, stage,
                                     tessellation);
}

#define D12_UMD_DEFINE_R3_SHADER_CREATE(name, stage, tessellation)           \
   static void APIENTRY name(                                                \
      D3D12DDI_HDEVICE handle,                                               \
      const D3D12DDIARG_CREATE_SHADER_0026 *arguments,                       \
      D3D12DDI_HSHADER shader)                                               \
   {                                                                          \
      d12_umd_create_shader_0026_stage(handle, arguments, shader, stage,      \
                                        tessellation);                        \
   }

D12_UMD_DEFINE_R3_SHADER_CREATE(d12_umd_create_vertex_shader_0026,
                                D12_FRONTEND_SHADER_VERTEX, false)
D12_UMD_DEFINE_R3_SHADER_CREATE(d12_umd_create_pixel_shader_0026,
                                D12_FRONTEND_SHADER_PIXEL, false)
D12_UMD_DEFINE_R3_SHADER_CREATE(d12_umd_create_geometry_shader_0026,
                                D12_FRONTEND_SHADER_GEOMETRY, false)
D12_UMD_DEFINE_R3_SHADER_CREATE(d12_umd_create_compute_shader_0026,
                                D12_FRONTEND_SHADER_COMPUTE, false)
D12_UMD_DEFINE_R3_SHADER_CREATE(d12_umd_create_hull_shader_0026,
                                D12_FRONTEND_SHADER_HULL, true)
D12_UMD_DEFINE_R3_SHADER_CREATE(d12_umd_create_domain_shader_0026,
                                D12_FRONTEND_SHADER_DOMAIN, true)

static bool
d12_umd_translate_geometry_shader_with_stream_output_0026(
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026 *source,
   D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 *destination)
{
   if (!source || !destination)
      return false;
   memset(destination, 0, sizeof(*destination));
   d12_umd_shader_0026_to_0010(&source->CreateShader,
                                &destination->CreateShader);
   destination->pOutputStreamDecl = source->pOutputStreamDecl;
   destination->NumEntries = source->NumEntries;
   destination->BufferStridesInBytes = source->BufferStridesInBytes;
   destination->NumStrides = source->NumStrides;
   destination->RasterizedStream = source->RasterizedStream;
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_geometry_shader_with_stream_output_size_0026(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026 *arguments)
{
   D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 release1;
   return d12_umd_translate_geometry_shader_with_stream_output_0026(
             arguments, &release1)
             ? d12_umd_calc_private_geometry_shader_with_stream_output_size_0010(
                  handle, &release1)
             : 0;
}

static void APIENTRY
d12_umd_create_geometry_shader_with_stream_output_0026(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026 *arguments,
   D3D12DDI_HSHADER shader)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0010 release1;
   if (!d12_umd_translate_geometry_shader_with_stream_output_0026(
          arguments, &release1)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }
   d12_umd_create_geometry_shader_with_stream_output_0010(
      handle, &release1, shader);
}

static HRESULT APIENTRY
d12_umd_create_pipeline_state_0021(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *arguments,
   D3D12DDI_HPIPELINESTATE output,
   D3D12DDI_HRTPIPELINESTATE runtime_pipeline)
{
   (void)runtime_pipeline;
   return d12_umd_create_pipeline_state_0010(handle, arguments, output);
}
#endif

#if D12_UMD_TARGETS_WDDM24
_Static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0033,
                        ViewInstancingDesc) ==
                  sizeof(D3D12DDIARG_CREATE_PIPELINE_STATE_0010),
               "DDI 0033 pipeline-state prefix changed");

static bool
d12_umd_translate_pipeline_state_0033(
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *source,
   D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *destination)
{
   if (!source || !destination ||
       source->ViewInstancingDesc.ViewInstanceCount != 0 ||
       source->ViewInstancingDesc.Flags !=
          D3D12DDI_VIEW_INSTANCING_FLAG_NONE)
      return false;

   memcpy(destination, source, sizeof(*destination));
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_pipeline_state_size_0033(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *arguments)
{
   D3D12DDIARG_CREATE_PIPELINE_STATE_0010 release1;

   if (!d12_umd_translate_pipeline_state_0033(arguments, &release1))
      return 0;
   return d12_umd_calc_private_pipeline_state_size_0010(handle, &release1);
}

static HRESULT APIENTRY
d12_umd_create_pipeline_state_0033(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0033 *arguments,
   D3D12DDI_HPIPELINESTATE output,
   D3D12DDI_HRTPIPELINESTATE runtime_pipeline)
{
   D3D12DDIARG_CREATE_PIPELINE_STATE_0010 release1;

   if (!d12_umd_translate_pipeline_state_0033(arguments, &release1))
      return E_INVALIDARG;
   return d12_umd_create_pipeline_state_0021(
      handle, &release1, output, runtime_pipeline);
}
#endif

#if D12_UMD_TARGETS_WDDM31
static bool
d12_umd_depth_stencil_state_arguments_valid_0095(
   struct d12_umd_device *device,
   const D3D12DDI_DEPTH_STENCIL_DESC_0095 *desc)
{
   return device && desc && !desc->DepthBoundsTestEnable &&
          d12_umd_pipeline_library_reference_valid(device,
                                                    desc->LibraryReference);
}

static SIZE_T APIENTRY
d12_umd_calc_private_depth_stencil_state_size_0095(
   D3D12DDI_HDEVICE handle,
   const D3D12DDI_DEPTH_STENCIL_DESC_0095 *desc)
{
   return d12_umd_depth_stencil_state_arguments_valid_0095(
             d12_umd_device_from_handle(handle), desc)
             ? sizeof(struct d12_umd_depth_stencil_state)
             : 0;
}

static void APIENTRY
d12_umd_create_depth_stencil_state_0095(
   D3D12DDI_HDEVICE handle,
   const D3D12DDI_DEPTH_STENCIL_DESC_0095 *desc,
   D3D12DDI_HDEPTHSTENCILSTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_depth_stencil_state *state = state_handle.pDrvPrivate;

   if (!state ||
       !d12_umd_depth_stencil_state_arguments_valid_0095(device, desc)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }

   memset(state, 0, sizeof(*state));
   state->signature = D12_UMD_DEPTH_STENCIL_STATE_SIGNATURE;
   state->device = device;
   state->desc = (struct d12_frontend_depth_stencil_desc){
      .depth_enable = desc->DepthEnable ? 1u : 0u,
      .depth_write_mask = (uint32_t)desc->DepthWriteMask,
      .depth_func = (uint32_t)desc->DepthFunc,
      .stencil_enable = desc->StencilEnable ? 1u : 0u,
      .front_enable = desc->FrontEnable ? 1u : 0u,
      .back_enable = desc->BackEnable ? 1u : 0u,
      .front_face_stencil_read_mask = desc->FrontFaceStencilReadMask,
      .front_face_stencil_write_mask = desc->FrontFaceStencilWriteMask,
      .back_face_stencil_read_mask = desc->BackFaceStencilReadMask,
      .back_face_stencil_write_mask = desc->BackFaceStencilWriteMask,
      .front_face = d12_umd_translate_depth_stencil_op(&desc->FrontFace),
      .back_face = d12_umd_translate_depth_stencil_op(&desc->BackFace),
   };
}

static bool
d12_umd_rasterizer_state_arguments_valid_0099(
   struct d12_umd_device *device,
   const D3D12DDI_RASTERIZER_DESC_0099 *desc)
{
   return device && desc &&
          d12_umd_pipeline_library_reference_valid(device,
                                                    desc->LibraryReference);
}

static SIZE_T APIENTRY
d12_umd_calc_private_rasterizer_state_size_0099(
   D3D12DDI_HDEVICE handle, const D3D12DDI_RASTERIZER_DESC_0099 *desc)
{
   return d12_umd_rasterizer_state_arguments_valid_0099(
             d12_umd_device_from_handle(handle), desc)
             ? sizeof(struct d12_umd_rasterizer_state)
             : 0;
}

static void APIENTRY
d12_umd_create_rasterizer_state_0099(
   D3D12DDI_HDEVICE handle, const D3D12DDI_RASTERIZER_DESC_0099 *desc,
   D3D12DDI_HRASTERIZERSTATE state_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_rasterizer_state *state = state_handle.pDrvPrivate;

   if (!state || !d12_umd_rasterizer_state_arguments_valid_0099(device,
                                                                 desc)) {
      d12_umd_pipeline_set_error(device, E_INVALIDARG);
      return;
   }

   memset(state, 0, sizeof(*state));
   state->signature = D12_UMD_RASTERIZER_STATE_SIGNATURE;
   state->device = device;
   state->desc = (struct d12_frontend_rasterizer_desc){
      .fill_mode = (uint32_t)desc->FillMode,
      .cull_mode = (uint32_t)desc->CullMode,
      .front_counter_clockwise = desc->FrontCounterClockwise ? 1u : 0u,
      .depth_bias = desc->DepthBias,
      .depth_bias_clamp = desc->DepthBiasClamp,
      .slope_scaled_depth_bias = desc->SlopeScaledDepthBias,
      .depth_clip_enable = desc->DepthClipEnable ? 1u : 0u,
      .scissor_enable = desc->ScissorEnable ? 1u : 0u,
      .multisample_enable = desc->MultisampleEnable ? 1u : 0u,
      .antialiased_line_enable = desc->AntialiasedLineEnable ? 1u : 0u,
      .forced_sample_count = desc->ForcedSampleCount,
      .conservative_rasterization_mode =
         (uint32_t)desc->ConservativeRasterizationMode,
   };
}

_Static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0099,
                        ViewInstancingDesc) ==
                  sizeof(D3D12DDIARG_CREATE_PIPELINE_STATE_0010),
               "DDI 0099 pipeline-state R1 prefix changed");
_Static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0099,
                        hMeshShader) ==
                  sizeof(D3D12DDIARG_CREATE_PIPELINE_STATE_0033),
               "DDI 0099 pipeline-state view-instancing prefix changed");

static bool
d12_umd_translate_pipeline_state_0099(
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0099 *source,
   D3D12DDIARG_CREATE_PIPELINE_STATE_0010 *destination,
   uint32_t *pipeline_flags)
{
   const uint32_t flags = source ? (uint32_t)source->Flags : 0;
   const uint32_t supported_flags =
      (uint32_t)D12_FRONTEND_PIPELINE_DYNAMIC_DEPTH_BIAS |
      (uint32_t)D12_FRONTEND_PIPELINE_DYNAMIC_INDEX_BUFFER_STRIP_CUT;

   if (!source || !destination || !pipeline_flags ||
       source->ViewInstancingDesc.ViewInstanceCount != 0 ||
       source->ViewInstancingDesc.Flags !=
          D3D12DDI_VIEW_INSTANCING_FLAG_NONE ||
       source->hMeshShader.pDrvPrivate ||
       source->hAmplificationShader.pDrvPrivate ||
       (flags & ~supported_flags) ||
       (source->hComputeShader.pDrvPrivate && flags))
      return false;

   memcpy(destination, source, sizeof(*destination));
   *pipeline_flags = flags;
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_pipeline_state_size_0099(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0099 *arguments)
{
   D3D12DDIARG_CREATE_PIPELINE_STATE_0010 release1;
   uint32_t pipeline_flags;

   return d12_umd_translate_pipeline_state_0099(
             arguments, &release1, &pipeline_flags)
             ? d12_umd_calc_private_pipeline_state_size_0010(handle,
                                                              &release1)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_pipeline_state_0099(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_STATE_0099 *arguments,
   D3D12DDI_HPIPELINESTATE output,
   D3D12DDI_HRTPIPELINESTATE runtime_pipeline)
{
   D3D12DDIARG_CREATE_PIPELINE_STATE_0010 release1;
   uint32_t pipeline_flags;
   (void)runtime_pipeline;

   if (!d12_umd_translate_pipeline_state_0099(
          arguments, &release1, &pipeline_flags))
      return E_INVALIDARG;
   return d12_umd_create_pipeline_state_0010_with_flags(
      handle, &release1, output, pipeline_flags);
}
#endif

static SIZE_T APIENTRY
d12_umd_calc_private_pipeline_library_size_0010(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 *arguments)
{
   struct d12_umd_pipeline_library_core parsed;
   enum d12_umd_pipeline_library_result result;

   if (!d12_umd_device_from_handle(handle) || !arguments ||
       (!!arguments->InitialDataSize != !!arguments->pInitialData))
      return 0;
   if (!arguments->InitialDataSize)
      return sizeof(struct d12_umd_pipeline_library);

   d12_umd_pipeline_library_init(&parsed);
   result = d12_umd_pipeline_library_import(
      &parsed, arguments->pInitialData, arguments->InitialDataSize);
   d12_umd_pipeline_library_cleanup(&parsed);
   return result == D12_UMD_PIPELINE_LIBRARY_OK
             ? sizeof(struct d12_umd_pipeline_library)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_pipeline_library_0010(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 *arguments,
   D3D12DDI_HPIPELINELIBRARY library_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_library *library = library_handle.pDrvPrivate;
   enum d12_umd_pipeline_library_result result =
      D12_UMD_PIPELINE_LIBRARY_OK;

   if (!device || !arguments || !library ||
       (!!arguments->InitialDataSize != !!arguments->pInitialData))
      return E_INVALIDARG;
   memset(library, 0, sizeof(*library));
   InitializeSRWLock(&library->lock);
   d12_umd_pipeline_library_init(&library->core);
   if (arguments->InitialDataSize)
      result = d12_umd_pipeline_library_import(
         &library->core, arguments->pInitialData, arguments->InitialDataSize);
   if (result != D12_UMD_PIPELINE_LIBRARY_OK) {
      d12_umd_pipeline_library_cleanup(&library->core);
      SecureZeroMemory(library, sizeof(*library));
      return d12_umd_pipeline_library_result_to_hresult(result);
   }
   library->signature = D12_UMD_PIPELINE_LIBRARY_SIGNATURE;
   library->device = device;
   return S_OK;
}

static void APIENTRY
d12_umd_destroy_pipeline_library_0010(
   D3D12DDI_HDEVICE handle, D3D12DDI_HPIPELINELIBRARY library_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_library *library =
      d12_umd_pipeline_library_from_handle(library_handle);

   if (!device || !library || library->device != device)
      return;
   AcquireSRWLockExclusive(&library->lock);
   if (device->renderer_ops &&
       device->renderer_ops->destroy_pipeline_state) {
      for (uint32_t index = 0; index < library->core.count; ++index) {
         if (library->core.entries[index].backend)
            device->renderer_ops->destroy_pipeline_state(
               library->core.entries[index].backend);
      }
   }
   d12_umd_pipeline_library_cleanup(&library->core);
   library->signature = 0;
   ReleaseSRWLockExclusive(&library->lock);
   SecureZeroMemory(library, sizeof(*library));
}

static HRESULT APIENTRY
d12_umd_add_pipeline_state_to_library_0010(
   D3D12DDI_HDEVICE handle, D3D12DDI_HPIPELINELIBRARY library_handle,
   D3D12DDI_HPIPELINESTATE pipeline_handle, UINT pipeline_index)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_library *library =
      d12_umd_pipeline_library_from_handle(library_handle);
   struct d12_umd_pipeline_state *pipeline =
      d12_umd_pipeline_state_from_handle(pipeline_handle);
   struct d12_umd_pipeline_library_entry *entry = NULL;
   enum d12_umd_pipeline_library_result result;

   if (!device || !library || library->device != device || !pipeline ||
       pipeline->device != device || !pipeline->backend ||
       !pipeline->serialization || !pipeline->serialization_size ||
       !device->renderer_ops ||
       !device->renderer_ops->retain_pipeline_state)
      return E_INVALIDARG;

   AcquireSRWLockExclusive(&library->lock);
   result = d12_umd_pipeline_library_add(
      &library->core, pipeline_index, pipeline->serialization,
      pipeline->serialization_size, &entry);
   if (result == D12_UMD_PIPELINE_LIBRARY_OK) {
      device->renderer_ops->retain_pipeline_state(pipeline->backend);
      entry->backend = pipeline->backend;
   }
   ReleaseSRWLockExclusive(&library->lock);
   return d12_umd_pipeline_library_result_to_hresult(result);
}

static SIZE_T APIENTRY
d12_umd_calc_serialized_library_size_0010(
   D3D12DDI_HDEVICE handle, D3D12DDI_HPIPELINELIBRARY library_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_library *library =
      d12_umd_pipeline_library_from_handle(library_handle);
   size_t size = 0;

   if (!device || !library || library->device != device)
      return 0;
   AcquireSRWLockShared(&library->lock);
   if (!d12_umd_pipeline_library_serialized_size(&library->core, &size))
      size = 0;
   ReleaseSRWLockShared(&library->lock);
   return size;
}

static HRESULT APIENTRY
d12_umd_serialize_library_0010(
   D3D12DDI_HDEVICE handle, D3D12DDI_HPIPELINELIBRARY library_handle,
   void *blob)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_pipeline_library *library =
      d12_umd_pipeline_library_from_handle(library_handle);
   enum d12_umd_pipeline_library_result result;
   size_t size;

   if (!device || !library || library->device != device || !blob)
      return E_INVALIDARG;
   AcquireSRWLockShared(&library->lock);
   if (!d12_umd_pipeline_library_serialized_size(&library->core, &size))
      result = D12_UMD_PIPELINE_LIBRARY_INVALID;
   else
      result = d12_umd_pipeline_library_serialize(&library->core, blob, size);
   ReleaseSRWLockShared(&library->lock);
   return d12_umd_pipeline_library_result_to_hresult(result);
}

static void APIENTRY
d12_umd_set_pipeline_state(D3D12DDI_HCOMMANDLIST list_handle,
                           D3D12DDI_HPIPELINESTATE state_handle)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_pipeline_state *state =
      d12_umd_pipeline_state_from_handle(state_handle);

   if (!list || !state || !list->device || state->device != list->device ||
       !list->backend || !state->backend || !list->device->renderer_ops ||
       !list->device->renderer_ops->set_pipeline_state) {
      d12_umd_report_command_list_error(list, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->set_pipeline_state(list->backend,
                                                   state->backend);
}

void
d12_umd_install_pipeline_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table)
{
   table->pfnCalcPrivateElementLayoutSize =
      d12_umd_calc_private_element_layout_size;
   table->pfnCreateElementLayout = d12_umd_create_element_layout;
   table->pfnDestroyElementLayout = d12_umd_destroy_element_layout;
   table->pfnCalcPrivateBlendStateSize = d12_umd_calc_private_blend_state_size;
   table->pfnCreateBlendState = d12_umd_create_blend_state;
   table->pfnDestroyBlendState = d12_umd_destroy_blend_state;
   table->pfnCalcPrivateDepthStencilStateSize =
      d12_umd_calc_private_depth_stencil_state_size;
   table->pfnCreateDepthStencilState = d12_umd_create_depth_stencil_state;
   table->pfnDestroyDepthStencilState = d12_umd_destroy_depth_stencil_state;
   table->pfnCalcPrivateRasterizerStateSize =
      d12_umd_calc_private_rasterizer_state_size;
   table->pfnCreateRasterizerState = d12_umd_create_rasterizer_state;
   table->pfnDestroyRasterizerState = d12_umd_destroy_rasterizer_state;
   table->pfnCalcPrivateShaderSize = d12_umd_calc_private_shader_size;
   table->pfnCreateVertexShader = d12_umd_create_vertex_shader;
   table->pfnCreatePixelShader = d12_umd_create_pixel_shader;
   table->pfnCreateGeometryShader = d12_umd_create_geometry_shader;
   table->pfnCreateComputeShader = d12_umd_create_compute_shader;
   table->pfnCalcPrivateGeometryShaderWithStreamOutput =
      d12_umd_calc_private_geometry_shader_with_stream_output_size;
   table->pfnCreateGeometryShaderWithStreamOutput =
      d12_umd_create_geometry_shader_with_stream_output;
   table->pfnCalcPrivateTessellationShaderSize =
      d12_umd_calc_private_tessellation_shader_size;
   table->pfnCreateHullShader = d12_umd_create_hull_shader;
   table->pfnCreateDomainShader = d12_umd_create_domain_shader;
   table->pfnDestroyShader = d12_umd_destroy_shader;
   table->pfnCalcPrivatePipelineStateSize =
      d12_umd_calc_private_pipeline_state_size;
   table->pfnCreatePipelineState = d12_umd_create_pipeline_state;
   table->pfnDestroyPipelineState = d12_umd_destroy_pipeline_state;
   table->pfnRetrieveShaderComment = d12_umd_retrieve_shader_comment;
   table->pfnSerializeObject = d12_umd_serialize_object;
   table->pfnDestroyObjectSerialization =
      d12_umd_destroy_object_serialization;
   table->pfnCalcPrivateDeserializedObjectSize =
      d12_umd_calc_private_deserialized_object_size;
   table->pfnCreateDeserializedObject = d12_umd_create_deserialized_object;
}

void
d12_umd_install_pipeline_device_functions_0010(
   D3D12DDI_DEVICE_FUNCS_CORE_0010 *table)
{
   table->pfnCalcPrivateElementLayoutSize =
      d12_umd_calc_private_element_layout_size_0010;
   table->pfnCreateElementLayout = d12_umd_create_element_layout_0010;
   table->pfnCalcPrivateBlendStateSize =
      d12_umd_calc_private_blend_state_size_0010;
   table->pfnCreateBlendState = d12_umd_create_blend_state_0010;
   table->pfnCalcPrivateDepthStencilStateSize =
      d12_umd_calc_private_depth_stencil_state_size_0010;
   table->pfnCreateDepthStencilState = d12_umd_create_depth_stencil_state_0010;
   table->pfnCalcPrivateRasterizerStateSize =
      d12_umd_calc_private_rasterizer_state_size_0010;
   table->pfnCreateRasterizerState = d12_umd_create_rasterizer_state_0010;
   table->pfnCalcPrivateShaderSize = d12_umd_calc_private_shader_size_0010;
   table->pfnCreateVertexShader = d12_umd_create_vertex_shader_0010;
   table->pfnCreatePixelShader = d12_umd_create_pixel_shader_0010;
   table->pfnCreateGeometryShader = d12_umd_create_geometry_shader_0010;
   table->pfnCreateComputeShader = d12_umd_create_compute_shader_0010;
   table->pfnCalcPrivateGeometryShaderWithStreamOutput =
      d12_umd_calc_private_geometry_shader_with_stream_output_size_0010;
   table->pfnCreateGeometryShaderWithStreamOutput =
      d12_umd_create_geometry_shader_with_stream_output_0010;
   table->pfnCalcPrivateTessellationShaderSize =
      d12_umd_calc_private_tessellation_shader_size_0010;
   table->pfnCreateHullShader = d12_umd_create_hull_shader_0010;
   table->pfnCreateDomainShader = d12_umd_create_domain_shader_0010;
   table->pfnCalcPrivatePipelineStateSize =
      d12_umd_calc_private_pipeline_state_size_0010;
   table->pfnCreatePipelineState = d12_umd_create_pipeline_state_0010;
   table->pfnCalcPrivatePipelineLibrarySize =
      d12_umd_calc_private_pipeline_library_size_0010;
   table->pfnCreatePipelineLibrary = d12_umd_create_pipeline_library_0010;
   table->pfnDestroyPipelineLibrary = d12_umd_destroy_pipeline_library_0010;
   table->pfnAddPipelineStateToLibrary =
      d12_umd_add_pipeline_state_to_library_0010;
   table->pfnCalcSerializedLibrarySize =
      d12_umd_calc_serialized_library_size_0010;
   table->pfnSerializeLibrary = d12_umd_serialize_library_0010;
}

#if D12_UMD_TARGETS_WDDM23
void
d12_umd_install_pipeline_device_functions_0030(
   D3D12DDI_DEVICE_FUNCS_CORE_0030 *table)
{
   table->pfnCalcPrivateDepthStencilStateSize =
      d12_umd_calc_private_depth_stencil_state_size_0025;
   table->pfnCreateDepthStencilState =
      d12_umd_create_depth_stencil_state_0025;
   table->pfnCalcPrivateShaderSize = d12_umd_calc_private_shader_size_0026;
   table->pfnCreateVertexShader = d12_umd_create_vertex_shader_0026;
   table->pfnCreatePixelShader = d12_umd_create_pixel_shader_0026;
   table->pfnCreateGeometryShader = d12_umd_create_geometry_shader_0026;
   table->pfnCreateComputeShader = d12_umd_create_compute_shader_0026;
   table->pfnCalcPrivateGeometryShaderWithStreamOutput =
      d12_umd_calc_private_geometry_shader_with_stream_output_size_0026;
   table->pfnCreateGeometryShaderWithStreamOutput =
      d12_umd_create_geometry_shader_with_stream_output_0026;
   table->pfnCalcPrivateTessellationShaderSize =
      d12_umd_calc_private_tessellation_shader_size_0026;
   table->pfnCreateHullShader = d12_umd_create_hull_shader_0026;
   table->pfnCreateDomainShader = d12_umd_create_domain_shader_0026;
   table->pfnCreatePipelineState = d12_umd_create_pipeline_state_0021;
}
#endif

#if D12_UMD_TARGETS_WDDM24
void
d12_umd_install_pipeline_device_functions_0040(
   D3D12DDI_DEVICE_FUNCS_CORE_0040 *table)
{
   table->pfnCalcPrivatePipelineStateSize =
      d12_umd_calc_private_pipeline_state_size_0033;
   table->pfnCreatePipelineState = d12_umd_create_pipeline_state_0033;
}
#endif

#if D12_UMD_TARGETS_WDDM31
void
d12_umd_install_pipeline_device_functions_0099(
   D3D12DDI_DEVICE_FUNCS_CORE_0099 *table)
{
   table->pfnCalcPrivateDepthStencilStateSize =
      d12_umd_calc_private_depth_stencil_state_size_0095;
   table->pfnCreateDepthStencilState =
      d12_umd_create_depth_stencil_state_0095;
   table->pfnCalcPrivateRasterizerStateSize =
      d12_umd_calc_private_rasterizer_state_size_0099;
   table->pfnCreateRasterizerState = d12_umd_create_rasterizer_state_0099;
   table->pfnCalcPrivatePipelineStateSize =
      d12_umd_calc_private_pipeline_state_size_0099;
   table->pfnCreatePipelineState = d12_umd_create_pipeline_state_0099;
}
#endif

void
d12_umd_install_pipeline_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table)
{
   table->pfnSetPipelineState = d12_umd_set_pipeline_state;
}
