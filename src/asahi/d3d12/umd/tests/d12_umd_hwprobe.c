/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * End-to-end adapter discovery probe for native ARM64 Windows.  It enumerates
 * real dxgkrnl adapters, lets OpenAdapter12 obtain UMDRIVERPRIVATE through the
 * normal adapter callback shape, and verifies the AGX identity/caps path.
 */

#define D3D_UMD_INTERFACE_VERSION 0x5002
#include <windows.h>
#include <d3d12umddi.h>

#include "../d12_umd_abi.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct probe_runtime_adapter {
   D3DKMT_HANDLE kmt_adapter;
   struct agxw_adapter_identity identity;
};

static HRESULT APIENTRY
query_adapter_info(HANDLE runtime_adapter,
                   const D3DDDICB_QUERYADAPTERINFO *query)
{
   struct probe_runtime_adapter *adapter =
      (struct probe_runtime_adapter *)runtime_adapter;
   D3DKMT_QUERYADAPTERINFO kmt_query;
   NTSTATUS status;

   if (!adapter || !query || !query->pPrivateDriverData ||
       query->PrivateDriverDataSize != sizeof(adapter->identity))
      return E_INVALIDARG;

   kmt_query = (D3DKMT_QUERYADAPTERINFO){
      .hAdapter = adapter->kmt_adapter,
      .Type = KMTQAITYPE_UMDRIVERPRIVATE,
      .pPrivateDriverData = query->pPrivateDriverData,
      .PrivateDriverDataSize = query->PrivateDriverDataSize,
   };
   status = D3DKMTQueryAdapterInfo(&kmt_query);
   return status < 0 ? HRESULT_FROM_NT(status) : S_OK;
}

static PFND3D12DDI_OPENADAPTER
load_open_adapter(HMODULE module)
{
   FARPROC symbol = GetProcAddress(module, "OpenAdapter12");
   PFND3D12DDI_OPENADAPTER function = NULL;
   if (symbol)
      memcpy(&function, &symbol, sizeof(function));
   return function;
}

