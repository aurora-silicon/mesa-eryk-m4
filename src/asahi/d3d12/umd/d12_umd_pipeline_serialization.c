/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_pipeline_serialization.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define D12_UMD_SERIALIZED_MAX_ROOT_PARAMETERS 64u
#define D12_UMD_SERIALIZED_MAX_ROOT_RANGES 1024u
#define D12_UMD_SERIALIZED_MAX_STATIC_SAMPLERS 64u
#define D12_UMD_SERIALIZED_MAX_ROOT_DWORDS 64u
#define D12_UMD_SERIALIZED_ROOT_FLAGS_MASK 0x7fu
#define D12_UMD_SERIALIZED_MAX_SHADER_BYTES (64u * 1024u * 1024u)
#define D12_UMD_SERIALIZED_MAX_TOTAL_BYTES (256u * 1024u * 1024u)

static const uint8_t d12_umd_pipeline_magic[8] = {
   'D', '1', '2', 'A', 'G', 'X', 'P', 'S',
};

struct d12_umd_serialized_root_parameter {
   uint32_t parameter_type;
   uint32_t shader_visibility;
   uint32_t range_start;
   uint32_t range_count;
   uint32_t data0;
   uint32_t data1;
   uint32_t data2;
};

/* All serialized state is fixed-width and pointer-free. Float fields are
 * stored as their uint32_t bit patterns inside the state arrays. */
struct d12_umd_pipeline_blob_header {
   uint8_t magic[8];
   uint32_t version;
   uint32_t header_size;
   uint64_t total_size;
   uint64_t blob_hash;
   uint64_t shader_sizes[5];

   uint32_t kind;
   uint32_t has_root_signature;
   uint32_t root_parameter_count;
   uint32_t root_range_count;
   uint32_t root_static_sampler_count;
   uint32_t root_flags;
   uint32_t element_count;
   uint32_t so_declaration_count;
   uint32_t so_stride_count;
   uint32_t so_rasterized_stream;
   uint32_t index_buffer_strip_cut_value;
   uint32_t primitive_topology_type;
   uint32_t num_render_targets;
   uint32_t rtv_formats[D12_FRONTEND_MAX_RENDER_TARGETS];
   uint32_t dsv_format;
   uint32_t sample_count;
   uint32_t sample_quality;
   uint32_t sample_mask;
   uint32_t blend[82];
   uint32_t rasterizer[12];
   uint32_t depth_stencil[16];
   uint32_t reserved[1];
};

_Static_assert(sizeof(struct d12_umd_serialized_root_parameter) == 28,
               "serialized root parameter width changed");
_Static_assert(sizeof(struct d12_frontend_descriptor_range) == 20,
               "serialized descriptor range width changed");
_Static_assert(sizeof(struct d12_frontend_static_sampler) == 52,
               "serialized static sampler width changed");
_Static_assert(sizeof(struct d12_frontend_input_element) == 24,
               "serialized input element width changed");
_Static_assert(sizeof(struct d12_frontend_stream_output_declaration) == 24,
               "serialized stream-output declaration width changed");
_Static_assert(sizeof(struct d12_umd_pipeline_blob_header) == 616,
               "serialized pipeline header width changed");

static bool
d12_umd_size_add(size_t *total, size_t count, size_t element_size)
{
   if (!total || (element_size && count > (SIZE_MAX - *total) / element_size))
      return false;
   *total += count * element_size;
   return true;
}

/* Hash the complete serialized representation while treating the hash field
 * itself as zero. This protects fixed-header pipeline state as well as the
 * variable payload and keeps validation allocation-free. */
static uint64_t
d12_umd_pipeline_blob_hash(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   const size_t hash_start =
      offsetof(struct d12_umd_pipeline_blob_header, blob_hash);
   const size_t hash_end = hash_start + sizeof(uint64_t);
   uint64_t hash = UINT64_C(14695981039346656037);

   for (size_t index = 0; index < size; ++index) {
      const uint8_t byte = index >= hash_start && index < hash_end
                              ? 0
                              : bytes[index];
      hash ^= byte;
      hash *= UINT64_C(1099511628211);
   }
   return hash;
}

static uint32_t
d12_umd_float_bits(float value)
{
   uint32_t bits;
   memcpy(&bits, &value, sizeof(bits));
   return bits;
}

static float
d12_umd_bits_float(uint32_t bits)
{
   float value;
   memcpy(&value, &bits, sizeof(value));
   return value;
}

static void
d12_umd_pack_blend(uint32_t destination[82],
                   const struct d12_frontend_blend_desc *source)
{
   destination[0] = source->alpha_to_coverage_enable;
   destination[1] = source->independent_blend_enable;
   for (uint32_t index = 0; index < D12_FRONTEND_MAX_RENDER_TARGETS;
        ++index) {
      const struct d12_frontend_render_target_blend *input =
         &source->render_targets[index];
      uint32_t *output = &destination[2u + index * 10u];
      output[0] = input->blend_enable;
      output[1] = input->logic_op_enable;
      output[2] = input->src_blend;
      output[3] = input->dest_blend;
      output[4] = input->blend_op;
      output[5] = input->src_blend_alpha;
      output[6] = input->dest_blend_alpha;
      output[7] = input->blend_op_alpha;
      output[8] = input->logic_op;
      output[9] = input->write_mask;
   }
}

