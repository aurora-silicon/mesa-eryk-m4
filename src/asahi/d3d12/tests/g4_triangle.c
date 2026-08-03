/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * d3d12agx G4 final acceptance: THE TRIANGLE.
 *
 * A vertex shader and a pixel shader, both authored in HLSL, compiled to
 * DXIL by dxc, handed to the driver through the public D3D12 API,
 * translated by us to NIR, compiled to AGX machine code, and rasterized
 * by the Apple GPU. No SPIR-V, no Vulkan, no external compiler.
 *
 *   VS: float4 main(uint vid : SV_VertexID) : SV_Position
 *       — a full-screen-ish triangle from the vertex index alone.
 *   PS: returns a constant colour.
 *
 * The target is cleared to one colour and the triangle drawn in another,
 * so the test verifies BOTH that the triangle appears (centre pixel is
 * the triangle colour) and that it is bounded (a corner keeps the clear
 * colour). A solid-filled or empty target fails.
 */

#define COBJMACROS
#include <wsl/winadapter.h>
#include <directx/d3d12.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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

#define W 256
#define H 256
#define STRIDE (((W * 4) + 63) & ~63u)

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
   if (argc < 3) {
      fprintf(stderr, "usage: %s tri.dxil solid.dxil\n", argv[0]);
      return 2;
   }

   size_t vs_size, ps_size;
   void *vs = slurp(argv[1], &vs_size);
   void *ps = slurp(argv[2], &ps_size);
   if (!vs || !ps) {
      fprintf(stderr, "cannot read shaders\n");
      return 2;
   }

   ID3D12Device *dev = NULL;
   HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                                  &IID_ID3D12Device, (void **)&dev);
   CHECK(hr == S_OK && dev, "D3D12CreateDevice");
   if (FAILED(hr) || !dev)
      return 1;

   ID3D12RootSignature *root_sig = NULL;
   ID3D12Device_CreateRootSignature(dev, 0, vs, 4, &IID_ID3D12RootSignature,
                                    (void **)&root_sig);

   /* Compile BOTH shaders from DXIL to AGX inside the driver. */
   D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {
      .pRootSignature = root_sig,
      .VS = {.pShaderBytecode = vs, .BytecodeLength = vs_size},
      .PS = {.pShaderBytecode = ps, .BytecodeLength = ps_size},
      .NumRenderTargets = 1,
      .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
      .SampleDesc = {.Count = 1},
      .SampleMask = ~0u,
   };
   ID3D12PipelineState *pso = NULL;
   hr = ID3D12Device_CreateGraphicsPipelineState(dev, &pd,
                                                 &IID_ID3D12PipelineState,
                                                 (void **)&pso);
   CHECK(hr == S_OK && pso,
         "CreateGraphicsPipelineState compiles VS+PS from DXIL to AGX");
   if (FAILED(hr) || !pso)
      return 1;

   /* Render target */
   D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
   D3D12_RESOURCE_DESC td = {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = W,
      .Height = H,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
   };
   ID3D12Resource *rt = NULL;
   hr = ID3D12Device_CreateCommittedResource(
      dev, &heap, D3D12_HEAP_FLAG_NONE, &td,
      D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
      (void **)&rt);
   CHECK(hr == S_OK && rt, "create 256x256 render target");

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
   CHECK(map != NULL, "map render target");
   memset(map, 0x55, STRIDE * H);

   /* Record: clear to red, then draw a green triangle. */
   ID3D12CommandAllocator *alloc = NULL;
   ID3D12GraphicsCommandList *list = NULL;
   ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       &IID_ID3D12CommandAllocator,
                                       (void **)&alloc);
   ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  alloc, NULL,
                                  &IID_ID3D12GraphicsCommandList,
                                  (void **)&list);

   const FLOAT clear_red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
   ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, clear_red, 0,
                                                   NULL);
   ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);

   D3D12_VIEWPORT vp = {.Width = W, .Height = H, .MaxDepth = 1.0f};
   ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
   D3D12_RECT sc = {.right = W, .bottom = H};
   ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);

   ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root_sig);
   ID3D12GraphicsCommandList_SetPipelineState(list, pso);
   ID3D12GraphicsCommandList_IASetPrimitiveTopology(
      list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);

   hr = ID3D12GraphicsCommandList_Close(list);
   CHECK(hr == S_OK, "Close command list (clear + draw)");

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
   hr = ID3D12Fence_SetEventOnCompletion(fence, 1, NULL);
   CHECK(hr == S_OK, "GPU render complete");

