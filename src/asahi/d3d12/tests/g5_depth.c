/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * d3d12agx depth/stencil acceptance.
 *
 * A depth test is easy to fake: a driver that ignores depth entirely draws
 * everything, and one that mis-encodes the comparison draws nothing. Both
 * look plausible in a screenshot. So every case here comes in a PAIR that
 * differs by ONE number, with opposite expected outcomes -- same geometry,
 * same shaders, same pipeline except the field under test. Only a driver
 * that actually runs the test on the hardware can pass both halves.
 *
 *   depth value   0.75 vs 0.125 against a 0.25 clear   -> rejected / drawn
 *   depth func    LESS vs GREATER, same values         -> drawn / rejected
 *   write mask    ALL vs ZERO on a first draw          -> occludes / does not
 *
 * The last pair is the one that proves depth is really being STORED: the
 * second draw's fate depends entirely on what the first draw left behind.
 *
 * Grading is through the colour buffer, because AGX depth buffers must be
 * twiddled (the ZLS unit has no linear mode) and are not CPU-readable by
 * row. Two pixel shaders in different colours make "which draw won" visible.
 *
 * Usage: g5_depth tri.dxil solid.dxil tri_depth.dxil solid_g.dxil
 *                 tri_depth_cw.dxil
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

/* Where every flat-depth case is graded. It has to be inside BOTH
 * triangles this test draws, and outside neither by accident: grading at
 * a point the triangle never covers makes each case pass whenever the
 * draw was rejected, for any reason, including the right one.
 *
 * Read off the DXIL, not off a memory of the HLSL:
 *
 *   tri.dxil       clip (1,-1) (-1,-1) (5,1)  -> window (256,256)
 *                  (0,256) (768,0); the visible part is the wedge under
 *                  the hypotenuse y = 256 - x/3, so nothing above y=171
 *                  is covered at all.
 *   tri_depth.dxil clip (-0.9,-0.9) (0.9,-0.9) (0,0.9) -> window
 *                  (12.8,243.2) (243.2,243.2) (128,12.8), apex at the top.
 *
 * (220,240) is ~58 px below the first triangle's hypotenuse and ~22 px
 * inside the second's right edge. The previous point, (30,30), was inside
 * NEITHER -- it was chosen against a triangle description that does not
 * match either shader, and survived only because a translator bug made
 * tri.dxil emit garbage vertices. See docs/findings.md.
 */
#define IN_X 220
#define IN_Y 240

/* The clear colour, and the two pixel-shader colours, as RGBA8 bytes. */
static const FLOAT clear_rgba[4] = {1.0f, 0.0f, 0.0f, 1.0f};
static const uint8_t RED[4] = {255, 0, 0, 255};
/* solid.hlsl: float4(0.25, 0.5, 0.75, 1.0) */
static const uint8_t BLUE[4] = {64, 128, 191, 255};
/* solid_g.hlsl: float4(0.0, 1.0, 0.0, 1.0) */
static const uint8_t GREEN[4] = {0, 255, 0, 255};

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

static const uint8_t *
px(const uint8_t *map, unsigned x, unsigned y)
{
   return map + y * STRIDE + x * 4;
}

/* Name a pixel's colour for readable failure output. */
static const char *
colour_name(const uint8_t *p)
{
   if (!memcmp(p, RED, 4))
      return "RED (clear)";
   if (!memcmp(p, BLUE, 4))
      return "BLUE (solid.hlsl)";
   if (!memcmp(p, GREEN, 4))
      return "GREEN (solid_g.hlsl)";
   return "other";
}

static int
expect_pixel(const uint8_t *map, unsigned x, unsigned y,
             const uint8_t want[4], const char *what)
{
   const uint8_t *p = px(map, x, y);
   if (!memcmp(p, want, 4)) {
      printf("ok   - %s\n", what);
      return 1;
   }

   printf("FAIL - %s\n", what);
   printf("       (%u,%u) is %02x%02x%02x%02x = %s, wanted "
          "%02x%02x%02x%02x = %s\n",
          x, y, p[0], p[1], p[2], p[3], colour_name(p), want[0], want[1],
          want[2], want[3], colour_name(want));
   failures++;
   return 0;
}

