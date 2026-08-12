/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral object descriptions shared by the public COM and WDDM DDI
 * entry layers. Values use the public/WDK numeric encodings, which the DDI
 * translation units pin with compile-time assertions.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct d12_frontend_descriptor_range {
   uint32_t range_type;
   uint32_t num_descriptors;
   uint32_t base_shader_register;
   uint32_t register_space;
   uint32_t offset_in_descriptors;
};

struct d12_frontend_root_parameter {
   uint32_t parameter_type;
   uint32_t shader_visibility;
   union {
      struct {
         uint32_t num_ranges;
         const struct d12_frontend_descriptor_range *ranges;
      } table;
      struct {
         uint32_t shader_register;
         uint32_t register_space;
         uint32_t num_32bit_values;
      } constants;
      struct {
         uint32_t shader_register;
         uint32_t register_space;
      } descriptor;
   };
};

struct d12_frontend_static_sampler {
   uint32_t filter;
   uint32_t address_u;
   uint32_t address_v;
   uint32_t address_w;
   float mip_lod_bias;
   uint32_t max_anisotropy;
   uint32_t comparison_func;
   uint32_t border_color;
   float min_lod;
   float max_lod;
   uint32_t shader_register;
   uint32_t register_space;
   uint32_t shader_visibility;
};

struct d12_frontend_root_signature_desc {
   uint32_t num_parameters;
   const struct d12_frontend_root_parameter *parameters;
   uint32_t num_static_samplers;
   const struct d12_frontend_static_sampler *static_samplers;
   uint32_t flags;
};

enum d12_frontend_root_pipeline {
   D12_FRONTEND_ROOT_GRAPHICS,
   D12_FRONTEND_ROOT_COMPUTE,
};

enum d12_frontend_root_buffer_type {
   D12_FRONTEND_ROOT_CBV,
   D12_FRONTEND_ROOT_SRV,
   D12_FRONTEND_ROOT_UAV,
};

enum d12_frontend_shader_stage {
   D12_FRONTEND_SHADER_PIXEL = 0,
   D12_FRONTEND_SHADER_VERTEX = 1,
   D12_FRONTEND_SHADER_GEOMETRY = 2,
   D12_FRONTEND_SHADER_HULL = 3,
   D12_FRONTEND_SHADER_DOMAIN = 4,
   D12_FRONTEND_SHADER_COMPUTE = 5,
};

#define D12_FRONTEND_MAX_INPUT_ELEMENTS 32u
#define D12_FRONTEND_MAX_RENDER_TARGETS 8u

struct d12_frontend_input_element {
   uint32_t input_slot;
   uint32_t aligned_byte_offset;
   uint32_t format;
   uint32_t input_slot_class;
   uint32_t instance_data_step_rate;
   uint32_t input_register;
};

struct d12_frontend_element_layout {
   uint32_t count;
   const struct d12_frontend_input_element *elements;
};

struct d12_frontend_render_target_blend {
   uint32_t blend_enable;
   uint32_t logic_op_enable;
   uint32_t src_blend;
   uint32_t dest_blend;
   uint32_t blend_op;
   uint32_t src_blend_alpha;
   uint32_t dest_blend_alpha;
   uint32_t blend_op_alpha;
   uint32_t logic_op;
   uint8_t write_mask;
};

struct d12_frontend_blend_desc {
   uint32_t alpha_to_coverage_enable;
   uint32_t independent_blend_enable;
   struct d12_frontend_render_target_blend
      render_targets[D12_FRONTEND_MAX_RENDER_TARGETS];
};

struct d12_frontend_depth_stencil_op {
   uint32_t stencil_fail_op;
   uint32_t stencil_depth_fail_op;
   uint32_t stencil_pass_op;
   uint32_t stencil_func;
};

struct d12_frontend_depth_stencil_desc {
   uint32_t depth_enable;
   uint32_t depth_write_mask;
   uint32_t depth_func;
   uint32_t stencil_enable;
   uint32_t front_enable;
   uint32_t back_enable;
   uint8_t stencil_read_mask;
   uint8_t stencil_write_mask;
   struct d12_frontend_depth_stencil_op front_face;
   struct d12_frontend_depth_stencil_op back_face;
};

struct d12_frontend_rasterizer_desc {
   uint32_t fill_mode;
   uint32_t cull_mode;
   uint32_t front_counter_clockwise;
   int32_t depth_bias;
   float depth_bias_clamp;
   float slope_scaled_depth_bias;
   uint32_t depth_clip_enable;
   uint32_t scissor_enable;
   uint32_t multisample_enable;
   uint32_t antialiased_line_enable;
   uint32_t forced_sample_count;
   uint32_t conservative_rasterization_mode;
};

