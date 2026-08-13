/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_umd_pipeline_serialization.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct d12_frontend_descriptor_range test_ranges[] = {
   {
      .range_type = 0,
      .num_descriptors = 8,
      .base_shader_register = 3,
      .register_space = 2,
      .offset_in_descriptors = 0,
   },
   {
      .range_type = 3,
      .num_descriptors = 4,
      .base_shader_register = 1,
      .register_space = 0,
      .offset_in_descriptors = 8,
   },
};

static const struct d12_frontend_root_parameter test_parameters[] = {
   {
      .parameter_type = 0,
      .shader_visibility = 0,
      .table = {
         .num_ranges = 2,
         .ranges = test_ranges,
      },
   },
   {
      .parameter_type = 1,
      .shader_visibility = 5,
      .constants = {
         .shader_register = 7,
         .register_space = 4,
         .num_32bit_values = 12,
      },
   },
   {
      .parameter_type = 2,
      .shader_visibility = 1,
      .descriptor = {
         .shader_register = 9,
         .register_space = 6,
      },
   },
};

static const struct d12_frontend_static_sampler test_samplers[] = {
   {
      .filter = 0x15,
      .address_u = 1,
      .address_v = 2,
      .address_w = 3,
      .mip_lod_bias = -1.25f,
      .max_anisotropy = 8,
      .comparison_func = 4,
      .border_color = 2,
      .min_lod = 0.5f,
      .max_lod = 13.0f,
      .shader_register = 5,
      .register_space = 3,
      .shader_visibility = 4,
   },
};

static const struct d12_frontend_root_signature_desc test_root = {
   .num_parameters = 3,
   .parameters = test_parameters,
   .num_static_samplers = 1,
   .static_samplers = test_samplers,
   .flags = 0x41,
};

static void
test_compute_round_trip(void)
{
   static const uint8_t shader[] = {0x44, 0x58, 0x42, 0x43, 1, 2, 3, 4, 5};
   const struct d12_frontend_compute_pipeline_desc source = {
      .shader = {.data = shader, .size = sizeof(shader)},
      .root_signature = (void *)(uintptr_t)1,
   };
   struct d12_umd_serialized_pipeline decoded;
   void *first = NULL;
   void *second = NULL;
   size_t first_size = 0;
   size_t second_size = 0;

   assert(d12_umd_pipeline_serialize_compute(&source, &test_root, &first,
                                             &first_size));
   assert(d12_umd_pipeline_serialize_compute(&source, &test_root, &second,
                                             &second_size));
   assert(first && second && first_size == second_size && first_size > 616);
   assert(!memcmp(first, second, first_size));
   assert(d12_umd_pipeline_serialization_valid(first, first_size));
   assert(d12_umd_pipeline_deserialize(first, first_size, &decoded));
   assert(decoded.kind == D12_UMD_SERIALIZED_PIPELINE_COMPUTE);
   assert(decoded.has_root_signature);
   assert(decoded.root_signature.num_parameters == 3);
   assert(decoded.root_signature.num_static_samplers == 1);
   assert(decoded.root_signature.flags == test_root.flags);
   assert(decoded.root_signature.parameters[0].table.num_ranges == 2);
   assert(decoded.root_signature.parameters[0].table.ranges[1].range_type ==
          test_ranges[1].range_type);
   assert(decoded.root_signature.parameters[1].constants.num_32bit_values ==
          12);
   assert(decoded.root_signature.parameters[2].descriptor.register_space ==
          6);
   assert(decoded.root_signature.static_samplers[0].mip_lod_bias == -1.25f);
   assert(decoded.compute.shader.size == sizeof(shader));
   assert(!memcmp(decoded.compute.shader.data, shader, sizeof(shader)));
   assert(decoded.compute.root_signature == NULL);
   d12_umd_serialized_pipeline_cleanup(&decoded);

   ((uint8_t *)first)[first_size - 1] ^= 0x80;
   assert(!d12_umd_pipeline_serialization_valid(first, first_size));
   ((uint8_t *)first)[first_size - 1] ^= 0x80;
   /* sample-mask lives in the fixed header; it must be checksummed too. */
   ((uint8_t *)first)[168] ^= 0x80;
   assert(!d12_umd_pipeline_serialization_valid(first, first_size));
   ((uint8_t *)first)[168] ^= 0x80;
   assert(!d12_umd_pipeline_serialization_valid(first, first_size - 1));
   ((uint8_t *)first)[0] = 0;
   assert(!d12_umd_pipeline_serialization_valid(first, first_size));

   free(first);
   free(second);
}

