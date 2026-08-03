/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * d3d12agx descriptor-table acceptance.
 *
 * res_probe.hlsl declares its three resources at registers chosen so that
 * nothing lines up by accident:
 *
 *   StructuredBuffer<Wide>   src : register(t2)   // 12-byte elements
 *   RWStructuredBuffer<uint> dst : register(u3)
 *   cbuffer Params           : register(b1)       // { scale, bias }
 *
 *   dst[i] = src[i].a * scale + src[i].c + bias
 *
 * Every number in that declaration is a trap for a plausible shortcut:
 *
 *   - t2, u3 and b1 are three different registers in three different
 *     classes. A driver indexing one flat table by register number puts
 *     all of them somewhere wrong.
 *   - The descriptor table's ranges are bound by root PARAMETER index,
 *     which is 0, while the registers are 2, 3 and 1. Only the root
 *     signature connects them.
 *   - The structured element is 12 bytes, and the shader reads fields at
 *     offsets 0 and 8. With an assumed 4-byte stride every read lands in
 *     a different element and the results are wrong but in bounds.
 *
 * The expected values are computed on the CPU from the same inputs, so a
 * pass means the GPU agreed element by element, not that it wrote
 * something.
 *
 * Usage: g5_descriptors res_probe.dxil rs1.rs rs2.rs rs3.rs space_probe.dxil
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

#define N 256

/* Must match res_probe.hlsl's `struct Wide { uint a, b, c; }`. */
struct wide {
   uint32_t a, b, c;
};

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