#define PIXEL(x, y) (map + (y) * STRIDE + (x) * 4)

   const uint8_t *centre = PIXEL(W / 2, H / 2);
   const uint8_t *corner = PIXEL(2, 2);

   printf("       centre (128,128) = %02x %02x %02x %02x\n", centre[0],
          centre[1], centre[2], centre[3]);
   printf("       corner (2,2)     = %02x %02x %02x %02x\n", corner[0],
          corner[1], corner[2], corner[3]);

   /* The clear must have happened everywhere. */
   bool cleared = corner[0] == 0xff && corner[1] == 0x00 && corner[2] == 0x00;
   CHECK(cleared, "clear reached the corner (red)");

   /* The triangle must cover the centre with the pixel shader's colour
    * (0.25, 0.5, 0.75, 1.0 from solid.hlsl).
    */
   bool drew = centre[0] == 0x40 && centre[1] == 0x80 && centre[2] == 0xbf;
   CHECK(drew, "triangle covers the centre with the pixel shader colour");

   /* Count how many pixels differ from the clear colour: a real triangle
    * covers a meaningful fraction but nowhere near everything.
    */
   unsigned covered = 0;
   for (unsigned y = 0; y < H; y++) {
      for (unsigned x = 0; x < W; x++) {
         const uint8_t *p = PIXEL(x, y);
         if (!(p[0] == 0xff && p[1] == 0x00 && p[2] == 0x00))
            covered++;
      }
   }
   printf("       covered %u of %u pixels (%.1f%%)\n", covered, W * H,
          100.0 * covered / (W * H));
   CHECK(covered > (W * H) / 20 && covered < (W * H) * 9 / 10,
         "coverage is a real triangle, not empty or fully filled");

   /* RSSetViewports used to be recorded and never read. Re-render into a
    * half-height viewport: a viewport that is honoured must shrink the
    * covered area, and must leave the bottom of the target cleared.
    */
   {
      memset(map, 0x55, STRIDE * H);
      ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
      ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, clear_red, 0,
                                                      NULL);
      D3D12_VIEWPORT half = {.Width = W, .Height = H / 2, .MaxDepth = 1.0f};
      ID3D12GraphicsCommandList_RSSetViewports(list, 1, &half);
      ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);
      ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root_sig);
      ID3D12GraphicsCommandList_SetPipelineState(list, pso);
      ID3D12GraphicsCommandList_IASetPrimitiveTopology(
         list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
      ID3D12GraphicsCommandList_Close(list);
      ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
      ID3D12CommandQueue_Signal(queue, fence, 2);
      ID3D12Fence_SetEventOnCompletion(fence, 2, NULL);

      unsigned half_cov = 0;
      for (unsigned y = 0; y < H; y++) {
         for (unsigned x = 0; x < W; x++) {
            const uint8_t *p = PIXEL(x, y);
            if (!(p[0] == 0xff && p[1] == 0x00 && p[2] == 0x00))
               half_cov++;
         }
      }
      printf("       half-height viewport covered %u (full was %u)\n",
             half_cov, covered);
      CHECK(half_cov > 0 && half_cov < covered,
            "RSSetViewports is honoured: half viewport shrinks coverage");

      const uint8_t *low = PIXEL(W / 2, H - 4);
      CHECK(low[0] == 0xff && low[1] == 0x00 && low[2] == 0x00,
            "area below the viewport keeps the clear colour");
   }

   ID3D12Resource_Unmap(rt, 0, NULL);
   ID3D12Fence_Release(fence);
   ID3D12CommandQueue_Release(queue);
   ID3D12GraphicsCommandList_Release(list);
   ID3D12CommandAllocator_Release(alloc);
   ID3D12DescriptorHeap_Release(rtv_heap);
   ID3D12Resource_Release(rt);
   ID3D12PipelineState_Release(pso);
   ID3D12RootSignature_Release(root_sig);
   ID3D12Device_Release(dev);

   printf(failures ? "G4 TRIANGLE: %d FAILURES\n" : "G4 TRIANGLE: ALL PASS\n",
          failures);
   free(vs);
   free(ps);
   return failures;
}
