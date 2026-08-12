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
               D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET,
   };
   const struct d12_frontend_clear_value clear = {
      .format = 28,
      .color = {0.0f, 0.25f, 0.5f, 1.0f},
   };
   struct d12_umd_resource_private packed;
   struct d12_umd_resource_private parsed;

   CHECK(sizeof(packed) == 176);
   CHECK(d12_umd_resource_private_pack(&heap, &resource, &clear, 4096,
                                       0x400000, &packed));
   CHECK(packed.kmd.struct_size == sizeof(struct agxw_resource_private));
   CHECK(d12_umd_resource_private_parse(&packed, sizeof(packed), &parsed));
   CHECK(parsed.heap.size == heap.size && parsed.resource.width == 1024);
   CHECK(parsed.row_pitch == 4096 && parsed.slice_pitch == 0x400000);

   packed.magic ^= 1;
   CHECK(!d12_umd_resource_private_parse(&packed, sizeof(packed), &parsed));
   packed.magic ^= 1;
   CHECK(!d12_umd_resource_private_parse(&packed, sizeof(packed) - 1,
                                         &parsed));
   packed.reserved[1] = 1;
   CHECK(!d12_umd_resource_private_parse(&packed, sizeof(packed), &parsed));

   {
      struct d12_frontend_heap_desc invalid_heap = heap;
      invalid_heap.alignment = UINT64_C(0x18000);
      CHECK(!d12_umd_resource_private_pack(
         &invalid_heap, &resource, &clear, 4096, 0x400000, &packed));
      invalid_heap = heap;
      invalid_heap.flags = D12_FRONTEND_HEAP_PRIMARY;
      CHECK(!d12_umd_resource_private_pack(
         &invalid_heap, &resource, &clear, 4096, 0x400000, &packed));
   }

   {
      struct d12_frontend_resource_desc invalid_resource = resource;
      invalid_resource.sample_count = 3;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, 4096, 0x400000, &packed));
      invalid_resource = resource;
      invalid_resource.layout = 2;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, 4096, 0x400000, &packed));
      invalid_resource = resource;
      invalid_resource.flags |= D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL;
      CHECK(!d12_umd_resource_private_pack(
         &heap, &invalid_resource, &clear, 4096, 0x400000, &packed));
   }

   puts("d12_umd_resource_private_tests: PASS");
   return 0;
}