static void
d12_umd_unpack_blend(struct d12_frontend_blend_desc *destination,
                     const uint32_t source[82])
{
   memset(destination, 0, sizeof(*destination));
   destination->alpha_to_coverage_enable = source[0];
   destination->independent_blend_enable = source[1];
   for (uint32_t index = 0; index < D12_FRONTEND_MAX_RENDER_TARGETS;
        ++index) {
      struct d12_frontend_render_target_blend *output =
         &destination->render_targets[index];
      const uint32_t *input = &source[2u + index * 10u];
      output->blend_enable = input[0];
      output->logic_op_enable = input[1];
      output->src_blend = input[2];
      output->dest_blend = input[3];
      output->blend_op = input[4];
      output->src_blend_alpha = input[5];
      output->dest_blend_alpha = input[6];
      output->blend_op_alpha = input[7];
      output->logic_op = input[8];
      output->write_mask = (uint8_t)input[9];
   }
}

static void
d12_umd_pack_rasterizer(
   uint32_t destination[12],
   const struct d12_frontend_rasterizer_desc *source)
{
   destination[0] = source->fill_mode;
   destination[1] = source->cull_mode;
   destination[2] = source->front_counter_clockwise;
   memcpy(&destination[3], &source->depth_bias, sizeof(source->depth_bias));
   destination[4] = d12_umd_float_bits(source->depth_bias_clamp);
   destination[5] = d12_umd_float_bits(source->slope_scaled_depth_bias);
   destination[6] = source->depth_clip_enable;
   destination[7] = source->scissor_enable;
   destination[8] = source->multisample_enable;
   destination[9] = source->antialiased_line_enable;
   destination[10] = source->forced_sample_count;
   destination[11] = source->conservative_rasterization_mode;
}

static void
d12_umd_unpack_rasterizer(
   struct d12_frontend_rasterizer_desc *destination,
   const uint32_t source[12])
{
   memset(destination, 0, sizeof(*destination));
   destination->fill_mode = source[0];
   destination->cull_mode = source[1];
   destination->front_counter_clockwise = source[2];
   memcpy(&destination->depth_bias, &source[3],
          sizeof(destination->depth_bias));
   destination->depth_bias_clamp = d12_umd_bits_float(source[4]);
   destination->slope_scaled_depth_bias = d12_umd_bits_float(source[5]);
   destination->depth_clip_enable = source[6];
   destination->scissor_enable = source[7];
   destination->multisample_enable = source[8];
   destination->antialiased_line_enable = source[9];
   destination->forced_sample_count = source[10];
   destination->conservative_rasterization_mode = source[11];
}

static void
d12_umd_pack_depth_stencil(
   uint32_t destination[16],
   const struct d12_frontend_depth_stencil_desc *source)
{
   destination[0] = source->depth_enable;
   destination[1] = source->depth_write_mask;
   destination[2] = source->depth_func;
   destination[3] = source->stencil_enable;
   destination[4] = source->front_enable;
   destination[5] = source->back_enable;
   destination[6] = source->stencil_read_mask;
   destination[7] = source->stencil_write_mask;
   destination[8] = source->front_face.stencil_fail_op;
   destination[9] = source->front_face.stencil_depth_fail_op;
   destination[10] = source->front_face.stencil_pass_op;
   destination[11] = source->front_face.stencil_func;
   destination[12] = source->back_face.stencil_fail_op;
   destination[13] = source->back_face.stencil_depth_fail_op;
   destination[14] = source->back_face.stencil_pass_op;
   destination[15] = source->back_face.stencil_func;
}

static void
d12_umd_unpack_depth_stencil(
   struct d12_frontend_depth_stencil_desc *destination,
   const struct d12_umd_pipeline_blob_header *source)
{
   memset(destination, 0, sizeof(*destination));
   destination->depth_enable = source->depth_stencil[0];
   destination->depth_write_mask = source->depth_stencil[1];
   destination->depth_func = source->depth_stencil[2];
   destination->stencil_enable = source->depth_stencil[3];
   destination->front_enable = source->depth_stencil[4];
   destination->back_enable = source->depth_stencil[5];
   destination->stencil_read_mask = (uint8_t)source->depth_stencil[6];
   destination->stencil_write_mask = (uint8_t)source->depth_stencil[7];
   destination->front_face.stencil_fail_op = source->depth_stencil[8];
   destination->front_face.stencil_depth_fail_op = source->depth_stencil[9];
   destination->front_face.stencil_pass_op = source->depth_stencil[10];
   destination->front_face.stencil_func = source->depth_stencil[11];
   destination->back_face.stencil_fail_op = source->depth_stencil[12];
   destination->back_face.stencil_depth_fail_op = source->depth_stencil[13];
   destination->back_face.stencil_pass_op = source->depth_stencil[14];
   destination->back_face.stencil_func = source->depth_stencil[15];
}

