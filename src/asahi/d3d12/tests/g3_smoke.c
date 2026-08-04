/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * d3d12agx G3 acceptance test: RTV descriptor heaps and
 * ClearRenderTargetView as a real tiler render pass (background clear +
 * end-of-tile store over PROCESS_EMPTY_TILES), pixel-exact on linear RGBA8
 * textures the CPU can inspect directly.
 *
 * Two sizes: 256x256 (tile-aligned) and 123x77 (deliberately unaligned in
 * both dimensions). Row-padding bytes between width*4 and the 64-byte-
 * aligned stride must keep their poison — proving the store writes exactly
 * the image, not the tile.
 */

#define COBJMACROS
#include <wsl/winadapter.h>
#include <directx/d3d12.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what)                                                    \
   do {                                                                      \
      if (cond) {                                                            \
         printf("ok   - %s\n", what);                                        \
      } else {                                                               \
         printf("FAIL - %s\n", what);                                        \
         failures++;                                                         \
      }                                                                      \
   } while (0)

/* Must mirror the driver's linear stride rule (d12_resource.c) */
#define STRIDE_B(w) ((((w) * 4) + 63) & ~63u)

static HRESULT
make_rt_texture(ID3D12Device *dev, UINT w, UINT h, ID3D12Resource **out)
{
   D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
   D3D12_RESOURCE_DESC desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = w,
      .Height = h,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
   };

   return ID3D12Device_CreateCommittedResource(
      dev, &heap, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
      (void **)out);
}

/* Expect every pixel of the w*h image to equal rgba; padding keeps poison */
static int
check_image(const uint8_t *map, UINT w, UINT h, const uint8_t rgba[4],
            int check_padding)
{
   UINT stride = STRIDE_B(w);

   for (UINT y = 0; y < h; y++) {
      const uint8_t *row = map + y * stride;

      for (UINT x = 0; x < w; x++) {
         if (memcmp(row + x * 4, rgba, 4)) {
            printf("       pixel (%u,%u): got %02x%02x%02x%02x want "
                   "%02x%02x%02x%02x\n",
                   x, y, row[x * 4], row[x * 4 + 1], row[x * 4 + 2],
                   row[x * 4 + 3], rgba[0], rgba[1], rgba[2], rgba[3]);
            return 0;
         }
      }

      if (check_padding) {
         for (UINT b = w * 4; b < stride; b++) {
            if (row[b] != 0x55) {
               printf("       row %u padding byte %u overwritten: %02x\n", y,
                      b, row[b]);
               return 0;
            }
         }
      }
   }

   return 1;
}

