/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_umd_resource_private.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                         \
   do {                                                                      \
      if (!(value)) {                                                        \
         fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #value);                \
         return 1;                                                           \
      }                                                                      \
   } while (0)

int
main(void)
{
   const struct d12_frontend_heap_desc heap = {
      .size = UINT64_C(0x400000),
      .alignment = UINT64_C(0x10000),
      .memory_pool = D12_FRONTEND_MEMORY_POOL_L0,
      .cpu_page_property = D12_FRONTEND_CPU_PAGE_WRITE_BACK,
      .flags = D12_FRONTEND_HEAP_ALLOW_NON_RT_DS_TEXTURES,
      .creation_node_mask = 1,
      .visible_node_mask = 1,
   };
   const struct d12_frontend_resource_desc resource = {
      .dimension = 3,
      .width = 1024,
      .height = 1024,
      .depth_or_array_size = 1,
      .mip_levels = 1,
      .format = 28,
      .sample_count = 1,
      .layout = 1,
      .flags = D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE |
               D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
               D12_FRONTEND_RESOURCE_ALLOW_CROSS_ADAPTER,
      .row_pitch = 4096,
      .slice_pitch = UINT32_C(0x400000),
   };
   const struct d12_frontend_clear_value clear = {
      .format = 28,
      .color = {0.0f, 0.25f, 0.5f, 1.0f},
   };
   struct d12_umd_resource_private packed;
   struct d12_umd_resource_private parsed;

   {
      uint64_t gpu_virtual_address = 0;
      uint64_t heap_offset = 0;

      CHECK(d12_umd_resource_reuse_range(
         UINT64_C(0x40000), UINT64_C(0x4400010000), UINT64_C(0x10000),
         UINT64_C(0x80000), UINT64_C(0x10000), UINT64_C(0x10000),
         UINT64_C(0x10000), &gpu_virtual_address, &heap_offset));
      CHECK(gpu_virtual_address == UINT64_C(0x4400020000) &&
            heap_offset == UINT64_C(0x20000));
      CHECK(!d12_umd_resource_reuse_range(
         UINT64_C(0x40000), UINT64_C(0x4400010000), UINT64_C(0x10000),
         UINT64_C(0x80000), 1, UINT64_C(0x10000), UINT64_C(0x10000),
         NULL, NULL));
      CHECK(!d12_umd_resource_reuse_range(
         UINT64_C(0x40000), UINT64_C(0x4400010000), UINT64_C(0x10000),
         UINT64_C(0x80000), UINT64_C(0x40000), UINT64_C(0x10000),
         UINT64_C(0x10000), NULL, NULL));
      CHECK(!d12_umd_resource_reuse_range(
         UINT64_MAX, UINT64_MAX - UINT64_C(0x7fff), 0,
         UINT64_MAX, UINT64_C(0x10000), UINT64_C(0x10000),
         UINT64_C(0x10000), NULL, NULL));
      CHECK(!d12_umd_resource_reuse_range(
         UINT64_C(0x40000), UINT64_C(0x4400010000), UINT64_C(0x78000),
         UINT64_C(0x80000), UINT64_C(0x10000), UINT64_C(0x10000),
         UINT64_C(0x10000), NULL, NULL));
      CHECK(!d12_umd_resource_reuse_range(
         UINT64_C(0x40000), UINT64_C(0x4400010000), UINT64_C(0x10000),
         UINT64_C(0x80000), UINT64_C(0x10000), UINT64_C(0x10000),
         UINT64_C(0x18000), NULL, NULL));
   }

   CHECK(sizeof(packed) == 176);
   CHECK(d12_umd_resource_private_pack(&heap, &resource, &clear, &packed));
   CHECK(packed.kmd.struct_size == sizeof(struct agxw_resource_private));
   CHECK(d12_umd_resource_private_parse(&packed, sizeof(packed), &parsed));
   CHECK(parsed.heap.size == heap.size && parsed.resource.width == 1024);
   CHECK(parsed.resource.row_pitch == 4096 &&
         parsed.resource.slice_pitch == UINT32_C(0x400000));

   {
      struct d12_frontend_heap_desc multisample_heap = heap;
      struct d12_frontend_resource_desc multisample_resource = resource;

      multisample_heap.flags = D12_FRONTEND_HEAP_ALLOW_NON_RT_DS_TEXTURES;
      multisample_resource.layout = 2;
      multisample_resource.row_pitch = 0;
      multisample_resource.slice_pitch = 0;

      /* One-sample shader resources need no output flag. */
      multisample_resource.flags =
         D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE;
      CHECK(d12_umd_resource_private_pack(
         &multisample_heap, &multisample_resource, NULL, &packed));
      CHECK(d12_umd_resource_private_parse(
         &packed, sizeof(packed), &parsed));
      CHECK(parsed.resource.sample_count == 1);

      multisample_heap.flags = D12_FRONTEND_HEAP_ALLOW_RT_DS_TEXTURES;
      multisample_resource.sample_count = 2;
      multisample_resource.flags =
         D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
         D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE;
      CHECK(d12_umd_resource_private_pack(
         &multisample_heap, &multisample_resource, NULL, &packed));
      CHECK(d12_umd_resource_private_parse(
         &packed, sizeof(packed), &parsed));
      CHECK(parsed.resource.sample_count == 2);

      multisample_resource.sample_count = 4;
      multisample_resource.flags =
         D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL |
         D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE;
      CHECK(d12_umd_resource_private_pack(
         &multisample_heap, &multisample_resource, NULL, &packed));
      CHECK(d12_umd_resource_private_parse(
         &packed, sizeof(packed), &parsed));
      CHECK(parsed.resource.sample_count == 4);

      multisample_resource.flags =
         D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE;
      multisample_resource.sample_count = 2;
      CHECK(!d12_umd_resource_private_pack(
         &multisample_heap, &multisample_resource, NULL, &packed));
      multisample_resource.sample_count = 4;
      CHECK(!d12_umd_resource_private_pack(
         &multisample_heap, &multisample_resource, NULL, &packed));

      /* DescriptorGen10 layout evidence must not widen the current D3D12
       * UMD contract: private metadata remains 1x/2x/4x only. */
      multisample_resource.sample_count = 8;
      multisample_resource.flags =
         D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
         D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE;
      CHECK(!d12_umd_resource_private_pack(
         &multisample_heap, &multisample_resource, NULL, &packed));
   }

   packed.magic ^= 1;
   CHECK(!d12_umd_resource_private_parse(&packed, sizeof(packed), &parsed));
   packed.magic ^= 1;
   CHECK(!d12_umd_resource_private_parse(&packed, sizeof(packed) - 1,
                                         &parsed));
   packed.reserved[1] = 1;
   CHECK(!d12_umd_resource_private_parse(&packed, sizeof(packed), &parsed));

   {
      uint64_t standard_descriptor = 0;
      uint32_t width = 0, height = 0, d3d_format = 0;
      struct agxw_resource_private foreign_resource = {
         .version = AGXW_RESOURCE_PRIVATE_VERSION,
         .struct_size = sizeof(foreign_resource),
         .allocation_count = AGXW_MAX_ALLOCATIONS_PER_RESOURCE,
      };
      struct agxw_alloc_private foreign_allocation = {
         .version = AGXW_ALLOC_PRIVATE_VERSION,
         .struct_size = sizeof(foreign_allocation),
         .size = UINT64_C(0x400000),
         .flags = AGXW_ALLOC_SHAREABLE | AGXW_ALLOC_WRITEBACK,
         .align_log2 = AGXW_WDDM_LARGE_PAGE_BITS,
      };
      struct d12_frontend_resource_desc standard_resource = resource;

      standard_resource.layout = 0;
      standard_resource.row_pitch = 0;
      standard_resource.slice_pitch = 0;

      CHECK(d12_umd_resource_private_standard_desc_pack(
         &standard_resource, &standard_descriptor));
      CHECK(!d12_umd_resource_private_standard_desc_pack(
         &resource, &standard_descriptor));
      CHECK(agxw_standard_desc_unpack(standard_descriptor, &width, &height,
                                      &d3d_format) == 0);
      CHECK(width == 1024 && height == 1024 && d3d_format == 32);
      foreign_allocation.reserved = standard_descriptor;
      CHECK(d12_umd_resource_private_parse_foreign(
         &foreign_resource, sizeof(foreign_resource), &foreign_allocation,
         &parsed));
      CHECK(parsed.flags == D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE);
      CHECK(parsed.heap.size == foreign_allocation.size &&
            parsed.heap.alignment == UINT64_C(0x10000) &&
            parsed.heap.cpu_page_property ==
               D12_FRONTEND_CPU_PAGE_WRITE_BACK);
      CHECK(parsed.heap.flags & D12_FRONTEND_HEAP_ALLOW_RT_DS_TEXTURES);
      CHECK(parsed.resource.dimension == 3 &&
            parsed.resource.width == 1024 &&
            parsed.resource.height == 1024 &&
            parsed.resource.format == 28 &&
            parsed.resource.sample_count == 1 &&
            parsed.resource.flags ==
               (D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
                D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE));

      CHECK(!d12_umd_resource_private_parse_foreign(
         &foreign_resource, sizeof(foreign_resource) - 1,
         &foreign_allocation, &parsed));
      foreign_allocation.flags = AGXW_ALLOC_VM_PRIVATE |
                                 AGXW_ALLOC_WRITEBACK;
      CHECK(!d12_umd_resource_private_parse_foreign(
         &foreign_resource, sizeof(foreign_resource), &foreign_allocation,
         &parsed));
      foreign_allocation.flags = AGXW_ALLOC_SHAREABLE |
                                 AGXW_ALLOC_WRITEBACK;
      foreign_allocation.size = UINT64_C(0x1000);
      CHECK(!d12_umd_resource_private_parse_foreign(
         &foreign_resource, sizeof(foreign_resource), &foreign_allocation,
         &parsed));
      foreign_allocation.size = UINT64_C(0x400000);
      CHECK(agxw_standard_desc_pack(1024, 1024, 1,
                                    &foreign_allocation.reserved) == 0);
      CHECK(!d12_umd_resource_private_parse_foreign(
         &foreign_resource, sizeof(foreign_resource), &foreign_allocation,
         &parsed));
   }

   {
      struct d12_frontend_heap_desc invalid_heap = heap;
      invalid_heap.alignment = UINT64_C(0x18000);
      CHECK(!d12_umd_resource_private_pack(
         &invalid_heap, &resource, &clear, &packed));
      invalid_heap = heap;
      invalid_heap.flags = D12_FRONTEND_HEAP_PRIMARY;
      CHECK(!d12_umd_resource_private_pack(
         &invalid_heap, &resource, &clear, &packed));
   }

   {
      struct d12_frontend_resource_desc invalid_resource = resource;
      invalid_resource.sample_count = 3;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, &packed));
      invalid_resource = resource;
      invalid_resource.layout = 2;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, &packed));
      invalid_resource = resource;
      invalid_resource.flags |= D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, &packed));
      invalid_resource = resource;
      invalid_resource.row_pitch = 255;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, &packed));
      invalid_resource = resource;
      invalid_resource.slice_pitch--;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, &packed));
      invalid_resource = resource;
      invalid_resource.flags &= ~D12_FRONTEND_RESOURCE_ALLOW_CROSS_ADAPTER;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, &packed));
      invalid_resource = resource;
      invalid_resource.row_pitch = 0;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, &packed));
      invalid_resource = resource;
      invalid_resource.height = UINT32_C(16777216);
      invalid_resource.row_pitch = 256;
      invalid_resource.slice_pitch = UINT32_MAX;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, &packed));
   }

   puts("d12_umd_resource_private_tests: PASS");
   return 0;
}