static bool
d12_umd_root_counts(const struct d12_frontend_root_signature_desc *root,
                    uint32_t *out_ranges)
{
   uint32_t ranges = 0;
   uint32_t dword_count = 0;

   if (!root) {
      *out_ranges = 0;
      return true;
   }
   if (root->num_parameters > D12_UMD_SERIALIZED_MAX_ROOT_PARAMETERS ||
       root->num_static_samplers >
          D12_UMD_SERIALIZED_MAX_STATIC_SAMPLERS ||
       (root->num_parameters && !root->parameters) ||
       (root->num_static_samplers && !root->static_samplers) ||
       (root->flags & ~D12_UMD_SERIALIZED_ROOT_FLAGS_MASK))
      return false;

   for (uint32_t index = 0; index < root->num_parameters; ++index) {
      const struct d12_frontend_root_parameter *parameter =
         &root->parameters[index];
      if (parameter->parameter_type > 4u ||
          parameter->shader_visibility > 5u)
         return false;
      if (parameter->parameter_type == 0u) {
         if ((parameter->table.num_ranges && !parameter->table.ranges) ||
             dword_count == D12_UMD_SERIALIZED_MAX_ROOT_DWORDS)
            return false;
         if (parameter->table.num_ranges >
             D12_UMD_SERIALIZED_MAX_ROOT_RANGES - ranges)
            return false;
         for (uint32_t range_index = 0;
              range_index < parameter->table.num_ranges; ++range_index) {
            const struct d12_frontend_descriptor_range *range =
               &parameter->table.ranges[range_index];
            if (range->range_type > 3u || !range->num_descriptors)
               return false;
         }
         ranges += parameter->table.num_ranges;
         ++dword_count;
      } else if (parameter->parameter_type == 1u) {
         if (!parameter->constants.num_32bit_values ||
             parameter->constants.num_32bit_values >
                D12_UMD_SERIALIZED_MAX_ROOT_DWORDS - dword_count)
            return false;
         dword_count += parameter->constants.num_32bit_values;
      } else {
         if (dword_count > D12_UMD_SERIALIZED_MAX_ROOT_DWORDS - 2u)
            return false;
         dword_count += 2u;
      }
   }
   for (uint32_t index = 0; index < root->num_static_samplers; ++index) {
      const struct d12_frontend_static_sampler *sampler =
         &root->static_samplers[index];
      if (sampler->shader_visibility > 5u || sampler->border_color > 2u)
         return false;
   }
   *out_ranges = ranges;
   return true;
}

static bool
d12_umd_pipeline_blob_size(
   const struct d12_umd_pipeline_blob_header *header, size_t *out_size)
{
   size_t size = sizeof(*header);

   if (!d12_umd_size_add(&size, header->root_parameter_count,
                         sizeof(struct d12_umd_serialized_root_parameter)) ||
       !d12_umd_size_add(&size, header->root_range_count,
                         sizeof(struct d12_frontend_descriptor_range)) ||
       !d12_umd_size_add(&size, header->root_static_sampler_count,
                         sizeof(struct d12_frontend_static_sampler)) ||
       !d12_umd_size_add(&size, header->element_count,
                         sizeof(struct d12_frontend_input_element)) ||
       !d12_umd_size_add(
          &size, header->so_declaration_count,
          sizeof(struct d12_frontend_stream_output_declaration)) ||
       !d12_umd_size_add(&size, header->so_stride_count, sizeof(uint32_t)))
      return false;
   for (uint32_t index = 0; index < 5; ++index) {
      if (header->shader_sizes[index] > SIZE_MAX ||
          !d12_umd_size_add(&size, (size_t)header->shader_sizes[index], 1))
         return false;
   }
   *out_size = size;
   return size <= D12_UMD_SERIALIZED_MAX_TOTAL_BYTES;
}

static void
d12_umd_pack_root_parameters(
   uint8_t *payload, const struct d12_frontend_root_signature_desc *root)
{
   struct d12_umd_serialized_root_parameter *records = (void *)payload;
   struct d12_frontend_descriptor_range *ranges =
      (void *)(payload + (size_t)root->num_parameters * sizeof(*records));
   uint32_t next_range = 0;

   for (uint32_t index = 0; index < root->num_parameters; ++index) {
      const struct d12_frontend_root_parameter *source =
         &root->parameters[index];
      struct d12_umd_serialized_root_parameter *destination =
         &records[index];
      destination->parameter_type = source->parameter_type;
      destination->shader_visibility = source->shader_visibility;
      switch (source->parameter_type) {
      case 0:
         destination->range_start = next_range;
         destination->range_count = source->table.num_ranges;
         if (source->table.num_ranges) {
            memcpy(&ranges[next_range], source->table.ranges,
                   (size_t)source->table.num_ranges * sizeof(*ranges));
         }
         next_range += source->table.num_ranges;
         break;
      case 1:
         destination->data0 = source->constants.shader_register;
         destination->data1 = source->constants.register_space;
         destination->data2 = source->constants.num_32bit_values;
         break;
      case 2:
      case 3:
      case 4:
         destination->data0 = source->descriptor.shader_register;
         destination->data1 = source->descriptor.register_space;
         break;
      default:
         break;
      }
   }
}

