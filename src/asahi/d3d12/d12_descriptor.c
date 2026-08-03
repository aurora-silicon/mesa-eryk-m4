/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * ID3D12DescriptorHeap. G3 scope: RTV heaps only — CPU-side slot storage
 * whose handles are pointers into the slot array, with the slot size equal
 * to the advertised 32-byte handle increment.
 */

#include "d12_private.h"

static struct d12_descriptor_heap *
d12_heap_from_iface(ID3D12DescriptorHeap *iface)
{
   return (struct d12_descriptor_heap *)iface;
}

static HRESULT STDMETHODCALLTYPE
d12_heap_QueryInterface(ID3D12DescriptorHeap *iface, REFIID riid, void **out)
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12Pageable) ||
       d12_guid_equal(riid, &IID_ID3D12DescriptorHeap)) {
      p_atomic_inc(&heap->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12DescriptorHeap::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_heap_AddRef(ID3D12DescriptorHeap *iface)
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);
   return p_atomic_inc_return(&heap->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_heap_Release(ID3D12DescriptorHeap *iface)
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&heap->refcount);

   if (refs == 0) {
      free(heap->slots);
      d12_device_release(heap->device);
      free(heap);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_heap_GetPrivateData(ID3D12DescriptorHeap *iface, REFGUID guid,
                        UINT *size, void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_heap_SetPrivateData(ID3D12DescriptorHeap *iface, REFGUID guid, UINT size,
                        const void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_heap_SetPrivateDataInterface(ID3D12DescriptorHeap *iface, REFGUID guid,
                                 const IUnknown *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_heap_SetName(ID3D12DescriptorHeap *iface, LPCWSTR name)
{
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_heap_GetDevice(ID3D12DescriptorHeap *iface, REFIID riid, void **out)
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);
   return ID3D12Device_QueryInterface(&heap->device->iface, riid, out);
}

static D3D12_DESCRIPTOR_HEAP_DESC STDMETHODCALLTYPE
d12_heap_GetDesc(ID3D12DescriptorHeap *iface)
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);
   return heap->desc;
}

static D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE
d12_heap_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap *iface)
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);
   D3D12_CPU_DESCRIPTOR_HANDLE handle = {.ptr = (SIZE_T)heap->slots};
   return handle;
}

static D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE
d12_heap_GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap *iface)
{
   /* No shader-visible heaps until the G4 binding model. */
   D3D12_GPU_DESCRIPTOR_HANDLE handle = {.ptr = 0};
   return handle;
}

static const ID3D12DescriptorHeapVtbl d12_heap_vtbl = {
   .QueryInterface = d12_heap_QueryInterface,
   .AddRef = d12_heap_AddRef,
   .Release = d12_heap_Release,
   .GetPrivateData = d12_heap_GetPrivateData,
   .SetPrivateData = d12_heap_SetPrivateData,
   .SetPrivateDataInterface = d12_heap_SetPrivateDataInterface,
   .SetName = d12_heap_SetName,
   .GetDevice = d12_heap_GetDevice,
   .GetDesc = d12_heap_GetDesc,
   .GetCPUDescriptorHandleForHeapStart =
      d12_heap_GetCPUDescriptorHandleForHeapStart,
   .GetGPUDescriptorHandleForHeapStart =
      d12_heap_GetGPUDescriptorHandleForHeapStart,
};

HRESULT
d12_descriptor_heap_create(struct d12_device *device,
                           const D3D12_DESCRIPTOR_HEAP_DESC *desc,
                           REFIID riid, void **out)
{
   if (!out)
      return E_POINTER;
   if (!desc || !desc->NumDescriptors)
      return E_INVALIDARG;

   if (desc->Type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
      mesa_loge("CreateDescriptorHeap: type %d not until G4 (RTV only)",
                (int)desc->Type);
      return E_NOTIMPL;
   }

   if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
      mesa_loge("CreateDescriptorHeap: RTV heaps cannot be shader-visible");
      return E_INVALIDARG;
   }

   struct d12_descriptor_heap *heap = calloc(1, sizeof(*heap));
   if (!heap)
      return E_OUTOFMEMORY;

   heap->slots = calloc(desc->NumDescriptors, sizeof(struct d12_rtv));
   if (!heap->slots) {
      free(heap);
      return E_OUTOFMEMORY;
   }

   heap->iface.lpVtbl = (ID3D12DescriptorHeapVtbl *)&d12_heap_vtbl;
   heap->refcount = 1;
   heap->device = device;
   heap->desc = *desc;
   d12_device_add_ref(device);

   HRESULT hr = d12_heap_QueryInterface(&heap->iface, riid, out);
   d12_heap_Release(&heap->iface);
   return hr;
}
