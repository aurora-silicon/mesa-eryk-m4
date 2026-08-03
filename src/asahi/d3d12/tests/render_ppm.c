/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Render through the d3d12agx driver and write the result to a file, so the
 * GPU's actual output can be looked at rather than only asserted on.
 *
 * This is the same path the acceptance tests take -- HLSL compiled to DXIL by
 * dxc, translated to NIR by our own front end, compiled to AGX, rasterized by
 * the GPU -- with the framebuffer written out as a PPM instead of checked.
 *
 * Usage: render_ppm <vs.dxil> <ps.dxil> <out.ppm> [size]
 */

#define COBJMACROS
#include <wsl/winadapter.h>
#include <directx/d3d12.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *
slurp(const char *path, size_t *size)
{
   FILE *f = fopen(path, "rb");
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   void *p = malloc(n);
   if (p && fread(p, 1, n, f) != (size_t)n) {
      free(p);
      p = NULL;
   }
   fclose(f);
   *size = n;
   return p;
}

int
main(int argc, char **argv)
{
   if (argc < 4) {
      fprintf(stderr, "usage: %s vs.dxil ps.dxil out.ppm [size]\n", argv[0]);
      return 2;
   }

   const unsigned dim = argc > 4 ? (unsigned)atoi(argv[4]) : 512;
   const unsigned stride = ((dim * 4) + 63) & ~63u;

   size_t vs_size, ps_size;
   void *vs = slurp(argv[1], &vs_size);
   void *ps = slurp(argv[2], &ps_size);
   if (!vs || !ps) {
      fprintf(stderr, "cannot read shaders\n");
      return 2;
   }

   ID3D12Device *dev = NULL;
   if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                                &IID_ID3D12Device, (void **)&dev))) {
      fprintf(stderr, "no device\n");
      return 1;
   }

   ID3D12RootSignature *rs = NULL;
   ID3D12Device_CreateRootSignature(dev, 0, vs, 4, &IID_ID3D12RootSignature,
                                    (void **)&rs);

   D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {
      .pRootSignature = rs,
      .VS = {.pShaderBytecode = vs, .BytecodeLength = vs_size},
      .PS = {.pShaderBytecode = ps, .BytecodeLength = ps_size},
      .NumRenderTargets = 1,
      .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
      .SampleDesc = {.Count = 1},
      .SampleMask = ~0u,
   };
   ID3D12PipelineState *pso = NULL;
   if (FAILED(ID3D12Device_CreateGraphicsPipelineState(
          dev, &pd, &IID_ID3D12PipelineState, (void **)&pso))) {
      fprintf(stderr, "pipeline creation failed\n");
      return 1;
   }

   D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
   D3D12_RESOURCE_DESC td = {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = dim,
      .Height = dim,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
   };
   ID3D12Resource *rt = NULL;
   if (FAILED(ID3D12Device_CreateCommittedResource(
          dev, &heap, D3D12_HEAP_FLAG_NONE, &td,
          D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
          (void **)&rt))) {
      fprintf(stderr, "render target creation failed\n");
      return 1;
   }

   D3D12_DESCRIPTOR_HEAP_DESC hd = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = 1,
   };
   ID3D12DescriptorHeap *rtv_heap = NULL;
   ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap,
                                     (void **)&rtv_heap);
   D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);
   ID3D12Device_CreateRenderTargetView(dev, rt, NULL, rtv);

   uint8_t *map = NULL;
   ID3D12Resource_Map(rt, 0, NULL, (void **)&map);

   ID3D12CommandAllocator *alloc = NULL;
   ID3D12GraphicsCommandList *list = NULL;
   ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       &IID_ID3D12CommandAllocator,
                                       (void **)&alloc);
   ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  alloc, NULL,
                                  &IID_ID3D12GraphicsCommandList,
                                  (void **)&list);

   const FLOAT clear[4] = {0.06f, 0.07f, 0.10f, 1.0f};
   ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, clear, 0, NULL);
   ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);

   D3D12_VIEWPORT vp = {.Width = dim, .Height = dim, .MaxDepth = 1.0f};
   ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
   D3D12_RECT sc = {.right = (LONG)dim, .bottom = (LONG)dim};
   ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);

   ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, rs);
   ID3D12GraphicsCommandList_SetPipelineState(list, pso);
   ID3D12GraphicsCommandList_IASetPrimitiveTopology(
      list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
   ID3D12GraphicsCommandList_Close(list);

   ID3D12CommandQueue *queue = NULL;
   D3D12_COMMAND_QUEUE_DESC qd = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue,
                                   (void **)&queue);
   ID3D12Fence *fence = NULL;
   ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                            (void **)&fence);

   ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
   ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
   ID3D12CommandQueue_Signal(queue, fence, 1);
   ID3D12Fence_SetEventOnCompletion(fence, 1, NULL);

   /* Straight out of GPU memory to disk. */
   FILE *out = fopen(argv[3], "wb");
   if (!out) {
      fprintf(stderr, "cannot write %s\n", argv[3]);
      return 1;
   }
   fprintf(out, "P6\n%u %u\n255\n", dim, dim);
   for (unsigned y = 0; y < dim; y++) {
      const uint8_t *row = map + y * stride;
      for (unsigned x = 0; x < dim; x++)
         fwrite(row + x * 4, 1, 3, out);
   }
   fclose(out);

   printf("rendered %ux%u on %s -> %s\n", dim, dim, "Apple GPU", argv[3]);

   ID3D12Resource_Unmap(rt, 0, NULL);
   ID3D12Fence_Release(fence);
   ID3D12CommandQueue_Release(queue);
   ID3D12GraphicsCommandList_Release(list);
   ID3D12CommandAllocator_Release(alloc);
   ID3D12DescriptorHeap_Release(rtv_heap);
   ID3D12Resource_Release(rt);
   ID3D12PipelineState_Release(pso);
   ID3D12RootSignature_Release(rs);
   ID3D12Device_Release(dev);
   free(vs);
   free(ps);
   return 0;
}
