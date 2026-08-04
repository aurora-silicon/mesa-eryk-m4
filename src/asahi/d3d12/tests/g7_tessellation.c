/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * G7: tessellation executes.
 *
 * AGX has no hardware tessellator. A tessellation pipeline becomes five
 * compute dispatches ordered ahead of the render pass -- software vertex
 * shader, hull shader, tessellator COUNT pass, prefix sum, tessellator
 * emit pass -- followed by an INDEXED INDIRECT draw whose index count
 * only exists after the GPU prefix sum. The domain shader is the
 * hardware vertex shader.
 *
 * The expectation here is derived, not calibrated. tess_hs declares a
 * quad domain with integer spacing and constant tessellation factors of
 * 2, so the patch tessellates into a 2x2 grid of quads covering the full
 * domain. tess_ds places each domain point by bilinear interpolation of
 * the four control points, which gs_input_vs puts at (+-0.5, +-0.5). The
 * result is therefore a solid quad covering NDC [-0.5, 0.5]^2, which on
 * a 128x128 target is x,y in [32, 96) -- exactly 64 x 64 = 4096 pixels.
 *
 * Any mis-tessellation changes that number: a bowtie from the wrong
 * corner order covers half, a dropped inner ring leaves a hole, and a
 * tessellator that emits nothing covers zero.
 */

#define COBJMACROS
#include <wsl/winadapter.h>
#include <directx/d3d12.h>

#include <stdbool.h>
#include <stdint.h>
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

#define W 128
#define H 128
#define STRIDE (((W * 4) + 63) & ~63u)

/* The tessellated patch covers NDC [-0.5,0.5]^2. */
#define QUAD_LO 32
#define QUAD_HI 96

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
   if (p)
      *size = n;
   return p;
}