static bool
d12_umd_pipeline_serialize(
   enum d12_umd_serialized_pipeline_kind kind,
   const struct d12_frontend_compute_pipeline_desc *compute,
   const struct d12_frontend_graphics_pipeline_desc *graphics,
   const struct d12_frontend_root_signature_desc *root, void **out_blob,
   size_t *out_size)
{
   struct d12_umd_pipeline_blob_header header;
   uint32_t range_count;
   size_t total_size;
   uint8_t *blob;
   uint8_t *cursor;

   if (!out_blob || !out_size)
      return false;
   *out_blob = NULL;
   *out_size = 0;
   if (!d12_umd_root_counts(root, &range_count))
      return false;

   memset(&header, 0, sizeof(header));
   memcpy(header.magic, d12_umd_pipeline_magic, sizeof(header.magic));
   header.version = D12_UMD_PIPELINE_SERIALIZATION_VERSION;
   header.header_size = sizeof(header);
   header.kind = kind;
   header.has_root_signature = root != NULL;
   if (root) {
      header.root_parameter_count = root->num_parameters;
      header.root_range_count = range_count;
      header.root_static_sampler_count = root->num_static_samplers;
      header.root_flags = root->flags;
   }

   if (kind == D12_UMD_SERIALIZED_PIPELINE_COMPUTE) {
      if (!compute || !compute->shader.data || !compute->shader.size ||
          (compute->root_signature != NULL) != (root != NULL))
         return false;
      header.shader_sizes[0] = compute->shader.size;
   } else if (kind == D12_UMD_SERIALIZED_PIPELINE_GRAPHICS) {
      const struct d12_frontend_shader_bytecode *shaders[5];
      if (!graphics || !graphics->vertex_shader.data ||
          !graphics->vertex_shader.size ||
          (graphics->root_signature != NULL) != (root != NULL) ||
          graphics->element_layout.count > D12_FRONTEND_MAX_INPUT_ELEMENTS ||
          (graphics->element_layout.count &&
           !graphics->element_layout.elements) ||
          graphics->stream_output.declaration_count >
             D12_FRONTEND_MAX_STREAM_OUTPUT_DECLARATIONS ||
          graphics->stream_output.stride_count >
             D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS ||
          (graphics->stream_output.declaration_count &&
           !graphics->stream_output.declarations) ||
          (graphics->stream_output.stride_count &&
           !graphics->stream_output.strides))
         return false;
      shaders[0] = &graphics->vertex_shader;
      shaders[1] = &graphics->pixel_shader;
      shaders[2] = &graphics->geometry_shader;
      shaders[3] = &graphics->hull_shader;
      shaders[4] = &graphics->domain_shader;
      for (uint32_t index = 0; index < 5; ++index) {
         if ((shaders[index]->size && !shaders[index]->data) ||
             shaders[index]->size > D12_UMD_SERIALIZED_MAX_SHADER_BYTES)
            return false;
         header.shader_sizes[index] = shaders[index]->size;
      }
      header.element_count = graphics->element_layout.count;
      header.so_declaration_count =
         graphics->stream_output.declaration_count;
      header.so_stride_count = graphics->stream_output.stride_count;
      header.so_rasterized_stream =
         graphics->stream_output.rasterized_stream;
      header.index_buffer_strip_cut_value =
         graphics->index_buffer_strip_cut_value;
      header.primitive_topology_type = graphics->primitive_topology_type;
      header.num_render_targets = graphics->num_render_targets;
      memcpy(header.rtv_formats, graphics->rtv_formats,
             sizeof(header.rtv_formats));
      header.dsv_format = graphics->dsv_format;
      header.sample_count = graphics->sample_count;
      header.sample_quality = graphics->sample_quality;
      header.sample_mask = graphics->sample_mask;
      d12_umd_pack_blend(header.blend, &graphics->blend);
      d12_umd_pack_rasterizer(header.rasterizer, &graphics->rasterizer);
      d12_umd_pack_depth_stencil(header.depth_stencil,
                                 &graphics->depth_stencil);
   } else {
      return false;
   }

   for (uint32_t index = 0; index < 5; ++index) {
      if (header.shader_sizes[index] > D12_UMD_SERIALIZED_MAX_SHADER_BYTES)
         return false;
   }
   if (!d12_umd_pipeline_blob_size(&header, &total_size))
      return false;
   header.total_size = total_size;
   blob = calloc(1, total_size);
   if (!blob)
      return false;
   memcpy(blob, &header, sizeof(header));
   cursor = blob + sizeof(header);

   if (root) {
      d12_umd_pack_root_parameters(cursor, root);
      cursor += (size_t)header.root_parameter_count *
                sizeof(struct d12_umd_serialized_root_parameter);
      cursor += (size_t)header.root_range_count *
                sizeof(struct d12_frontend_descriptor_range);
      if (header.root_static_sampler_count) {
         memcpy(cursor, root->static_samplers,
                (size_t)header.root_static_sampler_count *
                   sizeof(struct d12_frontend_static_sampler));
         cursor += (size_t)header.root_static_sampler_count *
                   sizeof(struct d12_frontend_static_sampler);
      }
   }
   if (graphics) {
      const struct d12_frontend_shader_bytecode *shaders[5] = {
         &graphics->vertex_shader, &graphics->pixel_shader,
         &graphics->geometry_shader, &graphics->hull_shader,
         &graphics->domain_shader,
      };
      if (header.element_count) {
         memcpy(cursor, graphics->element_layout.elements,
                (size_t)header.element_count *
                   sizeof(struct d12_frontend_input_element));
         cursor += (size_t)header.element_count *
                   sizeof(struct d12_frontend_input_element);
      }
      if (header.so_declaration_count) {
         memcpy(cursor, graphics->stream_output.declarations,
                (size_t)header.so_declaration_count *
                   sizeof(struct d12_frontend_stream_output_declaration));
         cursor += (size_t)header.so_declaration_count *
                   sizeof(struct d12_frontend_stream_output_declaration);
      }
      if (header.so_stride_count) {
         memcpy(cursor, graphics->stream_output.strides,
                (size_t)header.so_stride_count * sizeof(uint32_t));
         cursor += (size_t)header.so_stride_count * sizeof(uint32_t);
      }
      for (uint32_t index = 0; index < 5; ++index) {
         if (header.shader_sizes[index]) {
            memcpy(cursor, shaders[index]->data,
                   (size_t)header.shader_sizes[index]);
            cursor += (size_t)header.shader_sizes[index];
         }
      }
   } else {
      memcpy(cursor, compute->shader.data, compute->shader.size);
      cursor += compute->shader.size;
   }

   if ((size_t)(cursor - blob) != total_size) {
      free(blob);
      return false;
   }
   ((struct d12_umd_pipeline_blob_header *)blob)->blob_hash =
      d12_umd_pipeline_blob_hash(blob, total_size);
   if (!d12_umd_pipeline_serialization_valid(blob, total_size)) {
      free(blob);
      return false;
   }
   *out_blob = blob;
   *out_size = total_size;
   return true;
}