int
wmain(int argc, wchar_t **argv)
{
   const wchar_t *dll_path = argc > 1 ? argv[1] : L"d3d12agx_umd.dll";
   D3DKMT_ENUMADAPTERS2 enumeration = {0};
   D3DKMT_ADAPTERINFO *adapters = NULL;
   HMODULE module = NULL;
   PFND3D12DDI_OPENADAPTER open_adapter;
   NTSTATUS status;
   int result = 2;
   unsigned matches = 0;

   module = LoadLibraryW(dll_path);
   if (!module) {
      fprintf(stderr, "FAIL stage=load win32=%lu\n",
              (unsigned long)GetLastError());
      return 2;
   }
   open_adapter = load_open_adapter(module);
   if (!open_adapter) {
      fprintf(stderr, "FAIL stage=resolve win32=%lu\n",
              (unsigned long)GetLastError());
      goto cleanup;
   }

   status = D3DKMTEnumAdapters2(&enumeration);
   if (status < 0 || !enumeration.NumAdapters) {
      fprintf(stderr, "FAIL stage=enum_count status=0x%08" PRIx32
                      " count=%lu\n",
              (uint32_t)status, (unsigned long)enumeration.NumAdapters);
      goto cleanup;
   }

   adapters = calloc(enumeration.NumAdapters, sizeof(*adapters));
   if (!adapters) {
      fprintf(stderr, "FAIL stage=enum_alloc\n");
      goto cleanup;
   }
   enumeration.pAdapters = adapters;
   status = D3DKMTEnumAdapters2(&enumeration);
   if (status < 0) {
      fprintf(stderr, "FAIL stage=enum_fill status=0x%08" PRIx32 "\n",
              (uint32_t)status);
      goto cleanup;
   }

   for (ULONG i = 0; i < enumeration.NumAdapters; ++i) {
      const D3DDDI_ADAPTERCALLBACKS callbacks = {
         .pfnQueryAdapterInfoCb = query_adapter_info,
      };
      struct probe_runtime_adapter runtime_adapter = {
         .kmt_adapter = adapters[i].hAdapter,
      };
      D3D12DDI_ADAPTERFUNCS functions = {0};
      D3D12DDIARG_OPENADAPTER arguments = {
         .hRTAdapter = {.handle = &runtime_adapter},
         .pAdapterCallbacks = &callbacks,
         .pAdapterFuncs = &functions,
      };
      HRESULT open_result = open_adapter(&arguments);

      if (open_result == S_OK) {
         D3DKMT_QUERYADAPTERINFO identity_query = {
            .hAdapter = adapters[i].hAdapter,
            .Type = KMTQAITYPE_UMDRIVERPRIVATE,
            .pPrivateDriverData = &runtime_adapter.identity,
            .PrivateDriverDataSize = sizeof(runtime_adapter.identity),
         };
         UINT32 version_count = 3;
         UINT64 versions[3] = {0};

         status = D3DKMTQueryAdapterInfo(&identity_query);
         if (status < 0 ||
             d12_umd_validate_identity(&runtime_adapter.identity,
                                       sizeof(runtime_adapter.identity),
                                       &runtime_adapter.identity) !=
                D12_UMD_IDENTITY_OK) {
            fprintf(stderr, "FAIL stage=identity index=%lu\n",
                    (unsigned long)i);
            functions.pfnCloseAdapter(arguments.hAdapter);
            goto cleanup;
         }

         open_result = functions.pfnGetSupportedVersions(
            arguments.hAdapter, &version_count, versions);
         if (open_result != S_OK || version_count != ARRAYSIZE(versions) ||
             versions[0] != D3D12DDI_SUPPORTED_0011 ||
             versions[1] != D3D12DDI_SUPPORTED_0010 ||
             versions[2] != D3D12DDI_SUPPORTED_0003) {
            fprintf(stderr, "FAIL stage=versions index=%lu hr=0x%08" PRIx32
                            "\n",
                    (unsigned long)i, (uint32_t)open_result);
            functions.pfnCloseAdapter(arguments.hAdapter);
            goto cleanup;
         }

         printf("PASS stage=open_adapter12 index=%lu luid=%08" PRIx32
                ":%08" PRIx32 " chip=0x%04" PRIx32
                " generation=%" PRIu32 " variant=%c revision=0x%02" PRIx32
                " ddi=0x%016" PRIx64 "\n",
                (unsigned long)i,
                (uint32_t)adapters[i].AdapterLuid.HighPart,
                (uint32_t)adapters[i].AdapterLuid.LowPart,
                runtime_adapter.identity.chip_id,
                runtime_adapter.identity.gpu_generation,
                (char)runtime_adapter.identity.gpu_variant,
                runtime_adapter.identity.gpu_revision, versions[0]);
         functions.pfnCloseAdapter(arguments.hAdapter);
         ++matches;
      } else if (open_result != DXGI_ERROR_UNSUPPORTED) {
         fprintf(stderr,
                 "FAIL stage=open_adapter12 index=%lu hr=0x%08" PRIx32 "\n",
                 (unsigned long)i, (uint32_t)open_result);
         goto cleanup;
      }
   }

   if (matches != 1) {
      fprintf(stderr, "FAIL stage=match_count matches=%u\n", matches);
      goto cleanup;
   }

   result = 0;

cleanup:
   if (adapters) {
      for (ULONG i = 0; i < enumeration.NumAdapters; ++i) {
         if (adapters[i].hAdapter) {
            D3DKMT_CLOSEADAPTER close_arguments = {
               .hAdapter = adapters[i].hAdapter,
            };
            NTSTATUS close_status = D3DKMTCloseAdapter(&close_arguments);
            if (close_status < 0) {
               fprintf(stderr,
                       "FAIL stage=close_adapter index=%lu status=0x%08"
                       PRIx32 "\n",
                       (unsigned long)i, (uint32_t)close_status);
               result = 2;
            }
         }
      }
      free(adapters);
   }
   if (module)
      FreeLibrary(module);
   return result;
}