struct d12_frontend_shader_bytecode {
   const void *data;
   size_t size;
};

struct d12_frontend_compute_pipeline_desc {
   struct d12_frontend_shader_bytecode shader;
   void *root_signature;
};

#define D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS 4u
#define D12_FRONTEND_MAX_STREAM_OUTPUT_DECLARATIONS 64u

struct d12_frontend_stream_output_declaration {
   uint32_t stream;
   uint32_t output_slot;
   uint32_t register_index;
   uint32_t system_value;
   uint32_t start_component;
   uint32_t component_count;
};

struct d12_frontend_stream_output_desc {
   uint32_t declaration_count;
   const struct d12_frontend_stream_output_declaration *declarations;
   uint32_t stride_count;
   const uint32_t *strides;
   uint32_t rasterized_stream;
};

struct d12_frontend_graphics_pipeline_desc {
   struct d12_frontend_shader_bytecode vertex_shader;
   struct d12_frontend_shader_bytecode pixel_shader;
   struct d12_frontend_shader_bytecode geometry_shader;
   struct d12_frontend_shader_bytecode hull_shader;
   struct d12_frontend_shader_bytecode domain_shader;
   void *root_signature;
   struct d12_frontend_blend_desc blend;
   uint32_t sample_mask;
   struct d12_frontend_rasterizer_desc rasterizer;
   struct d12_frontend_depth_stencil_desc depth_stencil;
   struct d12_frontend_element_layout element_layout;
   struct d12_frontend_stream_output_desc stream_output;
   uint32_t index_buffer_strip_cut_value;
   uint32_t primitive_topology_type;
   uint32_t num_render_targets;
   uint32_t rtv_formats[D12_FRONTEND_MAX_RENDER_TARGETS];
   uint32_t dsv_format;
   uint32_t sample_count;
   uint32_t sample_quality;
};

enum d12_frontend_cpu_page_property {
   D12_FRONTEND_CPU_PAGE_NOT_AVAILABLE,
   D12_FRONTEND_CPU_PAGE_WRITE_COMBINE,
   D12_FRONTEND_CPU_PAGE_WRITE_BACK,
};

enum d12_frontend_memory_pool {
   D12_FRONTEND_MEMORY_POOL_L0,
   D12_FRONTEND_MEMORY_POOL_L1,
};

enum d12_frontend_heap_flags {
   D12_FRONTEND_HEAP_ALLOW_NON_RT_DS_TEXTURES = 1u << 0,
   D12_FRONTEND_HEAP_ALLOW_BUFFERS = 1u << 1,
   D12_FRONTEND_HEAP_COHERENT_SYSTEMWIDE = 1u << 2,
   D12_FRONTEND_HEAP_PRIMARY = 1u << 3,
   D12_FRONTEND_HEAP_ALLOW_RT_DS_TEXTURES = 1u << 4,
};

struct d12_frontend_heap_desc {
   uint64_t size;
   uint64_t alignment;
   enum d12_frontend_memory_pool memory_pool;
   enum d12_frontend_cpu_page_property cpu_page_property;
   uint32_t flags;
   uint32_t creation_node_mask;
   uint32_t visible_node_mask;
};

enum d12_frontend_resource_flags {
   D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET = 1u << 0,
   D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL = 1u << 1,
   D12_FRONTEND_RESOURCE_ALLOW_CROSS_ADAPTER = 1u << 2,
   D12_FRONTEND_RESOURCE_ALLOW_SIMULTANEOUS_ACCESS = 1u << 3,
   D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE = 1u << 4,
   D12_FRONTEND_RESOURCE_ALLOW_UNORDERED_ACCESS = 1u << 5,
};

struct d12_frontend_resource_desc {
   uint32_t dimension;
   uint64_t alignment;
   uint64_t width;
   uint32_t height;
   uint16_t depth_or_array_size;
   uint16_t mip_levels;
   uint32_t format;
   uint32_t sample_count;
   uint32_t sample_quality;
   uint32_t layout;
   uint32_t flags;
   uint32_t initial_state;
};

struct d12_frontend_clear_value {
   uint32_t format;
   union {
      float color[4];
      struct {
         float depth;
         uint8_t stencil;
      } depth_stencil;
   };
};

struct d12_frontend_resource_allocation_info {
   uint64_t size;
   uint64_t alignment;
   uint32_t layout;
};

struct d12_frontend_subresource_info {
   uint64_t offset;
   uint64_t row_stride;
   uint64_t depth_stride;
   uint16_t row_byte_pre_swizzle_offset;
   uint16_t column_pre_swizzle_offset;
   uint16_t depth_pre_swizzle_offset;
};