struct ctx {
   ID3D12Device *dev;
   ID3D12CommandQueue *queue;
   ID3D12Fence *fence;
   UINT64 fence_value;
   ID3D12CommandAllocator *alloc;
   ID3D12GraphicsCommandList *list;
   ID3D12Resource *rt, *ds;
   D3D12_CPU_DESCRIPTOR_HANDLE rtv, dsv;
   uint8_t *map;
};

/* One recorded-and-executed frame. Depth state comes from the PSO, depth
 * value from the viewport: setting MinDepth == MaxDepth pins window Z to a
 * chosen constant no matter what the vertex shader emits, which keeps these
 * cases independent of the shader's own Z.
 */
static void
begin_frame(struct ctx *c, FLOAT clear_depth)
{
   ID3D12GraphicsCommandList_Reset(c->list, c->alloc, NULL);
   ID3D12GraphicsCommandList_ClearRenderTargetView(c->list, c->rtv,
                                                   clear_rgba, 0, NULL);
   ID3D12GraphicsCommandList_ClearDepthStencilView(
      c->list, c->dsv, D3D12_CLEAR_FLAG_DEPTH, clear_depth, 0, 0, NULL);
   ID3D12GraphicsCommandList_OMSetRenderTargets(c->list, 1, &c->rtv, FALSE,
                                                &c->dsv);

   D3D12_RECT sc = {.right = W, .bottom = H};
   ID3D12GraphicsCommandList_RSSetScissorRects(c->list, 1, &sc);
   ID3D12GraphicsCommandList_IASetPrimitiveTopology(
      c->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void
draw_at_depth(struct ctx *c, ID3D12PipelineState *pso, FLOAT depth)
{
   D3D12_VIEWPORT vp = {
      .Width = W,
      .Height = H,
      .MinDepth = depth,
      .MaxDepth = depth,
   };
   ID3D12GraphicsCommandList_RSSetViewports(c->list, 1, &vp);
   ID3D12GraphicsCommandList_SetPipelineState(c->list, pso);
   ID3D12GraphicsCommandList_DrawInstanced(c->list, 3, 1, 0, 0);
}

static bool
end_frame(struct ctx *c)
{
   if (FAILED(ID3D12GraphicsCommandList_Close(c->list)))
      return false;

   ID3D12CommandList *lists[] = {(ID3D12CommandList *)c->list};
   ID3D12CommandQueue_ExecuteCommandLists(c->queue, 1, lists);
   ID3D12CommandQueue_Signal(c->queue, c->fence, ++c->fence_value);
   ID3D12Fence_SetEventOnCompletion(c->fence, c->fence_value, NULL);
   return ID3D12Fence_GetCompletedValue(c->fence) >= c->fence_value;
}

/* A graphics pipeline differing only in its depth/stencil state. */
static ID3D12PipelineState *
make_pso(ID3D12Device *dev, const void *vs, size_t vs_size, const void *ps,
         size_t ps_size, BOOL depth_enable, D3D12_DEPTH_WRITE_MASK write,
         D3D12_COMPARISON_FUNC func, DXGI_FORMAT dsv_format)
{
   D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {
      .VS = {.pShaderBytecode = vs, .BytecodeLength = vs_size},
      .PS = {.pShaderBytecode = ps, .BytecodeLength = ps_size},
      .NumRenderTargets = 1,
      .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
      .DSVFormat = dsv_format,
      .DepthStencilState =
         {
            .DepthEnable = depth_enable,
            .DepthWriteMask = write,
            .DepthFunc = func,
         },
      .RasterizerState = {.CullMode = D3D12_CULL_MODE_NONE},
      .SampleDesc = {.Count = 1},
      .SampleMask = ~0u,
   };

   ID3D12PipelineState *pso = NULL;
   if (FAILED(ID3D12Device_CreateGraphicsPipelineState(
          dev, &pd, &IID_ID3D12PipelineState, (void **)&pso)))
      return NULL;
   return pso;
}

/* A pipeline that differs only in its rasterizer facing/culling state. */
static ID3D12PipelineState *
make_cull_pso(ID3D12Device *dev, const void *vs, size_t vs_size,
              const void *ps, size_t ps_size, D3D12_CULL_MODE cull,
              BOOL front_ccw)
{
   D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {
      .VS = {.pShaderBytecode = vs, .BytecodeLength = vs_size},
      .PS = {.pShaderBytecode = ps, .BytecodeLength = ps_size},
      .NumRenderTargets = 1,
      .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
      .RasterizerState =
         {
            .CullMode = cull,
            .FrontCounterClockwise = front_ccw,
         },
      .SampleDesc = {.Count = 1},
      .SampleMask = ~0u,
   };

   ID3D12PipelineState *pso = NULL;
   if (FAILED(ID3D12Device_CreateGraphicsPipelineState(
          dev, &pd, &IID_ID3D12PipelineState, (void **)&pso)))
      return NULL;
   return pso;
}

/* Draw one triangle with no depth buffer and report how many pixels the
 * rasterizer produced. Used to grade culling, where the question is simply
 * whether the primitive survived.
 */
static unsigned
count_covered(struct ctx *c, ID3D12PipelineState *pso)
{
   ID3D12GraphicsCommandList_Reset(c->list, c->alloc, NULL);
   ID3D12GraphicsCommandList_ClearRenderTargetView(c->list, c->rtv,
                                                   clear_rgba, 0, NULL);
   ID3D12GraphicsCommandList_OMSetRenderTargets(c->list, 1, &c->rtv, FALSE,
                                                NULL);
   D3D12_RECT sc = {.right = W, .bottom = H};
   ID3D12GraphicsCommandList_RSSetScissorRects(c->list, 1, &sc);
   D3D12_VIEWPORT vp = {.Width = W, .Height = H, .MaxDepth = 1.0f};
   ID3D12GraphicsCommandList_RSSetViewports(c->list, 1, &vp);
   ID3D12GraphicsCommandList_IASetPrimitiveTopology(
      c->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   ID3D12GraphicsCommandList_SetPipelineState(c->list, pso);
   ID3D12GraphicsCommandList_DrawInstanced(c->list, 3, 1, 0, 0);

   if (!end_frame(c))
      return ~0u;

   unsigned n = 0;
   for (unsigned y = 0; y < H; y++)
      for (unsigned x = 0; x < W; x++)
         if (memcmp(px(c->map, x, y), RED, 4))
            n++;
   return n;
}

int
main(int argc, char **argv)
{
   if (argc < 6) {
      fprintf(stderr,
              "usage: %s tri.dxil solid.dxil tri_depth.dxil solid_g.dxil "
              "tri_depth_cw.dxil\n",
              argv[0]);
      return 2;
   }

   size_t vs_size, ps_size, vsd_size, psg_size, vscw_size;
   void *vs = slurp(argv[1], &vs_size);
   void *ps = slurp(argv[2], &ps_size);
   void *vs_depth = slurp(argv[3], &vsd_size);
   void *ps_green = slurp(argv[4], &psg_size);
   void *vs_depth_cw = slurp(argv[5], &vscw_size);
   if (!vs || !ps || !vs_depth || !ps_green || !vs_depth_cw) {
      fprintf(stderr, "cannot read shaders\n");
      return 2;
   }

   struct ctx c = {0};
   HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                                  &IID_ID3D12Device, (void **)&c.dev);
   CHECK(hr == S_OK && c.dev, "D3D12CreateDevice");
   if (FAILED(hr))
      return 1;

   /* ---- Depth resources and DSV heap ---- */

   D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
   D3D12_RESOURCE_DESC rtd = {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = W,
      .Height = H,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
   };
   hr = ID3D12Device_CreateCommittedResource(
      c.dev, &heap, D3D12_HEAP_FLAG_NONE, &rtd,
      D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
      (void **)&c.rt);
   CHECK(hr == S_OK, "create RGBA8 render target");

   D3D12_RESOURCE_DESC dsd = {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = W,
      .Height = H,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_D32_FLOAT,
      .SampleDesc = {.Count = 1},
      .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
   };
   hr = ID3D12Device_CreateCommittedResource(
      c.dev, &heap, D3D12_HEAP_FLAG_NONE, &dsd,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource,
      (void **)&c.ds);
   CHECK(hr == S_OK && c.ds, "create D32_FLOAT depth buffer");
   if (FAILED(hr))
      return 1;

   /* A depth format with a stencil plane must also be creatable: it needs a
    * second allocation, since AGX does not interleave depth and stencil.
    */
   D3D12_RESOURCE_DESC d24 = dsd;
   d24.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
   ID3D12Resource *ds24 = NULL;
   hr = ID3D12Device_CreateCommittedResource(
      c.dev, &heap, D3D12_HEAP_FLAG_NONE, &d24,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource,
      (void **)&ds24);
   CHECK(hr == S_OK && ds24, "create D24_UNORM_S8_UINT (separate stencil "
                             "plane)");
   if (ds24)
      ID3D12Resource_Release(ds24);

   D3D12_DESCRIPTOR_HEAP_DESC rtv_hd = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .NumDescriptors = 1};
   ID3D12DescriptorHeap *rtv_heap = NULL;
   ID3D12Device_CreateDescriptorHeap(c.dev, &rtv_hd,
                                     &IID_ID3D12DescriptorHeap,
                                     (void **)&rtv_heap);

   D3D12_DESCRIPTOR_HEAP_DESC dsv_hd = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV, .NumDescriptors = 2};
   ID3D12DescriptorHeap *dsv_heap = NULL;
   hr = ID3D12Device_CreateDescriptorHeap(c.dev, &dsv_hd,
                                          &IID_ID3D12DescriptorHeap,
                                          (void **)&dsv_heap);
   CHECK(hr == S_OK && dsv_heap, "CreateDescriptorHeap(DSV)");
   if (FAILED(hr))
      return 1;

   c.rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);
   c.dsv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap);
   ID3D12Device_CreateRenderTargetView(c.dev, c.rt, NULL, c.rtv);
   ID3D12Device_CreateDepthStencilView(c.dev, c.ds, NULL, c.dsv);

   /* A colour texture is not a depth/stencil target. Accepting it here
    * would point the ZLS unit at an RGBA8 buffer and corrupt it.
    */
   UINT dsv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(
      c.dev, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
   D3D12_CPU_DESCRIPTOR_HANDLE bad_dsv = {.ptr = c.dsv.ptr + dsv_inc};
   ID3D12Device_CreateDepthStencilView(c.dev, c.rt, NULL, bad_dsv);

   ID3D12Resource_Map(c.rt, 0, NULL, (void **)&c.map);

   D3D12_COMMAND_QUEUE_DESC qd = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ID3D12Device_CreateCommandQueue(c.dev, &qd, &IID_ID3D12CommandQueue,
                                   (void **)&c.queue);
   ID3D12Device_CreateFence(c.dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                            (void **)&c.fence);
   ID3D12Device_CreateCommandAllocator(c.dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       &IID_ID3D12CommandAllocator,
                                       (void **)&c.alloc);
   ID3D12Device_CreateCommandList(c.dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  c.alloc, NULL,
                                  &IID_ID3D12GraphicsCommandList,
                                  (void **)&c.list);
   ID3D12GraphicsCommandList_Close(c.list);

   /* Clearing through the rejected DSV must fail rather than scribble on
    * the colour target.
    */
   ID3D12GraphicsCommandList_Reset(c.list, c.alloc, NULL);
   ID3D12GraphicsCommandList_ClearRenderTargetView(c.list, c.rtv, clear_rgba,
                                                   0, NULL);
   ID3D12GraphicsCommandList_ClearDepthStencilView(
      c.list, bad_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
   CHECK(FAILED(ID3D12GraphicsCommandList_Close(c.list)),
         "a colour texture is refused as a depth/stencil view");

   /* ---- Pipelines ---- */

   ID3D12PipelineState *pso_less =
      make_pso(c.dev, vs, vs_size, ps, ps_size, TRUE,
               D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS,
               DXGI_FORMAT_D32_FLOAT);
   ID3D12PipelineState *pso_greater =
      make_pso(c.dev, vs, vs_size, ps, ps_size, TRUE,
               D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_GREATER,
               DXGI_FORMAT_D32_FLOAT);
   /* First draw of the write-mask pair: always passes, colour GREEN, and
    * differs between the two runs ONLY in whether it writes depth.
    */
   ID3D12PipelineState *pso_write =
      make_pso(c.dev, vs, vs_size, ps_green, psg_size, TRUE,
               D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_ALWAYS,
               DXGI_FORMAT_D32_FLOAT);
   ID3D12PipelineState *pso_nowrite =
      make_pso(c.dev, vs, vs_size, ps_green, psg_size, TRUE,
               D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS,
               DXGI_FORMAT_D32_FLOAT);
   /* Depth requested, but the pass will have no depth attached. */
   ID3D12PipelineState *pso_nods =
      make_pso(c.dev, vs, vs_size, ps, ps_size, TRUE,
               D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS,
               DXGI_FORMAT_D32_FLOAT);
   /* Interpolated depth across the primitive, in both windings. */
   ID3D12PipelineState *pso_slope =
      make_pso(c.dev, vs_depth, vsd_size, ps_green, psg_size, TRUE,
               D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS,
               DXGI_FORMAT_D32_FLOAT);
   ID3D12PipelineState *pso_slope_cw =
      make_pso(c.dev, vs_depth_cw, vscw_size, ps_green, psg_size, TRUE,
               D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS,
               DXGI_FORMAT_D32_FLOAT);

   CHECK(pso_less && pso_greater && pso_write && pso_nowrite && pso_nods &&
            pso_slope && pso_slope_cw,
         "compile seven pipelines differing in depth state");
   if (!pso_less || !pso_greater || !pso_write || !pso_nowrite ||
       !pso_slope || !pso_slope_cw)
      return 1;

   printf("\n-- pair 1: same pipeline, depth 0.75 vs 0.125 against a 0.25 "
          "clear\n");

   begin_frame(&c, 0.25f);
   draw_at_depth(&c, pso_less, 0.75f);
   CHECK(end_frame(&c), "far draw executes");
   expect_pixel(c.map, IN_X, IN_Y,RED,
                "0.75 LESS 0.25 is false: the triangle is rejected");

   begin_frame(&c, 0.25f);
   draw_at_depth(&c, pso_less, 0.125f);
   CHECK(end_frame(&c), "near draw executes");
   expect_pixel(c.map, IN_X, IN_Y,BLUE,
                "0.125 LESS 0.25 is true: the triangle is drawn");

   printf("\n-- pair 2: identical values, LESS vs GREATER\n");

   begin_frame(&c, 0.5f);
   draw_at_depth(&c, pso_less, 0.125f);
   CHECK(end_frame(&c), "LESS draw executes");
   expect_pixel(c.map, IN_X, IN_Y,BLUE, "0.125 LESS 0.5 is true: drawn");

   begin_frame(&c, 0.5f);
   draw_at_depth(&c, pso_greater, 0.125f);
   CHECK(end_frame(&c), "GREATER draw executes");
   expect_pixel(c.map, IN_X, IN_Y,RED,
                "0.125 GREATER 0.5 is false: rejected (DepthFunc honoured)");

   printf("\n-- pair 3: DepthWriteMask ALL vs ZERO on the occluding draw\n");

   /* Two draws in ONE pass. The second one's fate depends only on what the
    * first left in the depth buffer, so this is where a driver that never
    * actually stores depth gets caught.
    */
   begin_frame(&c, 1.0f);
   draw_at_depth(&c, pso_write, 0.25f);   /* GREEN, writes depth 0.25 */
   draw_at_depth(&c, pso_less, 0.5f);     /* BLUE, 0.5 LESS 0.25 -> no */
   CHECK(end_frame(&c), "write-ALL frame executes");
   expect_pixel(c.map, IN_X, IN_Y,GREEN,
                "depth written by draw 1 occludes draw 2");

   begin_frame(&c, 1.0f);
   draw_at_depth(&c, pso_nowrite, 0.25f); /* GREEN, writes no depth */
   draw_at_depth(&c, pso_less, 0.5f);     /* BLUE, 0.5 LESS 1.0 -> yes */
   CHECK(end_frame(&c), "write-ZERO frame executes");
   expect_pixel(c.map, IN_X, IN_Y,BLUE,
                "with writes masked off, draw 2 is not occluded");

   printf("\n-- depth interpolated across the primitive\n");

   /* tri_depth.hlsl: apex (128,13) at z=0.1, base y=243 at z=0.9, so z
    * crosses the 0.5 clear at y=128. One primitive, one draw, and the
    * result must be cut in half. A per-draw depth cannot do this.
    */
   begin_frame(&c, 0.5f);
   {
      /* A full 0..1 depth range this time, so the shader's own Z reaches
       * the rasterizer instead of being pinned by the viewport.
       */
      D3D12_VIEWPORT vp = {.Width = W, .Height = H, .MaxDepth = 1.0f};
      ID3D12GraphicsCommandList_RSSetViewports(c.list, 1, &vp);
      ID3D12GraphicsCommandList_SetPipelineState(c.list, pso_slope);
      ID3D12GraphicsCommandList_DrawInstanced(c.list, 3, 1, 0, 0);
   }
   CHECK(end_frame(&c), "sloped-depth draw executes");
   expect_pixel(c.map, 128, 60, GREEN,
                "near end of the primitive (z~0.26) passes");
   expect_pixel(c.map, 128, 200, RED,
                "far end of the SAME primitive (z~0.75) is rejected");

   printf("\n-- the same triangle wound the other way round\n");

   /* AGX holds front-face and back-face depth/stencil state in separate
    * slots and picks per primitive. Writing only the front half leaves the
    * back half at whatever it was -- and all-zero decodes as compare NEVER,
    * so back-facing geometry would silently vanish. This is the same test
    * as above on the same pixels, with the winding reversed.
    */
   begin_frame(&c, 0.5f);
   {
      D3D12_VIEWPORT vp = {.Width = W, .Height = H, .MaxDepth = 1.0f};
      ID3D12GraphicsCommandList_RSSetViewports(c.list, 1, &vp);
      ID3D12GraphicsCommandList_SetPipelineState(c.list, pso_slope_cw);
      ID3D12GraphicsCommandList_DrawInstanced(c.list, 3, 1, 0, 0);
   }
   CHECK(end_frame(&c), "reversed-winding draw executes");
   expect_pixel(c.map, 128, 60, GREEN,
                "the other face gets the same depth state (near end passes)");
   expect_pixel(c.map, 128, 200, RED,
                "the other face gets the same depth state (far end fails)");

   printf("\n-- facing and culling\n");

   /* Which winding D3D12 calls "front" is a convention our viewport's Y
    * negation inverts relative to the hardware flag, so it is asserted
    * rather than assumed. tri_depth is counter-clockwise in NDC, which is
    * D3D12's FRONT face with the default FrontCounterClockwise = FALSE.
    */
   {
      ID3D12PipelineState *cull_none = make_cull_pso(
         c.dev, vs_depth, vsd_size, ps_green, psg_size,
         D3D12_CULL_MODE_NONE, FALSE);
      ID3D12PipelineState *cull_back = make_cull_pso(
         c.dev, vs_depth, vsd_size, ps_green, psg_size,
         D3D12_CULL_MODE_BACK, FALSE);
      ID3D12PipelineState *cull_front = make_cull_pso(
         c.dev, vs_depth, vsd_size, ps_green, psg_size,
         D3D12_CULL_MODE_FRONT, FALSE);
      /* Same pipeline as cull_front, but the opposite winding convention:
       * the triangle it culls must swap.
       */
      ID3D12PipelineState *cull_front_ccw = make_cull_pso(
         c.dev, vs_depth, vsd_size, ps_green, psg_size,
         D3D12_CULL_MODE_FRONT, TRUE);

      CHECK(cull_none && cull_back && cull_front && cull_front_ccw,
            "compile pipelines differing only in cull state");

      unsigned n_none = count_covered(&c, cull_none);
      unsigned n_back = count_covered(&c, cull_back);
      unsigned n_front = count_covered(&c, cull_front);
      unsigned n_front_ccw = count_covered(&c, cull_front_ccw);

      printf("       covered: none=%u cullback=%u cullfront=%u "
             "cullfront+FCC=%u\n",
             n_none, n_back, n_front, n_front_ccw);

      CHECK(n_none > 0, "CULL_MODE_NONE draws the triangle");
      CHECK(n_back == n_none,
            "CULL_MODE_BACK keeps a front-facing triangle");
      CHECK(n_front == 0, "CULL_MODE_FRONT removes it (facing is honoured)");
      CHECK(n_front_ccw == n_none,
            "FrontCounterClockwise flips which face is culled");

      ID3D12PipelineState_Release(cull_front_ccw);
      ID3D12PipelineState_Release(cull_front);
      ID3D12PipelineState_Release(cull_back);
      ID3D12PipelineState_Release(cull_none);
   }

   printf("\n-- a depth-testing pipeline in a pass with no depth buffer\n");

   /* No DSV bound. D3D12 says the test is inert; encoding the comparison
    * anyway would test against whatever the ZLS unit last pointed at.
    */
   ID3D12GraphicsCommandList_Reset(c.list, c.alloc, NULL);
   ID3D12GraphicsCommandList_ClearRenderTargetView(c.list, c.rtv, clear_rgba,
                                                   0, NULL);
   ID3D12GraphicsCommandList_OMSetRenderTargets(c.list, 1, &c.rtv, FALSE,
                                                NULL);
   {
      D3D12_RECT sc = {.right = W, .bottom = H};
      ID3D12GraphicsCommandList_RSSetScissorRects(c.list, 1, &sc);
      ID3D12GraphicsCommandList_IASetPrimitiveTopology(
         c.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   }
   draw_at_depth(&c, pso_nods, 0.75f);
   CHECK(end_frame(&c), "no-depth frame executes");
   expect_pixel(c.map, IN_X, IN_Y,BLUE,
                "depth test is inert with no depth buffer, not NEVER");

   ID3D12Resource_Unmap(c.rt, 0, NULL);
   ID3D12PipelineState_Release(pso_slope_cw);
   ID3D12PipelineState_Release(pso_slope);
   ID3D12PipelineState_Release(pso_nods);
   ID3D12PipelineState_Release(pso_nowrite);
   ID3D12PipelineState_Release(pso_write);
   ID3D12PipelineState_Release(pso_greater);
   ID3D12PipelineState_Release(pso_less);
   ID3D12GraphicsCommandList_Release(c.list);
   ID3D12CommandAllocator_Release(c.alloc);
   ID3D12Fence_Release(c.fence);
   ID3D12CommandQueue_Release(c.queue);
   ID3D12DescriptorHeap_Release(dsv_heap);
   ID3D12DescriptorHeap_Release(rtv_heap);
   ID3D12Resource_Release(c.ds);
   ID3D12Resource_Release(c.rt);
   ID3D12Device_Release(c.dev);
   free(vs);
   free(ps);
   free(vs_depth);
   free(ps_green);
   free(vs_depth_cw);

   printf(failures ? "\nG5 DEPTH: %d FAILURES\n" : "\nG5 DEPTH: ALL PASS\n",
          failures);
   return failures;
}
