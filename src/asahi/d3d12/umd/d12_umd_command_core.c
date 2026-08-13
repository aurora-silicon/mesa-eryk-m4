/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_command_core.h"

bool
d12_umd_command_type_from_ddi(
   uint32_t list_type, uint32_t queue_flags,
   enum d12_umd_renderer_command_type *renderer_type)
{
   if (!renderer_type || !queue_flags ||
       (queue_flags & ~D12_UMD_DDI_QUEUE_SUPPORTED_MASK))
      return false;

   if (list_type == D12_UMD_DDI_COMMAND_LIST_BUNDLE) {
      if (queue_flags != D12_UMD_DDI_QUEUE_3D)
         return false;
      *renderer_type = D12_UMD_RENDERER_COMMAND_BUNDLE;
      return true;
   }

   if (list_type != D12_UMD_DDI_COMMAND_LIST_DIRECT)
      return false;

   /* QueueFlags describes engine capabilities. A 3D-capable queue is the
    * public DIRECT class even when it also exposes compute/copy; otherwise
    * choose the strongest remaining class.
    */
   if (queue_flags & D12_UMD_DDI_QUEUE_3D)
      *renderer_type = D12_UMD_RENDERER_COMMAND_DIRECT;
   else if (queue_flags & D12_UMD_DDI_QUEUE_COMPUTE)
      *renderer_type = D12_UMD_RENDERER_COMMAND_COMPUTE;
   else
      *renderer_type = D12_UMD_RENDERER_COMMAND_COPY;
   return true;
}

bool
d12_umd_command_list_flags_valid(uint32_t flags, bool release1)
{
   uint32_t supported = D12_UMD_DDI_COMMAND_LIST_ENABLE_MARKERS;

   if (release1)
      supported |= D12_UMD_DDI_COMMAND_LIST_ENABLE_FULL_PIPELINE_MARKERS;

   return !(flags & ~supported);
}

/* Public and DDI indirect enums deliberately share these baseline values.
 * d12_umd_winabi_tests.c pins them to the selected WDK. */
enum {
   D12_INDIRECT_DRAW = 0,
   D12_INDIRECT_DRAW_INDEXED = 1,
   D12_INDIRECT_DISPATCH = 2,
   D12_INDIRECT_VERTEX_BUFFER_VIEW = 3,
   D12_INDIRECT_INDEX_BUFFER_VIEW = 4,
   D12_INDIRECT_CONSTANT = 5,
   D12_INDIRECT_CONSTANT_BUFFER_VIEW = 6,
   D12_INDIRECT_SHADER_RESOURCE_VIEW = 7,
   D12_INDIRECT_UNORDERED_ACCESS_VIEW = 8,
};

enum {
   D12_ROOT_32BIT_CONSTANTS = 1,
   D12_ROOT_CBV = 2,
   D12_ROOT_SRV = 3,
   D12_ROOT_UAV = 4,
};