/* Frontend-neutral format capabilities.  Public COM and WDDM DDI frontends
 * translate this shared hardware policy into their distinct bit fields. */
enum d12_frontend_format_support {
   D12_FRONTEND_FORMAT_BUFFER = UINT64_C(1) << 0,
   D12_FRONTEND_FORMAT_VERTEX_BUFFER = UINT64_C(1) << 1,
   D12_FRONTEND_FORMAT_INDEX_BUFFER = UINT64_C(1) << 2,
   D12_FRONTEND_FORMAT_TEXTURE_1D = UINT64_C(1) << 3,
   D12_FRONTEND_FORMAT_TEXTURE_2D = UINT64_C(1) << 4,
   D12_FRONTEND_FORMAT_TEXTURE_3D = UINT64_C(1) << 5,
   D12_FRONTEND_FORMAT_TEXTURE_CUBE = UINT64_C(1) << 6,
   D12_FRONTEND_FORMAT_SHADER_LOAD = UINT64_C(1) << 7,
   D12_FRONTEND_FORMAT_SHADER_SAMPLE = UINT64_C(1) << 8,
   D12_FRONTEND_FORMAT_SHADER_SAMPLE_COMPARISON = UINT64_C(1) << 9,
   D12_FRONTEND_FORMAT_MIP = UINT64_C(1) << 10,
   D12_FRONTEND_FORMAT_RENDER_TARGET = UINT64_C(1) << 11,
   D12_FRONTEND_FORMAT_BLENDABLE = UINT64_C(1) << 12,
   D12_FRONTEND_FORMAT_DEPTH_STENCIL = UINT64_C(1) << 13,
   D12_FRONTEND_FORMAT_MULTISAMPLE_RESOLVE = UINT64_C(1) << 14,
   D12_FRONTEND_FORMAT_MULTISAMPLE_RENDER_TARGET = UINT64_C(1) << 15,
   D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD = UINT64_C(1) << 16,
   D12_FRONTEND_FORMAT_DISPLAY = UINT64_C(1) << 17,
   D12_FRONTEND_FORMAT_CAST_WITHIN_BIT_LAYOUT = UINT64_C(1) << 18,
   D12_FRONTEND_FORMAT_TYPED_UAV = UINT64_C(1) << 19,
   D12_FRONTEND_FORMAT_SHADER_GATHER = UINT64_C(1) << 20,
   D12_FRONTEND_FORMAT_SHADER_GATHER_COMPARISON = UINT64_C(1) << 21,
   D12_FRONTEND_FORMAT_UAV_TYPED_LOAD = UINT64_C(1) << 22,
   D12_FRONTEND_FORMAT_UAV_TYPED_STORE = UINT64_C(1) << 23,
};

struct d12_frontend_heap_allocation {
   uint32_t allocation;
   uint32_t resource;
   uint64_t gpu_virtual_address;
   uint64_t size;
};

#define D12_FRONTEND_DESCRIPTOR_SIZE 40u

enum d12_frontend_descriptor_heap_flags {
   D12_FRONTEND_DESCRIPTOR_HEAP_CPU_VISIBLE = 1u << 0,
   D12_FRONTEND_DESCRIPTOR_HEAP_SHADER_VISIBLE = 1u << 1,
};

struct d12_frontend_descriptor_heap_desc {
   uint32_t type;
   uint32_t num_descriptors;
   uint32_t flags;
   uint32_t node_mask;
};

struct d12_frontend_srv_desc {
   uint32_t format;
   uint32_t dimension;
   uint32_t component_mapping;
   union {
      struct {
         uint64_t first_element;
         uint32_t num_elements;
         uint32_t stride;
         uint32_t flags;
      } buffer;
      struct {
         uint32_t first_mip;
         uint32_t first_slice;
         uint32_t mip_levels;
         uint32_t array_size;
         uint32_t plane_slice;
         float min_lod;
      } texture;
      struct {
         uint32_t first_mip;
         uint32_t mip_levels;
         uint32_t first_face;
         uint32_t num_cubes;
         float min_lod;
      } cube;
   };
};

struct d12_frontend_uav_desc {
   uint32_t format;
   uint32_t dimension;
   union {
      struct {
         uint64_t first_element;
         uint32_t num_elements;
         uint32_t stride;
         uint64_t counter_offset;
         uint32_t flags;
      } buffer;
      struct {
         uint32_t mip_slice;
         uint32_t first_slice;
         uint32_t array_size;
         uint32_t plane_slice;
      } texture;
   };
};