bool
d12_umd_pipeline_serialize_compute(
   const struct d12_frontend_compute_pipeline_desc *pipeline,
   const struct d12_frontend_root_signature_desc *root_signature,
   void **out_blob, size_t *out_size)
{
   return d12_umd_pipeline_serialize(D12_UMD_SERIALIZED_PIPELINE_COMPUTE,
                                     pipeline, NULL, root_signature,
                                     out_blob, out_size);
}

bool
d12_umd_pipeline_serialize_graphics(
   const struct d12_frontend_graphics_pipeline_desc *pipeline,
   const struct d12_frontend_root_signature_desc *root_signature,
   void **out_blob, size_t *out_size)
{
   return d12_umd_pipeline_serialize(D12_UMD_SERIALIZED_PIPELINE_GRAPHICS,
                                     NULL, pipeline, root_signature,
                                     out_blob, out_size);
}

static bool
d12_umd_serialized_root_valid(
   const struct d12_umd_pipeline_blob_header *header,
   const uint8_t *payload)
{
   const struct d12_umd_serialized_root_parameter *parameters =
      (const void *)payload;
   const struct d12_frontend_descriptor_range *ranges =
      (const void *)(payload + (size_t)header->root_parameter_count *
                                  sizeof(*parameters));
   const struct d12_frontend_static_sampler *samplers =
      (const void *)((const uint8_t *)ranges +
                     (size_t)header->root_range_count * sizeof(*ranges));
   uint32_t next_range = 0;
   uint32_t dword_count = 0;

   if (header->root_flags & ~D12_UMD_SERIALIZED_ROOT_FLAGS_MASK)
      return false;

   for (uint32_t index = 0; index < header->root_parameter_count; ++index) {
      const struct d12_umd_serialized_root_parameter *parameter =
         &parameters[index];
      if (parameter->parameter_type > 4u ||
          parameter->shader_visibility > 5u)
         return false;
      if (parameter->parameter_type == 0u) {
         if (parameter->range_start != next_range ||
             parameter->range_count >
                header->root_range_count - next_range ||
             dword_count == D12_UMD_SERIALIZED_MAX_ROOT_DWORDS)
            return false;
         next_range += parameter->range_count;
         ++dword_count;
      } else if (parameter->range_start || parameter->range_count) {
         return false;
      } else if (parameter->parameter_type == 1u) {
         if (!parameter->data2 ||
             parameter->data2 >
                D12_UMD_SERIALIZED_MAX_ROOT_DWORDS - dword_count)
            return false;
         dword_count += parameter->data2;
      } else {
         if (parameter->data2 ||
             dword_count > D12_UMD_SERIALIZED_MAX_ROOT_DWORDS - 2u)
            return false;
         dword_count += 2u;
      }
   }
   if (next_range != header->root_range_count)
      return false;
   for (uint32_t index = 0; index < header->root_range_count; ++index) {
      if (ranges[index].range_type > 3u || !ranges[index].num_descriptors)
         return false;
   }
   for (uint32_t index = 0; index < header->root_static_sampler_count;
        ++index) {
      if (samplers[index].shader_visibility > 5u ||
          samplers[index].border_color > 2u)
         return false;
   }
   return true;
}