int
main(void)
{
   ID3D12Device *dev = NULL;
   HRESULT hr;

   hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device,
                          (void **)&dev);
   CHECK(hr == S_OK && dev, "D3D12CreateDevice");
   if (FAILED(hr) || !dev)
      return 1;

   /* RTV heap with two slots */
   D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = 2,
   };
   ID3D12DescriptorHeap *rtv_heap = NULL;
   hr = ID3D12Device_CreateDescriptorHeap(dev, &heap_desc,
                                          &IID_ID3D12DescriptorHeap,
                                          (void **)&rtv_heap);
   CHECK(hr == S_OK && rtv_heap, "CreateDescriptorHeap(RTV, 2)");

   UINT inc = ID3D12Device_GetDescriptorHandleIncrementSize(
      dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
   D3D12_CPU_DESCRIPTOR_HANDLE rtv_a =
      ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);
   D3D12_CPU_DESCRIPTOR_HANDLE rtv_b = {.ptr = rtv_a.ptr + inc};
   /* The increment is sizeof(struct d12_descriptor) and is OPAQUE to
    * applications. It grew from 32 to 40 when UAV counter addresses were
    * added to the descriptor, and this assertion kept the old literal --
    * failing silently because nothing ran this suite afterwards.
    *
    * Assert the CONTRACT instead of a number that goes stale: a non-zero
    * stride that genuinely separates two descriptors. That separation is
    * exactly what the two pixel-exact clears further down prove.
    */
   CHECK(inc >= sizeof(uint64_t) && rtv_a.ptr && rtv_b.ptr != rtv_a.ptr,
         "heap start, and an opaque non-zero handle increment");

   /* Textures: aligned and deliberately unaligned */
   ID3D12Resource *tex_a = NULL, *tex_b = NULL;
   CHECK(make_rt_texture(dev, 256, 256, &tex_a) == S_OK &&
            make_rt_texture(dev, 123, 77, &tex_b) == S_OK,
         "create 256x256 and 123x77 RGBA8 render targets");

   ID3D12Device_CreateRenderTargetView(dev, tex_a, NULL, rtv_a);
   ID3D12Device_CreateRenderTargetView(dev, tex_b, NULL, rtv_b);

   uint8_t *map_a = NULL, *map_b = NULL;
   CHECK(ID3D12Resource_Map(tex_a, 0, NULL, (void **)&map_a) == S_OK &&
            ID3D12Resource_Map(tex_b, 0, NULL, (void **)&map_b) == S_OK,
         "map textures (UMA linear)");

   memset(map_a, 0x55, STRIDE_B(256) * 256);
   memset(map_b, 0x55, STRIDE_B(123) * 77);

   /* Also prove render+compute segment mixing in one list */
   D3D12_HEAP_PROPERTIES hu = {.Type = D3D12_HEAP_TYPE_UPLOAD};
   D3D12_RESOURCE_DESC bd = {
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Width = 256,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .SampleDesc = {.Count = 1},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
   };
   ID3D12Resource *buf_src = NULL, *buf_dst = NULL;
   hr = ID3D12Device_CreateCommittedResource(dev, &hu, D3D12_HEAP_FLAG_NONE,
                                             &bd, D3D12_RESOURCE_STATE_COMMON,
                                             NULL, &IID_ID3D12Resource,
                                             (void **)&buf_src);
   HRESULT hr2 = ID3D12Device_CreateCommittedResource(
      dev, &hu, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COMMON, NULL,
      &IID_ID3D12Resource, (void **)&buf_dst);
   CHECK(hr == S_OK && hr2 == S_OK, "create side buffers");

   uint8_t *src_map = NULL, *dst_map = NULL;
   ID3D12Resource_Map(buf_src, 0, NULL, (void **)&src_map);
   ID3D12Resource_Map(buf_dst, 0, NULL, (void **)&dst_map);
   for (unsigned i = 0; i < 256; i++)
      src_map[i] = (uint8_t)(i ^ 0xC3);
   memset(dst_map, 0, 256);

   /* Record: clear A, clear B, then a buffer copy — three segments */
   ID3D12CommandAllocator *alloc = NULL;
   ID3D12GraphicsCommandList *list = NULL;
   ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       &IID_ID3D12CommandAllocator,
                                       (void **)&alloc);
   hr = ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       alloc, NULL,
                                       &IID_ID3D12GraphicsCommandList,
                                       (void **)&list);
   CHECK(hr == S_OK, "CreateCommandList");

   /* Exact-representable UNORM values: no rounding ambiguity */
   const FLOAT color_a[4] = {1.0f, 0.0f, 128.0f / 255.0f, 1.0f};
   const FLOAT color_b[4] = {0.0f, 1.0f, 0.0f, 1.0f};
   const uint8_t bytes_a[4] = {255, 0, 128, 255};
   const uint8_t bytes_b[4] = {0, 255, 0, 255};

   ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv_a, color_a, 0,
                                                   NULL);
   ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv_b, color_b, 0,
                                                   NULL);
   ID3D12GraphicsCommandList_CopyBufferRegion(list, buf_dst, 0, buf_src, 0,
                                              256);
   hr = ID3D12GraphicsCommandList_Close(list);
   CHECK(hr == S_OK, "Close (2 render + 1 compute segment)");

   ID3D12CommandQueue *queue = NULL;
   D3D12_COMMAND_QUEUE_DESC qdesc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ID3D12Device_CreateCommandQueue(dev, &qdesc, &IID_ID3D12CommandQueue,
                                   (void **)&queue);
   ID3D12Fence *fence = NULL;
   ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                            (void **)&fence);

   ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
   ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
   ID3D12CommandQueue_Signal(queue, fence, 1);
   hr = ID3D12Fence_SetEventOnCompletion(fence, 1, NULL);
   CHECK(hr == S_OK && ID3D12Fence_GetCompletedValue(fence) >= 1,
         "GPU render + copy complete (fence reached 1)");

   CHECK(check_image(map_a, 256, 256, bytes_a, 0),
         "256x256 cleared pixel-exact to (255,0,128,255)");
   CHECK(check_image(map_b, 123, 77, bytes_b, 1),
         "123x77 cleared pixel-exact, row padding untouched");
   CHECK(memcmp(dst_map, src_map, 256) == 0,
         "compute segment after renders landed (256B copy)");

   /* Re-clear A to B's color through list reuse */
   hr = ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
   CHECK(hr == S_OK, "Reset for re-clear");
   ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv_a, color_b, 0,
                                                   NULL);
   ID3D12GraphicsCommandList_Close(list);
   ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
   ID3D12CommandQueue_Signal(queue, fence, 2);
   hr = ID3D12Fence_SetEventOnCompletion(fence, 2, NULL);
   CHECK(hr == S_OK && check_image(map_a, 256, 256, bytes_b, 0),
         "re-clear via reused list lands (256x256 now green)");

   ID3D12Resource_Unmap(tex_a, 0, NULL);
   ID3D12Resource_Unmap(tex_b, 0, NULL);
   ID3D12Fence_Release(fence);
   ID3D12CommandQueue_Release(queue);
   ID3D12GraphicsCommandList_Release(list);
   ID3D12CommandAllocator_Release(alloc);
   ID3D12Resource_Release(buf_src);
   ID3D12Resource_Release(buf_dst);
   ID3D12Resource_Release(tex_a);
   ID3D12Resource_Release(tex_b);
   ID3D12DescriptorHeap_Release(rtv_heap);
   CHECK(ID3D12Device_Release(dev) == 0, "device refcount reaches 0");

   printf(failures ? "G3 SMOKE: %d FAILURES\n" : "G3 SMOKE: ALL PASS\n",
          failures);
   return failures;
}