struct d12_frontend_rtv_desc {
   uint32_t format;
   uint32_t dimension;
   union {
      struct {
         uint64_t first_element;
         uint32_t num_elements;
      } buffer;
      struct {
         uint32_t mip_slice;
         uint32_t first_slice;
         uint32_t array_size;
         uint32_t plane_slice;
      } texture;
   };
};

struct d12_frontend_dsv_desc {
   uint32_t format;
   uint32_t dimension;
   uint32_t flags;
   uint32_t mip_slice;
   uint32_t first_slice;
   uint32_t array_size;
};

struct d12_frontend_sampler_desc {
   uint32_t filter;
   uint32_t address_u;
   uint32_t address_v;
   uint32_t address_w;
   float mip_lod_bias;
   uint32_t max_anisotropy;
   uint32_t comparison_func;
   float border_color[4];
   float min_lod;
   float max_lod;
};

#define D12_FRONTEND_MAX_VERTEX_BUFFERS 32u

struct d12_frontend_vertex_buffer_view {
   uint64_t address;
   uint32_t size;
   uint32_t stride;
};

struct d12_frontend_index_buffer_view {
   uint64_t address;
   uint32_t size;
   uint32_t format;
};

struct d12_frontend_stream_output_buffer_view {
   uint64_t address;
   uint64_t size;
   uint64_t filled_size_address;
};

struct d12_frontend_indirect_argument_desc {
   uint32_t type;
   uint32_t root_parameter_index;
   uint32_t destination_offset;
   uint32_t value_count;
};

struct d12_frontend_command_signature_desc {
   uint32_t byte_stride;
   uint32_t argument_count;
   const struct d12_frontend_indirect_argument_desc *arguments;
   void *root_signature;
   uint32_t node_mask;
};

struct d12_frontend_rect {
   int32_t left;
   int32_t top;
   int32_t right;
   int32_t bottom;
};

struct d12_frontend_box {
   int32_t left;
   int32_t top;
   int32_t front;
   int32_t right;
   int32_t bottom;
   int32_t back;
};

enum d12_frontend_resource_barrier_type {
   D12_FRONTEND_BARRIER_TRANSITION,
   D12_FRONTEND_BARRIER_ALIASING,
   D12_FRONTEND_BARRIER_UAV,
};

struct d12_frontend_resource_barrier {
   uint32_t type;
   uint32_t flags;
   union {
      struct {
         void *resource;
         uint32_t subresource;
         uint32_t state_before;
         uint32_t state_after;
      } transition;
      struct {
         void *resource_before;
         void *resource_after;
      } aliasing;
      struct {
         void *resource;
      } uav;
   };
};

enum d12_frontend_texture_copy_type {
   D12_FRONTEND_TEXTURE_COPY_SUBRESOURCE,
   D12_FRONTEND_TEXTURE_COPY_FOOTPRINT,
};

struct d12_frontend_texture_copy_location {
   void *resource;
   uint32_t type;
   union {
      uint32_t subresource;
      struct {
         uint64_t offset;
         uint32_t format;
         uint32_t width;
         uint32_t height;
         uint32_t depth;
         uint32_t row_pitch;
      } footprint;
   };
};

/*
 * One-pass presentation blit description.  DXGI may require resolve,
 * rotation, scaling and display-format conversion in the same operation;
 * keeping that operation intact across the frontend seam avoids both a
 * temporary allocation and the tearing-prone multi-pass implementation that
 * the DXGI DDI contract explicitly warns drivers not to use for Present.
 * Rotation uses the DXGI numeric encoding (0 unspecified, 1 identity,
 * 2/3/4 counter-clockwise 90/180/270 degrees).
 */
struct d12_frontend_present_blt_desc {
   uint32_t destination_subresource;
   uint32_t destination_left;
   uint32_t destination_top;
   uint32_t destination_right;
   uint32_t destination_bottom;
   uint32_t source_subresource;
   uint32_t source_left;
   uint32_t source_top;
   uint32_t source_right;
   uint32_t source_bottom;
   uint32_t rotation;
   uint32_t resolve;
};

/*
 * The baseline D3D12 UMD DDI represents a fence as one placement per
 * physical adapter.  Keep this representation at the frontend seam: the
 * renderer needs the GPU addresses to emit ordered writes and a coherent CPU
 * view to implement WDDM 2.x's scheduler-side wait model without inventing a
 * private KMD channel.
 */
struct d12_frontend_fence_placement {
   uint64_t current_gpu_address;
   uint64_t monitored_gpu_address;
   volatile uint64_t *current_cpu_address;
   uint32_t flags;
};