static ID3D12Resource *
make_buffer(ID3D12Device *dev, UINT64 size)
{
   D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_UPLOAD};
   D3D12_RESOURCE_DESC desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Width = size,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .SampleDesc = {.Count = 1},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
   };

   ID3D12Resource *res = NULL;
   if (FAILED(ID3D12Device_CreateCommittedResource(
          dev, &heap, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
          (void **)&res)))
      return NULL;
   return res;
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      fprintf(stderr, "usage: %s res_probe.dxil [rootsig.rs ...]\n", argv[0]);
      return 2;
   }

   size_t cs_size;
   void *cs = slurp(argv[1], &cs_size);
   if (!cs) {
      fprintf(stderr, "cannot read %s\n", argv[1]);
      return 2;
   }

   ID3D12Device *dev = NULL;
   HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                                  &IID_ID3D12Device, (void **)&dev);
   CHECK(hr == S_OK && dev, "D3D12CreateDevice");
   if (FAILED(hr))
      return 1;

   /* ---- The root signature: ONE table covering three register ranges ---- */

   D3D12_DESCRIPTOR_RANGE ranges[] = {
      {
         .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
         .NumDescriptors = 1,
         .BaseShaderRegister = 2, /* t2 */
         .OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
      },
      {
         .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
         .NumDescriptors = 1,
         .BaseShaderRegister = 3, /* u3 */
         .OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
      },
      {
         .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
         .NumDescriptors = 1,
         .BaseShaderRegister = 1, /* b1 */
         .OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
      },
   };

   D3D12_ROOT_PARAMETER param = {
      .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
      .DescriptorTable = {.NumDescriptorRanges = 3,
                          .pDescriptorRanges = ranges},
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
   };
   D3D12_ROOT_SIGNATURE_DESC rsd = {.NumParameters = 1, .pParameters = &param};

   ID3DBlob *rs_blob = NULL;
   hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                    &rs_blob, NULL);
   CHECK(hr == S_OK && rs_blob, "serialize a descriptor-table root signature");
   if (FAILED(hr))
      return 1;

   ID3D12RootSignature *root_sig = NULL;
   hr = ID3D12Device_CreateRootSignature(
      dev, 0, ID3D10Blob_GetBufferPointer(rs_blob),
      ID3D10Blob_GetBufferSize(rs_blob), &IID_ID3D12RootSignature,
      (void **)&root_sig);
   CHECK(hr == S_OK && root_sig, "parse it back (round trip)");
   ID3D10Blob_Release(rs_blob);
   if (FAILED(hr))
      return 1;

   /* Blobs produced by Microsoft's own compiler must parse too: agreeing
    * with our serializer proves only that we are self-consistent.
    */
   const int last_blob = argc > 5 ? argc - 1 : argc;
   for (int i = 2; i < last_blob; i++) {
      size_t blob_size;
      void *blob = slurp(argv[i], &blob_size);
      if (!blob) {
         printf("FAIL - cannot read %s\n", argv[i]);
         failures++;
         continue;
      }

      ID3D12RootSignature *rs = NULL;
      hr = ID3D12Device_CreateRootSignature(dev, 0, blob, blob_size,
                                            &IID_ID3D12RootSignature,
                                            (void **)&rs);
      char what[160];
      snprintf(what, sizeof(what), "parse a dxc-produced blob (%s)",
               strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i]);
      CHECK(hr == S_OK && rs, what);
      if (rs)
         ID3D12RootSignature_Release(rs);

      /* Truncating it must be rejected, not read past the end. */
      if (blob_size > 40) {
         ID3D12RootSignature *bad = NULL;
         hr = ID3D12Device_CreateRootSignature(dev, 0, blob, blob_size / 2,
                                               &IID_ID3D12RootSignature,
                                               (void **)&bad);
         CHECK(FAILED(hr) && !bad, "a truncated blob is refused");
         if (bad)
            ID3D12RootSignature_Release(bad);
      }

      free(blob);
   }

   /* ---- Resources and their descriptors ---- */

   ID3D12Resource *src = make_buffer(dev, sizeof(struct wide) * N);
   ID3D12Resource *dst = make_buffer(dev, sizeof(uint32_t) * N);
   ID3D12Resource *cb = make_buffer(dev, 256);
   CHECK(src && dst && cb, "create source, destination and constant buffers");
   if (!src || !dst || !cb)
      return 1;

   struct wide *src_map = NULL;
   uint32_t *dst_map = NULL, *cb_map = NULL;
   ID3D12Resource_Map(src, 0, NULL, (void **)&src_map);
   ID3D12Resource_Map(dst, 0, NULL, (void **)&dst_map);
   ID3D12Resource_Map(cb, 0, NULL, (void **)&cb_map);

   for (unsigned i = 0; i < N; i++) {
      src_map[i].a = i * 7 + 1;
      src_map[i].b = 0xDEADBEEF; /* never read: catches a wrong field offset */
      src_map[i].c = i * 13 + 5;
   }
   memset(dst_map, 0xAB, sizeof(uint32_t) * N);

   const uint32_t scale = 3, bias = 11;
   cb_map[0] = scale;
   cb_map[1] = bias;

   D3D12_DESCRIPTOR_HEAP_DESC hd = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = 3,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
   };
   ID3D12DescriptorHeap *heap = NULL;
   hr = ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap,
                                          (void **)&heap);
   CHECK(hr == S_OK && heap, "create a shader-visible CBV/SRV/UAV heap");
   if (FAILED(hr))
      return 1;

   UINT inc = ID3D12Device_GetDescriptorHandleIncrementSize(
      dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   D3D12_CPU_DESCRIPTOR_HANDLE cpu =
      ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
   D3D12_GPU_DESCRIPTOR_HANDLE gpu =
      ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
   CHECK(inc && cpu.ptr && gpu.ptr, "heap start handles are usable");

   /* Table slot 0: the SRV. Its 12-byte stride is the shader's, not four. */
   D3D12_SHADER_RESOURCE_VIEW_DESC srv = {
      .Format = DXGI_FORMAT_UNKNOWN,
      .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
      .Buffer = {.FirstElement = 0,
                 .NumElements = N,
                 .StructureByteStride = sizeof(struct wide)},
   };
   ID3D12Device_CreateShaderResourceView(dev, src, &srv, cpu);

   /* Table slot 1: the UAV. */
   D3D12_CPU_DESCRIPTOR_HANDLE cpu1 = {.ptr = cpu.ptr + inc};
   D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {
      .Format = DXGI_FORMAT_UNKNOWN,
      .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
      .Buffer = {.FirstElement = 0,
                 .NumElements = N,
                 .StructureByteStride = sizeof(uint32_t)},
   };
   ID3D12Device_CreateUnorderedAccessView(dev, dst, NULL, &uav, cpu1);

   /* Table slot 2: the CBV. */
   D3D12_CPU_DESCRIPTOR_HANDLE cpu2 = {.ptr = cpu.ptr + 2 * inc};
   D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {
      .BufferLocation = ID3D12Resource_GetGPUVirtualAddress(cb),
      .SizeInBytes = 256,
   };
   ID3D12Device_CreateConstantBufferView(dev, &cbv, cpu2);

   /* A texture SRV has no descriptor we can honestly write yet, so it must
    * be refused rather than recorded as if it were a linear buffer.
    */
   {
      D3D12_HEAP_PROPERTIES dh = {.Type = D3D12_HEAP_TYPE_DEFAULT};
      D3D12_RESOURCE_DESC td = {
         .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
         .Width = 16,
         .Height = 16,
         .DepthOrArraySize = 1,
         .MipLevels = 1,
         .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
         .SampleDesc = {.Count = 1},
      };
      ID3D12Resource *tex = NULL;
      ID3D12Device_CreateCommittedResource(
         dev, &dh, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COMMON,
         NULL, &IID_ID3D12Resource, (void **)&tex);

      if (tex) {
         D3D12_SHADER_RESOURCE_VIEW_DESC tsrv = {
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping =
               D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = {.MipLevels = 1},
         };
         /* Write into an unused fourth slot's worth of scratch. */
         struct { uint8_t bytes[32]; } scratch = {0};
         D3D12_CPU_DESCRIPTOR_HANDLE h = {.ptr = (SIZE_T)&scratch};
         ID3D12Device_CreateShaderResourceView(dev, tex, &tsrv, h);

         bool empty = true;
         for (unsigned i = 0; i < sizeof(scratch); i++)
            empty &= scratch.bytes[i] == 0;
         CHECK(empty, "a texture SRV is refused, not recorded as a buffer");
         ID3D12Resource_Release(tex);
      }
   }

   /* ---- Pipeline and dispatch ---- */

   D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {
      .pRootSignature = root_sig,
      .CS = {.pShaderBytecode = cs, .BytecodeLength = cs_size},
   };
   ID3D12PipelineState *pso = NULL;
   hr = ID3D12Device_CreateComputePipelineState(dev, &pd,
                                                &IID_ID3D12PipelineState,
                                                (void **)&pso);
   CHECK(hr == S_OK && pso, "compile a shader using t2, u3 and b1");
   if (FAILED(hr))
      return 1;

   ID3D12CommandAllocator *alloc = NULL;
   ID3D12GraphicsCommandList *list = NULL;
   ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                       &IID_ID3D12CommandAllocator,
                                       (void **)&alloc);
   ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                  alloc, NULL,
                                  &IID_ID3D12GraphicsCommandList,
                                  (void **)&list);

   ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
   ID3D12GraphicsCommandList_SetComputeRootSignature(list, root_sig);
   ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
   ID3D12GraphicsCommandList_SetPipelineState(list, pso);
   ID3D12GraphicsCommandList_Dispatch(list, N / 64, 1, 1);
   hr = ID3D12GraphicsCommandList_Close(list);
   CHECK(hr == S_OK, "record a dispatch bound through a descriptor table");
   if (FAILED(hr))
      return 1;

   ID3D12CommandQueue *queue = NULL;
   D3D12_COMMAND_QUEUE_DESC qd = {.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE};
   ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue,
                                   (void **)&queue);
   ID3D12Fence *fence = NULL;
   ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                            (void **)&fence);

   ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
   ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
   ID3D12CommandQueue_Signal(queue, fence, 1);
   ID3D12Fence_SetEventOnCompletion(fence, 1, NULL);
   CHECK(ID3D12Fence_GetCompletedValue(fence) >= 1, "GPU dispatch complete");

   /* ---- Grade against the same arithmetic on the CPU ---- */

   unsigned wrong = 0;
   for (unsigned i = 0; i < N; i++) {
      uint32_t want = src_map[i].a * scale + src_map[i].c + bias;
      if (dst_map[i] != want) {
         if (wrong < 4) {
            printf("       [%u] got %u, want %u\n", i, dst_map[i], want);
         }
         wrong++;
      }
   }

   CHECK(wrong == 0,
         "all 256 elements match: t2/u3/b1 reached through one table, "
         "12-byte stride honoured");
   if (wrong)
      printf("       %u of %u elements wrong\n", wrong, N);
   else
      printf("       spot check: [0]=%u [1]=%u [255]=%u\n", dst_map[0],
             dst_map[1], dst_map[255]);

   /* ---- Register spaces ----
    *
    * The same register number in different spaces names different
    * resources. Slots are assigned densely from the root signature rather
    * than by a formula over (class, register), so this costs nothing and,
    * more to the point, cannot collide.
    */
   if (argc > 2 + 3) {
      size_t sp_size;
      void *sp_cs = slurp(argv[argc - 1], &sp_size);
      if (sp_cs) {
         D3D12_DESCRIPTOR_RANGE sp_ranges[] = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 2,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 5,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 3,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
         };
         D3D12_ROOT_PARAMETER sp_param = {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable = {.NumDescriptorRanges = 4,
                                .pDescriptorRanges = sp_ranges},
         };
         D3D12_ROOT_SIGNATURE_DESC sp_rsd = {.NumParameters = 1,
                                             .pParameters = &sp_param};

         ID3DBlob *sb = NULL;
         D3D12SerializeRootSignature(&sp_rsd, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                     &sb, NULL);
         ID3D12RootSignature *sp_rs = NULL;
         ID3D12Device_CreateRootSignature(
            dev, 0, ID3D10Blob_GetBufferPointer(sb),
            ID3D10Blob_GetBufferSize(sb), &IID_ID3D12RootSignature,
            (void **)&sp_rs);
         ID3D10Blob_Release(sb);

         D3D12_COMPUTE_PIPELINE_STATE_DESC sp_pd = {
            .pRootSignature = sp_rs,
            .CS = {.pShaderBytecode = sp_cs, .BytecodeLength = sp_size},
         };
         ID3D12PipelineState *sp_pso = NULL;
         hr = ID3D12Device_CreateComputePipelineState(
            dev, &sp_pd, &IID_ID3D12PipelineState, (void **)&sp_pso);
         CHECK(hr == S_OK && sp_pso,
               "compile t0/space0, t0/space2, t0/space5 and u0/space3 "
               "without collision");

         /* The same shader against a signature that only covers space 0
          * must be refused: those bindings genuinely are not there.
          */
         D3D12_DESCRIPTOR_RANGE only0[] = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
         };
         D3D12_ROOT_PARAMETER only0_param = {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable = {.NumDescriptorRanges = 2,
                                .pDescriptorRanges = only0},
         };
         D3D12_ROOT_SIGNATURE_DESC only0_rsd = {.NumParameters = 1,
                                                .pParameters = &only0_param};
         ID3DBlob *ob = NULL;
         D3D12SerializeRootSignature(&only0_rsd,
                                     D3D_ROOT_SIGNATURE_VERSION_1_0, &ob,
                                     NULL);
         ID3D12RootSignature *only0_rs = NULL;
         ID3D12Device_CreateRootSignature(
            dev, 0, ID3D10Blob_GetBufferPointer(ob),
            ID3D10Blob_GetBufferSize(ob), &IID_ID3D12RootSignature,
            (void **)&only0_rs);
         ID3D10Blob_Release(ob);

         D3D12_COMPUTE_PIPELINE_STATE_DESC bad_pd = {
            .pRootSignature = only0_rs,
            .CS = {.pShaderBytecode = sp_cs, .BytecodeLength = sp_size},
         };
         ID3D12PipelineState *bad_pso = NULL;
         hr = ID3D12Device_CreateComputePipelineState(
            dev, &bad_pd, &IID_ID3D12PipelineState, (void **)&bad_pso);
         CHECK(FAILED(hr) && !bad_pso,
               "a shader using a space the signature omits is refused");
         if (bad_pso)
            ID3D12PipelineState_Release(bad_pso);

         if (sp_pso)
            ID3D12PipelineState_Release(sp_pso);
         if (sp_rs)
            ID3D12RootSignature_Release(sp_rs);
         if (only0_rs)
            ID3D12RootSignature_Release(only0_rs);
         free(sp_cs);
      }
   }

   /* Binding a table against a root parameter that is not one must fail
    * rather than write addresses into arbitrary slots.
    */
   {
      D3D12_ROOT_PARAMETER uav_param = {
         .ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV,
         .Descriptor = {.ShaderRegister = 0},
      };
      D3D12_ROOT_SIGNATURE_DESC bad_rsd = {.NumParameters = 1,
                                           .pParameters = &uav_param};
      ID3DBlob *b = NULL;
      D3D12SerializeRootSignature(&bad_rsd, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                  &b, NULL);
      ID3D12RootSignature *bad_rs = NULL;
      ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(b),
                                       ID3D10Blob_GetBufferSize(b),
                                       &IID_ID3D12RootSignature,
                                       (void **)&bad_rs);
      ID3D10Blob_Release(b);

      ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
      ID3D12GraphicsCommandList_SetComputeRootSignature(list, bad_rs);
      ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
      CHECK(FAILED(ID3D12GraphicsCommandList_Close(list)),
            "binding a table to a root descriptor parameter is refused");
      ID3D12RootSignature_Release(bad_rs);
   }

   ID3D12Resource_Unmap(src, 0, NULL);
   ID3D12Resource_Unmap(dst, 0, NULL);
   ID3D12Resource_Unmap(cb, 0, NULL);
   ID3D12Fence_Release(fence);
   ID3D12CommandQueue_Release(queue);
   ID3D12GraphicsCommandList_Release(list);
   ID3D12CommandAllocator_Release(alloc);
   ID3D12PipelineState_Release(pso);
   ID3D12DescriptorHeap_Release(heap);
   ID3D12Resource_Release(cb);
   ID3D12Resource_Release(dst);
   ID3D12Resource_Release(src);
   ID3D12RootSignature_Release(root_sig);
   CHECK(ID3D12Device_Release(dev) == 0, "device refcount reaches 0");
   free(cs);

   printf(failures ? "\nG5 DESCRIPTORS: %d FAILURES\n"
                   : "\nG5 DESCRIPTORS: ALL PASS\n",
          failures);
   return failures;
}
