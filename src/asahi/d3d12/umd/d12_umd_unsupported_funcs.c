/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Exact-signature, fail-closed adapters for Release-8 features which the
 * native AGX backend does not advertise. Keeping these entries non-NULL lets
 * the runtime validate a complete DDI 0099 table without routing a typed call
 * through an ABI-incompatible catch-all thunk.
 */

#include "d12_umd_unsupported_funcs.h"

#include "d12_umd_internal.h"

#include <string.h>

#if D12_UMD_TARGETS_WDDM31

static struct d12_umd_device *
d12_umd_note_unsupported_device(D3D12DDI_HDEVICE handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);

   if (device)
      InterlockedIncrement(&device->unimplemented_hits);
   return device;
}

static void
d12_umd_set_unsupported_device_error(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_device_error(d12_umd_note_unsupported_device(handle),
                               E_NOTIMPL);
}

static void
d12_umd_set_unsupported_command_list_error(D3D12DDI_HCOMMANDLIST handle)
{
   d12_umd_report_command_list_error(
      d12_umd_command_list_from_handle(handle), E_NOTIMPL);
}

static SIZE_T APIENTRY
d12_umd_unsupported_calc_scheduling_group_size(
   D3D12DDI_HDEVICE device,
   const D3D12DDIARG_CREATESCHEDULINGGROUP_0050 *arguments)
{
   (void)arguments;
   d12_umd_note_unsupported_device(device);
   return 0;
}

static HRESULT APIENTRY
d12_umd_unsupported_create_scheduling_group(
   D3D12DDI_HDEVICE device,
   const D3D12DDIARG_CREATESCHEDULINGGROUP_0050 *arguments,
   D3D12DDI_HSCHEDULINGGROUP_0050 scheduling_group,
   D3D12DDI_HRTSCHEDULINGGROUP_0050 runtime_scheduling_group)
{
   (void)arguments;
   (void)scheduling_group;
   (void)runtime_scheduling_group;
   d12_umd_note_unsupported_device(device);
   return E_NOTIMPL;
}

static void APIENTRY
d12_umd_unsupported_destroy_scheduling_group(
   D3D12DDI_HDEVICE device,
   D3D12DDI_HSCHEDULINGGROUP_0050 scheduling_group)
{
   (void)device;
   (void)scheduling_group;
}