bool
d12_umd_command_signature_desc_valid(
   const struct d12_frontend_command_signature_desc *desc,
   const struct d12_frontend_root_signature_desc *root_signature)
{
   size_t packed_size = 0;
   bool saw_terminal = false;
   bool changes_root_arguments = false;
   bool has_root_reference = false;
   bool last_root_was_constant = false;
   bool has_index_buffer = false;
   uint32_t terminal_type = UINT32_MAX;
   uint32_t last_root_parameter = 0;
   uint32_t last_constant_end = 0;
   uint32_t vertex_buffer_mask = 0;

   if (!desc || !desc->argument_count || desc->argument_count > 65 ||
       !desc->arguments || desc->node_mask > 1)
      return false;

   for (uint32_t index = 0; index < desc->argument_count; ++index) {
      const struct d12_frontend_indirect_argument_desc *argument =
         &desc->arguments[index];
      size_t element_size;
      bool terminal = false;

      switch (argument->type) {
      case D12_INDIRECT_DRAW:
         element_size = 16;
         terminal = true;
         break;
      case D12_INDIRECT_DRAW_INDEXED:
         element_size = 20;
         terminal = true;
         break;
      case D12_INDIRECT_DISPATCH:
         element_size = 12;
         terminal = true;
         break;
      case D12_INDIRECT_CONSTANT: {
         uint32_t root_parameter = argument->root_parameter_index;
         uint32_t constant_start = argument->destination_offset;
         const struct d12_frontend_root_parameter *parameter;

         if (!root_signature || !argument->value_count ||
             root_parameter >= root_signature->num_parameters ||
             !root_signature->parameters)
            return false;
         parameter = &root_signature->parameters[root_parameter];
         if (parameter->parameter_type != D12_ROOT_32BIT_CONSTANTS ||
             constant_start > parameter->constants.num_32bit_values ||
             argument->value_count >
                parameter->constants.num_32bit_values - constant_start ||
             argument->value_count > (SIZE_MAX - packed_size) / 4)
            return false;
         if ((has_root_reference &&
              root_parameter < last_root_parameter) ||
             (has_root_reference &&
              root_parameter == last_root_parameter &&
              (!last_root_was_constant ||
               constant_start < last_constant_end)))
            return false;

         element_size = (size_t)argument->value_count * 4;
         changes_root_arguments = true;
         has_root_reference = true;
         last_root_was_constant = true;
         last_root_parameter = root_parameter;
         last_constant_end = constant_start + argument->value_count;
         break;
      }
      case D12_INDIRECT_CONSTANT_BUFFER_VIEW:
      case D12_INDIRECT_SHADER_RESOURCE_VIEW:
      case D12_INDIRECT_UNORDERED_ACCESS_VIEW: {
         uint32_t root_parameter = argument->root_parameter_index;
         uint32_t expected =
            argument->type == D12_INDIRECT_CONSTANT_BUFFER_VIEW
               ? D12_ROOT_CBV
            : argument->type == D12_INDIRECT_SHADER_RESOURCE_VIEW
               ? D12_ROOT_SRV
               : D12_ROOT_UAV;

         if (!root_signature || !root_signature->parameters ||
             root_parameter >= root_signature->num_parameters ||
             root_signature->parameters[root_parameter].parameter_type !=
                expected ||
             (has_root_reference &&
              root_parameter <= last_root_parameter))
            return false;

         element_size = 8;
         changes_root_arguments = true;
         has_root_reference = true;
         last_root_was_constant = false;
         last_root_parameter = root_parameter;
         break;
      }
      case D12_INDIRECT_VERTEX_BUFFER_VIEW: {
         uint32_t slot = argument->root_parameter_index;
         if (slot >= D12_FRONTEND_MAX_VERTEX_BUFFERS ||
             (vertex_buffer_mask & (UINT32_C(1) << slot)))
            return false;
         vertex_buffer_mask |= UINT32_C(1) << slot;
         element_size = 16;
         break;
      }
      case D12_INDIRECT_INDEX_BUFFER_VIEW:
         if (has_index_buffer)
            return false;
         has_index_buffer = true;
         element_size = 16;
         break;
      default:
         return false;
      }

      if (terminal) {
         if (saw_terminal || index + 1 != desc->argument_count)
            return false;
         saw_terminal = true;
         terminal_type = argument->type;
      }
      if (element_size > SIZE_MAX - packed_size)
         return false;
      packed_size += element_size;
   }

   return saw_terminal &&
          !(terminal_type == D12_INDIRECT_DISPATCH &&
            (vertex_buffer_mask || has_index_buffer)) &&
          !(has_index_buffer &&
            terminal_type != D12_INDIRECT_DRAW_INDEXED) &&
          ((root_signature != NULL) == changes_root_arguments) &&
          desc->byte_stride >= packed_size && !(desc->byte_stride & 3);
}