static void
test_graphics_round_trip(void)
{
   static const uint8_t vertex[] = {1, 3, 5, 7, 9};
   static const uint8_t pixel[] = {2, 4, 6};
   static const uint8_t geometry[] = {8, 10};
   static const struct d12_frontend_input_element elements[] = {
      {
         .input_slot = 0,
         .aligned_byte_offset = 16,
         .format = 28,
         .input_slot_class = 0,
         .instance_data_step_rate = 0,
         .input_register = 2,
      },
      {
         .input_slot = 1,
         .aligned_byte_offset = 4,
         .format = 41,
         .input_slot_class = 1,
         .instance_data_step_rate = 3,
         .input_register = 5,
      },
   };
   static const struct d12_frontend_stream_output_declaration declarations[] = {
      {
         .stream = 0,
         .output_slot = 0,
         .register_index = UINT32_MAX,
         .start_component = 0,
         .component_count = 4,
      },
      {
         .stream = 0,
         .output_slot = 0,
         .register_index = 2,
         .system_value = 1,
         .start_component = 1,
         .component_count = 2,
      },
   };
   static const uint32_t strides[] = {24};
   struct d12_frontend_graphics_pipeline_desc source;
   struct d12_umd_serialized_pipeline decoded;
   void *blob = NULL;
   size_t blob_size = 0;

   memset(&source, 0, sizeof(source));
   source.vertex_shader =
      (struct d12_frontend_shader_bytecode){vertex, sizeof(vertex)};
   source.pixel_shader =
      (struct d12_frontend_shader_bytecode){pixel, sizeof(pixel)};
   source.geometry_shader =
      (struct d12_frontend_shader_bytecode){geometry, sizeof(geometry)};
   source.root_signature = (void *)(uintptr_t)1;
   source.blend.alpha_to_coverage_enable = 1;
   source.blend.independent_blend_enable = 1;
   source.blend.render_targets[0].blend_enable = 1;
   source.blend.render_targets[0].src_blend = 5;
   source.blend.render_targets[0].dest_blend = 6;
   source.blend.render_targets[0].write_mask = 0xb;
   source.sample_mask = UINT32_C(0xa5a55a5a);
   source.rasterizer.fill_mode = 3;
   source.rasterizer.cull_mode = 2;
   source.rasterizer.front_counter_clockwise = 1;
   source.rasterizer.depth_bias = -7;
   source.rasterizer.depth_bias_clamp = 1.5f;
   source.rasterizer.slope_scaled_depth_bias = -0.25f;
   source.rasterizer.depth_clip_enable = 1;
   source.depth_stencil.depth_enable = 1;
   source.depth_stencil.depth_write_mask = 1;
   source.depth_stencil.depth_func = 4;
   source.depth_stencil.stencil_enable = 1;
   source.depth_stencil.stencil_read_mask = 0x7f;
   source.depth_stencil.stencil_write_mask = 0x3f;
   source.depth_stencil.front_face.stencil_pass_op = 6;
   source.depth_stencil.back_face.stencil_fail_op = 2;
   source.depth_stencil.back_face.stencil_depth_fail_op = 3;
   source.depth_stencil.back_face.stencil_pass_op = 4;
   source.depth_stencil.back_face.stencil_func = 5;
   source.element_layout = (struct d12_frontend_element_layout){
      .count = 2,
      .elements = elements,
   };
   source.stream_output = (struct d12_frontend_stream_output_desc){
      .declaration_count = 2,
      .declarations = declarations,
      .stride_count = 1,
      .strides = strides,
      .rasterized_stream = 0,
   };
   source.index_buffer_strip_cut_value = 2;
   source.primitive_topology_type = 3;
   source.num_render_targets = 2;
   source.rtv_formats[0] = 28;
   source.rtv_formats[1] = 87;
   source.dsv_format = 45;
   source.sample_count = 4;
   source.sample_quality = 0;

   assert(d12_umd_pipeline_serialize_graphics(&source, &test_root, &blob,
                                              &blob_size));
   assert(d12_umd_pipeline_deserialize(blob, blob_size, &decoded));
   assert(decoded.kind == D12_UMD_SERIALIZED_PIPELINE_GRAPHICS);
   assert(decoded.graphics.element_layout.count == 2);
   assert(decoded.graphics.element_layout.elements[1].input_register == 5);
   assert(decoded.graphics.stream_output.declaration_count == 2);
   assert(decoded.graphics.stream_output.declarations[0].register_index ==
          UINT32_MAX);
   assert(decoded.graphics.stream_output.strides[0] == 24);
   assert(decoded.graphics.blend.render_targets[0].write_mask == 0xb);
   assert(decoded.graphics.rasterizer.depth_bias == -7);
   assert(decoded.graphics.rasterizer.depth_bias_clamp == 1.5f);
   assert(decoded.graphics.depth_stencil.front_face.stencil_pass_op == 6);
   assert(decoded.graphics.depth_stencil.back_face.stencil_fail_op == 2);
   assert(decoded.graphics.depth_stencil.back_face.stencil_depth_fail_op == 3);
   assert(decoded.graphics.depth_stencil.back_face.stencil_pass_op == 4);
   assert(decoded.graphics.depth_stencil.back_face.stencil_func == 5);
   assert(decoded.graphics.sample_mask == UINT32_C(0xa5a55a5a));
   assert(decoded.graphics.rtv_formats[1] == 87);
   assert(decoded.graphics.vertex_shader.size == sizeof(vertex));
   assert(!memcmp(decoded.graphics.vertex_shader.data, vertex,
                  sizeof(vertex)));
   assert(decoded.graphics.pixel_shader.size == sizeof(pixel));
   assert(decoded.graphics.geometry_shader.size == sizeof(geometry));
   d12_umd_serialized_pipeline_cleanup(&decoded);
   free(blob);
}