static bool
d12_umd_serialized_graphics_valid(
   const struct d12_umd_pipeline_blob_header *header,
   const uint8_t *payload)
{
   const uint8_t *cursor = payload;
   const struct d12_frontend_input_element *elements;
   const struct d12_frontend_stream_output_declaration *declarations;
   const uint32_t *strides;

   cursor += (size_t)header->root_parameter_count *
             sizeof(struct d12_umd_serialized_root_parameter);
   cursor += (size_t)header->root_range_count *
             sizeof(struct d12_frontend_descriptor_range);
   cursor += (size_t)header->root_static_sampler_count *
             sizeof(struct d12_frontend_static_sampler);
   elements = (const void *)cursor;
   cursor += (size_t)header->element_count * sizeof(*elements);
   declarations = (const void *)cursor;
   cursor += (size_t)header->so_declaration_count * sizeof(*declarations);
   strides = (const void *)cursor;

   if ((header->sample_count != 1u && header->sample_count != 2u &&
        header->sample_count != 4u) ||
       header->sample_quality != 0u ||
       header->index_buffer_strip_cut_value > 2u ||
       header->primitive_topology_type < 1u ||
       header->primitive_topology_type > 5u ||
       (header->shader_sizes[3] == 0) != (header->shader_sizes[4] == 0) ||
       (header->so_declaration_count == 0) !=
          (header->so_stride_count == 0) ||
       (header->so_rasterized_stream != UINT32_MAX &&
        header->so_rasterized_stream >=
           D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS))
      return false;

   for (uint32_t index = header->num_render_targets;
        index < D12_FRONTEND_MAX_RENDER_TARGETS; ++index) {
      if (header->rtv_formats[index] != 0u)
         return false;
   }
   for (uint32_t index = 0; index < header->element_count; ++index) {
      if (elements[index].input_slot >= 32u ||
          elements[index].input_register >= 32u ||
          elements[index].input_slot_class > 1u ||
          (elements[index].input_slot_class == 0u &&
           elements[index].instance_data_step_rate != 0u))
         return false;
   }
   for (uint32_t index = 0; index < header->so_stride_count; ++index) {
      if ((strides[index] & 3u) || strides[index] > UINT16_MAX)
         return false;
   }
   for (uint32_t index = 0; index < header->so_declaration_count; ++index) {
      const struct d12_frontend_stream_output_declaration *declaration =
         &declarations[index];
      if (declaration->stream >= D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS ||
          declaration->output_slot >= header->so_stride_count ||
          !declaration->component_count ||
          declaration->start_component >= 4u ||
          declaration->component_count > 4u - declaration->start_component ||
          (declaration->register_index != UINT32_MAX &&
           (declaration->register_index >= 32u ||
            declaration->system_value > 22u)))
         return false;
   }
   return true;
}

bool
d12_umd_pipeline_serialization_valid(const void *blob, size_t size)
{
   const struct d12_umd_pipeline_blob_header *header = blob;
   const uint8_t *payload;
   size_t expected_size;

   if (!blob || size < sizeof(*header) ||
       memcmp(header->magic, d12_umd_pipeline_magic,
              sizeof(header->magic)) ||
       header->version != D12_UMD_PIPELINE_SERIALIZATION_VERSION ||
       header->header_size != sizeof(*header) || header->total_size != size ||
       header->root_parameter_count >
          D12_UMD_SERIALIZED_MAX_ROOT_PARAMETERS ||
       header->root_range_count > D12_UMD_SERIALIZED_MAX_ROOT_RANGES ||
       header->root_static_sampler_count >
          D12_UMD_SERIALIZED_MAX_STATIC_SAMPLERS ||
       header->element_count > D12_FRONTEND_MAX_INPUT_ELEMENTS ||
       header->so_declaration_count >
          D12_FRONTEND_MAX_STREAM_OUTPUT_DECLARATIONS ||
       header->so_stride_count > D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS ||
       header->num_render_targets > D12_FRONTEND_MAX_RENDER_TARGETS ||
       header->reserved[0] != 0 ||
       !d12_umd_pipeline_blob_size(header, &expected_size) ||
       expected_size != size)
      return false;
   if ((!header->has_root_signature &&
        (header->root_parameter_count || header->root_range_count ||
         header->root_static_sampler_count || header->root_flags)) ||
       header->has_root_signature > 1u)
      return false;
   for (uint32_t index = 0; index < 5; ++index) {
      if (header->shader_sizes[index] > D12_UMD_SERIALIZED_MAX_SHADER_BYTES)
         return false;
   }
   if (header->kind == D12_UMD_SERIALIZED_PIPELINE_COMPUTE) {
      if (!header->shader_sizes[0] || header->shader_sizes[1] ||
          header->shader_sizes[2] || header->shader_sizes[3] ||
          header->shader_sizes[4] || header->element_count ||
          header->so_declaration_count || header->so_stride_count)
         return false;
   } else if (header->kind == D12_UMD_SERIALIZED_PIPELINE_GRAPHICS) {
      if (!header->shader_sizes[0] ||
          (header->so_declaration_count && !header->so_stride_count))
         return false;
   } else {
      return false;
   }

   payload = (const uint8_t *)blob + sizeof(*header);
   if (header->blob_hash != d12_umd_pipeline_blob_hash(blob, size) ||
       !d12_umd_serialized_root_valid(header, payload))
      return false;
   return header->kind != D12_UMD_SERIALIZED_PIPELINE_GRAPHICS ||
          d12_umd_serialized_graphics_valid(header, payload);
}