int
main(int argc, char **argv)
{
   if (argc < 5) {
      printf("usage: %s vs.dxil ps.dxil hs.dxil ds.dxil\n", argv[0]);
      return 77;
   }

   size_t vs_size = 0, ps_size = 0, hs_size = 0, ds_size = 0;
   void *vs = slurp(argv[1], &vs_size);
   void *ps = slurp(argv[2], &ps_size);
   void *hs = slurp(argv[3], &hs_size);
   void *ds = slurp(argv[4], &ds_size);
   if (!vs || !ps || !hs || !ds) {
      printf("FAIL - could not read the shaders\n");
      return 1;
   }

   ID3D12Device *dev = NULL;
   HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                                  &IID_ID3D12Device, (void **)&dev);
   CHECK(hr == S_OK && dev, "D3D12CreateDevice");
   if (!dev)
      return 1;

   D3D12_ROOT_SIGNATURE_DESC rsd = {
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
   };
   ID3DBlob *sig = NULL;
   hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig,
                                    NULL);
   ID3D12RootSignature *root_sig = NULL;
   if (hr == S_OK)
      hr = ID3D12Device_CreateRootSignature(
         dev, 0, ID3D10Blob_GetBufferPointer(sig),
         ID3D10Blob_GetBufferSize(sig), &IID_ID3D12RootSignature,
         (void **)&root_sig);
   CHECK(hr == S_OK && root_sig, "create root signature");
   if (!root_sig)
      return 1;

   D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {
      .pRootSignature = root_sig,
      .VS = {.pShaderBytecode = vs, .BytecodeLength = vs_size},
      .PS = {.pShaderBytecode = ps, .BytecodeLength = ps_size},
      .HS = {.pShaderBytecode = hs, .BytecodeLength = hs_size},
      .DS = {.pShaderBytecode = ds, .BytecodeLength = ds_size},
      .NumRenderTargets = 1,
      .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
      .RasterizerState = {.CullMode = D3D12_CULL_MODE_NONE},
      .SampleDesc = {.Count = 1},
      .SampleMask = ~0u,
   };

   ID3D12PipelineState *pso = NULL;
   hr = ID3D12Device_CreateGraphicsPipelineState(
      dev, &pd, &IID_ID3D12PipelineState, (void **)&pso);
   CHECK(hr == S_OK && pso,
         "a tessellation pipeline is CREATED, not refused");
   if (!pso)
      goto done;

   D3D12_HEAP_PROPERTIES hp = {.Type = D3D12_HEAP_TYPE_DEFAULT};
   D3D12_RESOURCE_DESC rd = {
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
      dev, &hp, D3D12_HEAP_FLAG_NONE, &rd,
      D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
      (void **)&rt);
   CHECK(hr == S_OK && rt, "create render target");
   if (!rt)
      goto done;

   D3D12_DESCRIPTOR_HEAP_DESC hd = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                    .NumDescriptors = 1};
   ID3D12DescriptorHeap *rtv_heap = NULL;
   ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap,
                                     (void **)&rtv_heap);
   D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);
   ID3D12Device_CreateRenderTargetView(dev, rt, NULL, rtv);

   D3D12_COMMAND_QUEUE_DESC qd = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ID3D12CommandQueue *queue = NULL;
   ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue,
                                   (void **)&queue);
   ID3D12CommandAllocator *alloc = NULL;
   ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       &IID_ID3D12CommandAllocator,
                                       (void **)&alloc);
   ID3D12GraphicsCommandList *list = NULL;
   ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  alloc, NULL, &IID_ID3D12GraphicsCommandList,
                                  (void **)&list);
   ID3D12Fence *fence = NULL;
   ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                            (void **)&fence);
   ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};

   uint8_t *map = NULL;
   ID3D12Resource_Map(rt, 0, NULL, (void **)&map);
   memset(map, 0x55, STRIDE * H);

   const FLOAT clear_red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
   D3D12_VIEWPORT vp = {.Width = W, .Height = H, .MaxDepth = 1.0f};
   D3D12_RECT sc = {.right = W, .bottom = H};

   ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
   ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, clear_red, 0,
                                                   NULL);
   ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
   ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);
   ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root_sig);
   ID3D12GraphicsCommandList_SetPipelineState(list, pso);
   ID3D12GraphicsCommandList_IASetPrimitiveTopology(
      list, D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
   ID3D12GraphicsCommandList_DrawInstanced(list, 4, 1, 0, 0);
   hr = ID3D12GraphicsCommandList_Close(list);
   CHECK(hr == S_OK, "tessellation command list closes cleanly");
   if (hr != S_OK)
      goto teardown;

   ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
   ID3D12CommandQueue_Signal(queue, fence, 1);
   ID3D12Fence_SetEventOnCompletion(fence, 1, NULL);

   CHECK(ID3D12Fence_GetCompletedValue(fence) >= 1,
         "the GPU COMPLETES a tessellated draw: five compute dispatches and "
         "an indexed indirect draw are a stream it accepts");

   /* Grade against the derived quad, not against whatever came out. */
   unsigned covered = 0, inside_wrong = 0, outside_wrong = 0;
   for (unsigned y = 0; y < H; y++) {
      for (unsigned x = 0; x < W; x++) {
         const uint8_t *p = map + y * STRIDE + x * 4;
         bool is_quad = p[0] == 0x40 && p[1] == 0x80 && p[2] == 0xbf;
         bool is_clear = p[0] == 0xff && p[1] == 0x00 && p[2] == 0x00;
         bool want = x >= QUAD_LO && x < QUAD_HI && y >= QUAD_LO &&
                     y < QUAD_HI;

         if (is_quad)
            covered++;
         if (want && !is_quad)
            inside_wrong++;
         if (!want && !is_clear)
            outside_wrong++;
      }
   }

   printf("       covered %u of %u (expected %u), inside wrong %u, "
          "outside wrong %u\n",
          covered, W * H, (QUAD_HI - QUAD_LO) * (QUAD_HI - QUAD_LO),
          inside_wrong, outside_wrong);

   CHECK(covered > 0,
         "the tessellator emits geometry: the domain shader runs and its "
         "output is rasterised");

   CHECK(covered == (QUAD_HI - QUAD_LO) * (QUAD_HI - QUAD_LO) &&
            inside_wrong == 0 && outside_wrong == 0,
         "the tessellated patch is EXACTLY the derived quad, pixel for "
         "pixel");

teardown:
   ID3D12Resource_Unmap(rt, 0, NULL);
   ID3D12Fence_Release(fence);
   ID3D12CommandQueue_Release(queue);
   ID3D12GraphicsCommandList_Release(list);
   ID3D12CommandAllocator_Release(alloc);
   ID3D12DescriptorHeap_Release(rtv_heap);
   ID3D12Resource_Release(rt);

done:
   if (pso)
      ID3D12PipelineState_Release(pso);
   ID3D12RootSignature_Release(root_sig);
   ID3D12Device_Release(dev);

   printf(failures ? "G7 TESSELLATION: %d FAILURES\n"
                   : "G7 TESSELLATION: ALL PASS\n",
          failures);
   free(vs);
   free(ps);
   free(hs);
   free(ds);
   return failures ? 1 : 0;
}