static void
test_root_presence_validation(void)
{
   static const uint8_t shader[] = {1};
   struct d12_frontend_compute_pipeline_desc source = {
      .shader = {.data = shader, .size = sizeof(shader)},
      .root_signature = NULL,
   };
   struct d12_umd_serialized_pipeline decoded;
   void *blob = NULL;
   size_t size = 0;

   assert(!d12_umd_pipeline_serialize_compute(&source, &test_root, &blob,
                                              &size));
   assert(d12_umd_pipeline_serialize_compute(&source, NULL, &blob, &size));
   assert(d12_umd_pipeline_deserialize(blob, size, &decoded));
   assert(!decoded.has_root_signature);
   assert(decoded.root_signature.num_parameters == 0);
   d12_umd_serialized_pipeline_cleanup(&decoded);
   free(blob);
}

static void
test_wide_descriptor_table_round_trip(void)
{
   enum { range_count = 17 };
   static const uint8_t shader[] = {1, 2, 3, 4};
   struct d12_frontend_descriptor_range ranges[range_count];
   memset(ranges, 0, sizeof(ranges));
   for (uint32_t index = 0; index < range_count; ++index) {
      ranges[index] = (struct d12_frontend_descriptor_range){
         .range_type = 2,
         .num_descriptors = 1,
         .base_shader_register = index,
         .register_space = 3,
         .offset_in_descriptors = index,
      };
   }

   const struct d12_frontend_root_parameter parameter = {
      .parameter_type = 0,
      .shader_visibility = 0,
      .table = {
         .num_ranges = range_count,
         .ranges = ranges,
      },
   };
   const struct d12_frontend_root_signature_desc root = {
      .num_parameters = 1,
      .parameters = &parameter,
   };
   const struct d12_frontend_compute_pipeline_desc source = {
      .shader = {.data = shader, .size = sizeof(shader)},
      .root_signature = (void *)(uintptr_t)1,
   };
   struct d12_umd_serialized_pipeline decoded;
   void *blob = NULL;
   size_t size = 0;

   assert(d12_umd_pipeline_serialize_compute(&source, &root, &blob, &size));
   assert(d12_umd_pipeline_deserialize(blob, size, &decoded));
   assert(decoded.root_signature.parameters[0].table.num_ranges ==
          range_count);
   assert(decoded.root_signature.parameters[0].table.ranges[16]
             .base_shader_register == 16);
   d12_umd_serialized_pipeline_cleanup(&decoded);
   free(blob);
}

static void
test_stage_disjoint_static_samplers(void)
{
   enum { sampler_count = 65 };
   static const uint8_t shader[] = {5, 6, 7, 8};
   struct d12_frontend_static_sampler samplers[sampler_count];
   memset(samplers, 0, sizeof(samplers));
   for (uint32_t index = 0; index < sampler_count; ++index) {
      samplers[index] = (struct d12_frontend_static_sampler){
         .address_u = 1,
         .address_v = 1,
         .address_w = 1,
         .max_anisotropy = 1,
         .comparison_func = 8,
         .max_lod = 16.0f,
         .shader_register = index & 15u,
         .register_space = index >> 4,
         .shader_visibility = 1u + index % 5u,
      };
   }

   const struct d12_frontend_root_signature_desc root = {
      .num_static_samplers = sampler_count,
      .static_samplers = samplers,
   };
   const struct d12_frontend_compute_pipeline_desc source = {
      .shader = {.data = shader, .size = sizeof(shader)},
      .root_signature = (void *)(uintptr_t)1,
   };
   struct d12_umd_serialized_pipeline decoded;
   void *blob = NULL;
   size_t size = 0;

   assert(d12_umd_pipeline_serialize_compute(&source, &root, &blob, &size));
   assert(d12_umd_pipeline_deserialize(blob, size, &decoded));
   assert(decoded.root_signature.num_static_samplers == sampler_count);
   assert(decoded.root_signature.static_samplers[64].register_space == 4);
   d12_umd_serialized_pipeline_cleanup(&decoded);
   free(blob);
}

int
main(void)
{
   test_compute_round_trip();
   test_graphics_round_trip();
   test_root_presence_validation();
   test_wide_descriptor_table_round_trip();
   test_stage_disjoint_static_samplers();
   puts("d12_umd_pipeline_serialization_tests: PASS");
   return 0;
}