static const uint8_t *
d12_umd_deserialize_root(
   const struct d12_umd_pipeline_blob_header *header, const uint8_t *cursor,
   struct d12_umd_serialized_pipeline *pipeline)
{
   const struct d12_umd_serialized_root_parameter *records =
      (const void *)cursor;
   struct d12_frontend_root_parameter *parameters = NULL;
   struct d12_frontend_descriptor_range *ranges = NULL;
   struct d12_frontend_static_sampler *samplers = NULL;

   cursor += (size_t)header->root_parameter_count * sizeof(*records);
   if (header->root_parameter_count)
      parameters = calloc(header->root_parameter_count, sizeof(*parameters));
   if (header->root_range_count)
      ranges = calloc(header->root_range_count, sizeof(*ranges));
   if (header->root_static_sampler_count) {
      samplers = calloc(header->root_static_sampler_count, sizeof(*samplers));
   }
   if ((header->root_parameter_count && !parameters) ||
       (header->root_range_count && !ranges) ||
       (header->root_static_sampler_count && !samplers)) {
      free(parameters);
      free(ranges);
      free(samplers);
      return NULL;
   }
   if (header->root_range_count) {
      memcpy(ranges, cursor,
             (size_t)header->root_range_count * sizeof(*ranges));
      cursor += (size_t)header->root_range_count * sizeof(*ranges);
   }
   if (header->root_static_sampler_count) {
      memcpy(samplers, cursor,
             (size_t)header->root_static_sampler_count * sizeof(*samplers));
      cursor += (size_t)header->root_static_sampler_count * sizeof(*samplers);
   }
   for (uint32_t index = 0; index < header->root_parameter_count; ++index) {
      const struct d12_umd_serialized_root_parameter *source = &records[index];
      struct d12_frontend_root_parameter *destination = &parameters[index];
      destination->parameter_type = source->parameter_type;
      destination->shader_visibility = source->shader_visibility;
      switch (source->parameter_type) {
      case 0:
         destination->table.num_ranges = source->range_count;
         destination->table.ranges = source->range_count
                                        ? &ranges[source->range_start]
                                        : NULL;
         break;
      case 1:
         destination->constants.shader_register = source->data0;
         destination->constants.register_space = source->data1;
         destination->constants.num_32bit_values = source->data2;
         break;
      case 2:
      case 3:
      case 4:
         destination->descriptor.shader_register = source->data0;
         destination->descriptor.register_space = source->data1;
         break;
      default:
         break;
      }
   }
   pipeline->root_signature = (struct d12_frontend_root_signature_desc){
      .num_parameters = header->root_parameter_count,
      .parameters = parameters,
      .num_static_samplers = header->root_static_sampler_count,
      .static_samplers = samplers,
      .flags = header->root_flags,
   };
   pipeline->owned_root_ranges = ranges;
   return cursor;
}

bool
d12_umd_pipeline_deserialize(
   const void *blob, size_t size,
   struct d12_umd_serialized_pipeline *out_pipeline)
{
   const struct d12_umd_pipeline_blob_header *header = blob;
   const uint8_t *cursor;

   if (!out_pipeline)
      return false;
   memset(out_pipeline, 0, sizeof(*out_pipeline));
   if (!d12_umd_pipeline_serialization_valid(blob, size))
      return false;
   out_pipeline->kind =
      (enum d12_umd_serialized_pipeline_kind)header->kind;
   out_pipeline->has_root_signature = header->has_root_signature != 0;
   cursor = (const uint8_t *)blob + sizeof(*header);
   if (out_pipeline->has_root_signature) {
      cursor = d12_umd_deserialize_root(header, cursor, out_pipeline);
      if (!cursor)
         goto fail;
   }