static HRESULT APIENTRY
d12_umd_unsupported_enumerate_meta_commands(
   D3D12DDI_HDEVICE device, UINT *count,
   D3D12DDIARG_META_COMMAND_DESC *descriptions)
{
   (void)device;
   (void)descriptions;
   if (!count)
      return E_INVALIDARG;
   *count = 0;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_unsupported_enumerate_meta_command_parameters(
   D3D12DDI_HDEVICE device, GUID command_id,
   D3D12DDI_META_COMMAND_PARAMETER_STAGE stage, UINT *count,
   D3D12DDIARG_META_COMMAND_PARAMETER_DESC *descriptions)
{
   (void)device;
   (void)command_id;
   (void)stage;
   (void)descriptions;
   if (!count)
      return E_INVALIDARG;
   *count = 0;
   return S_OK;
}

static SIZE_T APIENTRY
d12_umd_unsupported_calc_meta_command_size(
   D3D12DDI_HDEVICE device, GUID command_id, UINT node_mask,
   const void *parameters, SIZE_T parameter_size)
{
   (void)command_id;
   (void)node_mask;
   (void)parameters;
   (void)parameter_size;
   d12_umd_note_unsupported_device(device);
   return 0;
}

static HRESULT APIENTRY
d12_umd_unsupported_create_meta_command(
   D3D12DDI_HDEVICE device, GUID command_id, UINT node_mask,
   const void *parameters, SIZE_T parameter_size,
   D3D12DDI_HMETACOMMAND_0052 meta_command,
   D3D12DDI_HRTMETACOMMAND_0052 runtime_meta_command)
{
   (void)command_id;
   (void)node_mask;
   (void)parameters;
   (void)parameter_size;
   (void)meta_command;
   (void)runtime_meta_command;
   d12_umd_note_unsupported_device(device);
   return E_NOTIMPL;
}

static void APIENTRY
d12_umd_unsupported_destroy_meta_command(
   D3D12DDI_HDEVICE device, D3D12DDI_HMETACOMMAND_0052 meta_command)
{
   (void)device;
   (void)meta_command;
}

static void APIENTRY
d12_umd_unsupported_get_meta_command_required_parameter_info(
   D3D12DDI_HMETACOMMAND_0052 meta_command,
   D3D12DDI_META_COMMAND_PARAMETER_STAGE stage, UINT parameter_index,
   D3D12DDIARG_META_COMMAND_REQUIRED_PARAMETER_INFO *information)
{
   (void)meta_command;
   (void)stage;
   (void)parameter_index;
   if (information)
      memset(information, 0, sizeof(*information));
}

static SIZE_T APIENTRY
d12_umd_unsupported_calc_state_object_size(
   D3D12DDI_HDEVICE device,
   const D3D12DDIARG_CREATE_STATE_OBJECT_0054 *arguments)
{
   (void)arguments;
   d12_umd_note_unsupported_device(device);
   return 0;
}

static HRESULT APIENTRY
d12_umd_unsupported_create_state_object(
   D3D12DDI_HDEVICE device,
   const D3D12DDIARG_CREATE_STATE_OBJECT_0054 *arguments,
   D3D12DDI_HSTATEOBJECT_0054 state_object,
   D3D12DDI_HRTSTATEOBJECT_0054 runtime_state_object)
{
   (void)arguments;
   (void)state_object;
   (void)runtime_state_object;
   d12_umd_note_unsupported_device(device);
   return E_NOTIMPL;
}

static void APIENTRY
d12_umd_unsupported_destroy_state_object(
   D3D12DDI_HDEVICE device, D3D12DDI_HSTATEOBJECT_0054 state_object)
{
   (void)device;
   (void)state_object;
}

static void APIENTRY
d12_umd_unsupported_get_raytracing_prebuild_info(
   D3D12DDI_HDEVICE device,
   const D3D12DDI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_0054
      *inputs,
   D3D12DDI_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_0054
      *information)
{
   (void)inputs;
   if (information)
      memset(information, 0, sizeof(*information));
   d12_umd_set_unsupported_device_error(device);
}

static D3D12DDI_DRIVER_MATCHING_IDENTIFIER_STATUS APIENTRY
d12_umd_unsupported_check_driver_matching_identifier(
   D3D12DDI_HDEVICE device, D3D12DDI_SERIALIZED_DATA_TYPE data_type,
   const D3D12DDI_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER_0054
      *identifier)
{
   (void)data_type;
   (void)identifier;
   d12_umd_note_unsupported_device(device);
   return D3D12DDI_DRIVER_MATCHING_IDENTIFIER_UNSUPPORTED_TYPE;
}

static void *APIENTRY
d12_umd_unsupported_get_shader_identifier(
   D3D12DDI_HSTATEOBJECT_0054 state_object, LPCWSTR export_name)
{
   (void)state_object;
   (void)export_name;
   return NULL;
}

static UINT APIENTRY
d12_umd_unsupported_get_shader_stack_size(
   D3D12DDI_HSTATEOBJECT_0054 state_object, LPCWSTR export_name)
{
   (void)state_object;
   (void)export_name;
   return 0;
}

static UINT APIENTRY
d12_umd_unsupported_get_pipeline_stack_size(
   D3D12DDI_HSTATEOBJECT_0054 state_object)
{
   (void)state_object;
   return 0;
}

static void APIENTRY
d12_umd_unsupported_set_pipeline_stack_size(
   D3D12DDI_HSTATEOBJECT_0054 state_object, UINT stack_size)
{
   (void)state_object;
   (void)stack_size;
}

static void APIENTRY
d12_umd_unsupported_set_background_processing_mode(
   D3D12DDI_HDEVICE device,
   D3D12DDI_BACKGROUND_PROCESSING_MODE_0062 mode,
   D3D12DDI_MEASUREMENTS_ACTION_0062 measurements_action,
   BOOL *further_measurements_desired)
{
   (void)mode;
   (void)measurements_action;
   if (further_measurements_desired)
      *further_measurements_desired = FALSE;
   (void)device;
}

static SIZE_T APIENTRY
d12_umd_unsupported_calc_add_to_state_object_size(
   D3D12DDI_HDEVICE device,
   const D3D12DDIARG_ADD_TO_STATE_OBJECT_0072 *arguments)
{
   (void)arguments;
   d12_umd_note_unsupported_device(device);
   return 0;
}

static HRESULT APIENTRY
d12_umd_unsupported_add_to_state_object(
   D3D12DDI_HDEVICE device,
   const D3D12DDIARG_ADD_TO_STATE_OBJECT_0072 *arguments,
   D3D12DDI_HSTATEOBJECT_0054 state_object,
   D3D12DDI_HRTSTATEOBJECT_0054 runtime_state_object)
{
   (void)arguments;
   (void)state_object;
   (void)runtime_state_object;
   d12_umd_note_unsupported_device(device);
   return E_NOTIMPL;
}

static void APIENTRY
d12_umd_unsupported_create_sampler_feedback_uav(
   D3D12DDI_HDEVICE device, D3D12DDI_HRESOURCE targeted_resource,
   D3D12DDI_HRESOURCE feedback_resource,
   D3D12DDI_CPU_DESCRIPTOR_HANDLE destination)
{
   (void)targeted_resource;
   (void)feedback_resource;
   (void)destination;
   d12_umd_set_unsupported_device_error(device);
}

static SIZE_T APIENTRY
d12_umd_unsupported_calc_mesh_shader_size(
   D3D12DDI_HDEVICE device,
   const D3D12DDIARG_CREATE_SHADER_0026 *arguments)
{
   (void)arguments;
   d12_umd_note_unsupported_device(device);
   return 0;
}

static void APIENTRY
d12_umd_unsupported_create_mesh_shader(
   D3D12DDI_HDEVICE device,
   const D3D12DDIARG_CREATE_SHADER_0026 *arguments,
   D3D12DDI_HSHADER shader)
{
   (void)arguments;
   (void)shader;
   d12_umd_set_unsupported_device_error(device);
}

static void APIENTRY
d12_umd_unsupported_implicit_shader_cache_control(
   D3D12DDI_HDEVICE device,
   D3D12DDI_IMPLICIT_SHADER_CACHE_CONTROL_FLAGS_0080 flags)
{
   (void)device;
   (void)flags;
}

static void APIENTRY
d12_umd_unsupported_meta_command(
   D3D12DDI_HCOMMANDLIST command_list,
   D3D12DDI_HMETACOMMAND_0052 meta_command, const void *parameters,
   SIZE_T parameter_size)
{
   (void)meta_command;
   (void)parameters;
   (void)parameter_size;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_build_raytracing_acceleration_structure(
   D3D12DDI_HCOMMANDLIST command_list,
   const D3D12DDIARG_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_0054
      *arguments)
{
   (void)arguments;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_emit_raytracing_postbuild_info(
   D3D12DDI_HCOMMANDLIST command_list,
   const D3D12DDIARG_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_0054
      *arguments)
{
   (void)arguments;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_copy_raytracing_acceleration_structure(
   D3D12DDI_HCOMMANDLIST command_list,
   const D3D12DDIARG_COPY_RAYTRACING_ACCELERATION_STRUCTURE_0054
      *arguments)
{
   (void)arguments;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_set_pipeline_state1(
   D3D12DDI_HCOMMANDLIST command_list,
   D3D12DDI_HSTATEOBJECT_0054 state_object)
{
   (void)state_object;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_dispatch_rays(
   D3D12DDI_HCOMMANDLIST command_list,
   const D3D12DDIARG_DISPATCH_RAYS_0054 *arguments)
{
   (void)arguments;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_set_shading_rate(
   D3D12DDI_HCOMMANDLIST command_list,
   D3D12DDI_SHADING_RATE_0062 shading_rate,
   const D3D12DDI_SHADING_RATE_COMBINER_0062 *combiners)
{
   (void)shading_rate;
   (void)combiners;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_set_shading_rate_image(
   D3D12DDI_HCOMMANDLIST command_list, D3D12DDI_HRESOURCE resource)
{
   (void)resource;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_dispatch_mesh(D3D12DDI_HCOMMANDLIST command_list,
                                  UINT x, UINT y, UINT z)
{
   (void)x;
   (void)y;
   (void)z;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_barrier(
   D3D12DDI_HCOMMANDLIST command_list, UINT32 barrier_count,
   const D3D12DDIARG_BARRIER_0094 *barriers)
{
   (void)barrier_count;
   (void)barriers;
   d12_umd_set_unsupported_command_list_error(command_list);
}

static void APIENTRY
d12_umd_unsupported_alpha_blend_factor(
   D3D12DDI_HCOMMANDLIST command_list, FLOAT alpha_blend_factor)
{
   (void)command_list;
   (void)alpha_blend_factor;
}

static HRESULT APIENTRY
d12_umd_unsupported_get_extended_features_0096(
   D3D12DDI_HDEVICE device,
   D3D12DDI_FEATURE_0020 highest_allowed_feature, UINT32 *feature_count,
   D3D12DDI_FEATURE_0020 *features)
{
   (void)highest_allowed_feature;
   (void)features;
   if (!d12_umd_device_from_handle(device) || !feature_count)
      return E_INVALIDARG;
   *feature_count = 0;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_unsupported_get_extended_feature_versions(
   D3D12DDI_HDEVICE device, D3D12DDI_FEATURE_0020 feature,
   UINT32 *version_count, UINT32 *versions)
{
   (void)feature;
   (void)versions;
   if (!d12_umd_device_from_handle(device) || !version_count)
      return E_INVALIDARG;
   *version_count = 0;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_unsupported_enable_extended_feature(
   D3D12DDI_HDEVICE device, D3D12DDI_FEATURE_0020 feature,
   UINT32 feature_version)
{
   (void)feature;
   (void)feature_version;
   if (!d12_umd_device_from_handle(device))
      return E_INVALIDARG;
   d12_umd_note_unsupported_device(device);
   return E_NOTIMPL;
}

static HRESULT APIENTRY
d12_umd_unsupported_set_extended_feature_callbacks(
   D3D12DDI_HDEVICE device, D3D12DDI_TABLE_TYPE table_type,
   const void *callbacks, SIZE_T callbacks_size)
{
   (void)table_type;
   (void)callbacks;
   (void)callbacks_size;
   if (!d12_umd_device_from_handle(device))
      return E_INVALIDARG;
   d12_umd_note_unsupported_device(device);
   return E_NOTIMPL;
}

void
d12_umd_install_unsupported_device_functions_0099(
   D3D12DDI_DEVICE_FUNCS_CORE_0099 *table)
{
   table->pfnCalcPrivateSchedulingGroupSize =
      d12_umd_unsupported_calc_scheduling_group_size;
   table->pfnCreateSchedulingGroup =
      d12_umd_unsupported_create_scheduling_group;
   table->pfnDestroySchedulingGroup =
      d12_umd_unsupported_destroy_scheduling_group;
   table->pfnEnumerateMetaCommands =
      d12_umd_unsupported_enumerate_meta_commands;
   table->pfnEnumerateMetaCommandParameters =
      d12_umd_unsupported_enumerate_meta_command_parameters;
   table->pfnCalcPrivateMetaCommandSize =
      d12_umd_unsupported_calc_meta_command_size;
   table->pfnCreateMetaCommand = d12_umd_unsupported_create_meta_command;
   table->pfnDestroyMetaCommand = d12_umd_unsupported_destroy_meta_command;
   table->pfnGetMetaCommandRequiredParameterInfo =
      d12_umd_unsupported_get_meta_command_required_parameter_info;
   table->pfnCalcPrivateStateObjectSize =
      d12_umd_unsupported_calc_state_object_size;
   table->pfnCreateStateObject = d12_umd_unsupported_create_state_object;
   table->pfnDestroyStateObject = d12_umd_unsupported_destroy_state_object;
   table->pfnGetRaytracingAccelerationStructurePrebuildInfo =
      d12_umd_unsupported_get_raytracing_prebuild_info;
   table->pfnCheckDriverMatchingIdentifier =
      d12_umd_unsupported_check_driver_matching_identifier;
   table->pfnGetShaderIdentifier = d12_umd_unsupported_get_shader_identifier;
   table->pfnGetShaderStackSize = d12_umd_unsupported_get_shader_stack_size;
   table->pfnGetPipelineStackSize =
      d12_umd_unsupported_get_pipeline_stack_size;
   table->pfnSetPipelineStackSize =
      d12_umd_unsupported_set_pipeline_stack_size;
   table->pfnSetBackgroundProcessingMode =
      d12_umd_unsupported_set_background_processing_mode;
   table->pfnCalcPrivateAddToStateObjectSize =
      d12_umd_unsupported_calc_add_to_state_object_size;
   table->pfnAddToStateObject = d12_umd_unsupported_add_to_state_object;
   table->pfnCreateSamplerFeedbackUnorderedAccessView =
      d12_umd_unsupported_create_sampler_feedback_uav;
   table->pfnCreateAmplificationShader = d12_umd_unsupported_create_mesh_shader;
   table->pfnCreateMeshShader = d12_umd_unsupported_create_mesh_shader;
   table->pfnCalcPrivateMeshShaderSize =
      d12_umd_unsupported_calc_mesh_shader_size;
   table->pfnImplicitShaderCacheControl =
      d12_umd_unsupported_implicit_shader_cache_control;
}

void
d12_umd_install_unsupported_command_list_functions_0099(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0099 *table)
{
   table->pfnInitializeMetaCommand = d12_umd_unsupported_meta_command;
   table->pfnExecuteMetaCommand = d12_umd_unsupported_meta_command;
   table->pfnBuildRaytracingAccelerationStructure =
      d12_umd_unsupported_build_raytracing_acceleration_structure;
   table->pfnEmitRaytracingAccelerationStructurePostbuildInfo =
      d12_umd_unsupported_emit_raytracing_postbuild_info;
   table->pfnCopyRaytracingAccelerationStructure =
      d12_umd_unsupported_copy_raytracing_acceleration_structure;
   table->pfnSetPipelineState1 = d12_umd_unsupported_set_pipeline_state1;
   table->pfnDispatchRays = d12_umd_unsupported_dispatch_rays;
   table->pfnRSSetShadingRate = d12_umd_unsupported_set_shading_rate;
   table->pfnRSSetShadingRateImage =
      d12_umd_unsupported_set_shading_rate_image;
   table->pfnDispatchMesh = d12_umd_unsupported_dispatch_mesh;
   table->pfnBarrier = d12_umd_unsupported_barrier;
   table->pfnOmSetAlphaBlendFactor =
      d12_umd_unsupported_alpha_blend_factor;
}

HRESULT
d12_umd_fill_unsupported_extended_features_table_0096(
   void *raw_table, SIZE_T table_size, UINT table_number)
{
   D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 *table = raw_table;

   _Static_assert(sizeof(D3D12DDI_EXTENDED_FEATURES_FUNCS_0096) ==
                     4u * sizeof(void *),
                  "DDI 0096 extended-feature table width changed");

   if (!table || table_size != sizeof(*table) || table_number != 0)
      return E_INVALIDARG;
   memset(table, 0, sizeof(*table));
   table->pfnGetSupportedExtendedFeatures =
      d12_umd_unsupported_get_extended_features_0096;
   table->pfnGetSupportedExtendedFeatureVersions =
      d12_umd_unsupported_get_extended_feature_versions;
   table->pfnEnableExtendedFeature =
      d12_umd_unsupported_enable_extended_feature;
   table->pfnSetExtendedFeatureCallbacks =
      d12_umd_unsupported_set_extended_feature_callbacks;
   return S_OK;
}

#endif