   if (header->kind == D12_UMD_SERIALIZED_PIPELINE_GRAPHICS) {
      struct d12_frontend_input_element *elements = NULL;
      struct d12_frontend_stream_output_declaration *declarations = NULL;
      uint32_t *strides = NULL;
      struct d12_frontend_shader_bytecode *shaders[5] = {
         &out_pipeline->graphics.vertex_shader,
         &out_pipeline->graphics.pixel_shader,
         &out_pipeline->graphics.geometry_shader,
         &out_pipeline->graphics.hull_shader,
         &out_pipeline->graphics.domain_shader,
      };
      if (header->element_count)
         elements = calloc(header->element_count, sizeof(*elements));
      if (header->so_declaration_count) {
         declarations = calloc(header->so_declaration_count,
                               sizeof(*declarations));
      }
      if (header->so_stride_count)
         strides = calloc(header->so_stride_count, sizeof(*strides));
      if ((header->element_count && !elements) ||
          (header->so_declaration_count && !declarations) ||
          (header->so_stride_count && !strides)) {
         free(elements);
         free(declarations);
         free(strides);
         goto fail;
      }
      if (header->element_count) {
         memcpy(elements, cursor,
                (size_t)header->element_count * sizeof(*elements));
         cursor += (size_t)header->element_count * sizeof(*elements);
      }
      if (header->so_declaration_count) {
         memcpy(declarations, cursor,
                (size_t)header->so_declaration_count *
                   sizeof(*declarations));
         cursor += (size_t)header->so_declaration_count *
                   sizeof(*declarations);
      }
      if (header->so_stride_count) {
         memcpy(strides, cursor,
                (size_t)header->so_stride_count * sizeof(*strides));
         cursor += (size_t)header->so_stride_count * sizeof(*strides);
      }
      out_pipeline->graphics.element_layout.count = header->element_count;
      out_pipeline->graphics.element_layout.elements = elements;
      out_pipeline->graphics.stream_output.declaration_count =
         header->so_declaration_count;
      out_pipeline->graphics.stream_output.declarations = declarations;
      out_pipeline->graphics.stream_output.stride_count =
         header->so_stride_count;
      out_pipeline->graphics.stream_output.strides = strides;
      out_pipeline->graphics.stream_output.rasterized_stream =
         header->so_rasterized_stream;
      out_pipeline->graphics.index_buffer_strip_cut_value =
         header->index_buffer_strip_cut_value;
      out_pipeline->graphics.primitive_topology_type =
         header->primitive_topology_type;
      out_pipeline->graphics.num_render_targets = header->num_render_targets;
      memcpy(out_pipeline->graphics.rtv_formats, header->rtv_formats,
             sizeof(header->rtv_formats));
      out_pipeline->graphics.dsv_format = header->dsv_format;
      out_pipeline->graphics.sample_count = header->sample_count;
      out_pipeline->graphics.sample_quality = header->sample_quality;
      out_pipeline->graphics.sample_mask = header->sample_mask;
      d12_umd_unpack_blend(&out_pipeline->graphics.blend, header->blend);
      d12_umd_unpack_rasterizer(&out_pipeline->graphics.rasterizer,
                                header->rasterizer);
      d12_umd_unpack_depth_stencil(&out_pipeline->graphics.depth_stencil,
                                   header);
      for (uint32_t index = 0; index < 5; ++index) {
         if (header->shader_sizes[index]) {
            void *shader = malloc((size_t)header->shader_sizes[index]);
            if (!shader)
               goto fail;
            memcpy(shader, cursor, (size_t)header->shader_sizes[index]);
            shaders[index]->data = shader;
            shaders[index]->size = (size_t)header->shader_sizes[index];
            cursor += (size_t)header->shader_sizes[index];
         }
      }
   } else {
      void *shader = malloc((size_t)header->shader_sizes[0]);
      if (!shader)
         goto fail;
      memcpy(shader, cursor, (size_t)header->shader_sizes[0]);
      out_pipeline->compute.shader.data = shader;
      out_pipeline->compute.shader.size = (size_t)header->shader_sizes[0];
      cursor += (size_t)header->shader_sizes[0];
   }
   if ((size_t)(cursor - (const uint8_t *)blob) != size)
      goto fail;
   return true;

fail:
   d12_umd_serialized_pipeline_cleanup(out_pipeline);
   return false;
}

void
d12_umd_serialized_pipeline_cleanup(
   struct d12_umd_serialized_pipeline *pipeline)
{
   if (!pipeline)
      return;
   free((void *)pipeline->root_signature.parameters);
   free(pipeline->owned_root_ranges);
   free((void *)pipeline->root_signature.static_samplers);
   free((void *)pipeline->graphics.element_layout.elements);
   free((void *)pipeline->graphics.stream_output.declarations);
   free((void *)pipeline->graphics.stream_output.strides);
   free((void *)pipeline->compute.shader.data);
   free((void *)pipeline->graphics.vertex_shader.data);
   free((void *)pipeline->graphics.pixel_shader.data);
   free((void *)pipeline->graphics.geometry_shader.data);
   free((void *)pipeline->graphics.hull_shader.data);
   free((void *)pipeline->graphics.domain_shader.data);
   memset(pipeline, 0, sizeof(*pipeline));
}
