/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12DescriptorHeap. Slot storage whose handles are pointers into the slot
 * array, with the slot size equal to the advertised 40-byte handle increment.
 * Shader-visible heaps use coherent GPU BOs: finite ranges are snapshotted
 * into root bindings, while unbounded ranges dereference the slots directly.
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

   if (d12_private_data_query_notifier(&heap->private_data, riid, out))
      return S_OK;

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
      simple_mtx_lock(&heap->device->descriptor_heap_lock);
      struct d12_descriptor_heap **link = &heap->device->descriptor_heaps;
      while (*link && *link != heap)
         link = &(*link)->registry_next;
      if (*link == heap)
         *link = heap->registry_next;
      simple_mtx_unlock(&heap->device->descriptor_heap_lock);

      if (heap->texture_bo)
         agx_bo_unreference(&heap->device->dev, heap->texture_bo);
      if (heap->pbe_bo)
         agx_bo_unreference(&heap->device->dev, heap->pbe_bo);
      if (heap->sampler_bo)
         agx_bo_unreference(&heap->device->dev, heap->sampler_bo);
      if (heap->bo)
         agx_bo_unreference(&heap->device->dev, heap->bo);
      else
         free(heap->slots);
      d12_private_data_store_destroy(&heap->private_data);
      d12_device_release(heap->device);
      free(heap);
   }

   return refs;
}

static void
d12_descriptor_heap_rebuild_samplers(struct d12_descriptor_heap *heap)
{
   if (!heap->sampler_bo)
      return;

   struct agx_sampler_packed *native = agx_bo_map(heap->sampler_bo);
   uint16_t hash_table[2048];
   uint32_t unique = 0;
   bool overflow = false;
   memset(hash_table, 0xff, sizeof(hash_table));

   static_assert(sizeof(struct agx_sampler_packed) == sizeof(uint64_t),
                 "sampler deduplication key");

   for (UINT logical = 0; logical < heap->desc.NumDescriptors; logical++) {
      struct agx_sampler_packed state;
      struct d12_descriptor *slot = &heap->slots[logical];
      if (slot->sampler_kind == D12_DESC_SAMPLER)
         state = slot->sampler;
      else
         agx_pack_txf_sampler(&state);

      uint64_t key;
      memcpy(&key, &state, sizeof(key));
      uint32_t bucket = ((uint32_t)key ^ (uint32_t)(key >> 32)) & 2047;
      uint32_t index = 0;
      while (hash_table[bucket] != UINT16_MAX) {
         index = hash_table[bucket];
         if (memcmp(&native[index], &state, sizeof(state)) == 0)
            break;
         bucket = (bucket + 1) & 2047;
      }

      if (hash_table[bucket] == UINT16_MAX) {
         if (unique >= 1024) {
            overflow = true;
            index = 0;
         } else {
            index = unique++;
            native[index] = state;
            hash_table[bucket] = index;
         }
      }

      slot->sampler_native_index = index;
   }

   heap->sampler_unique_count = overflow ? UINT32_MAX : unique;
}

void
d12_descriptor_heap_update_slot(struct d12_device *device,
                                struct d12_descriptor *slot)
{
   if (!device || !slot)
      return;

   uintptr_t ptr = (uintptr_t)slot;

   simple_mtx_lock(&device->descriptor_heap_lock);
   for (struct d12_descriptor_heap *heap = device->descriptor_heaps; heap;
        heap = heap->registry_next) {
      uintptr_t start = (uintptr_t)heap->slots;
      size_t bytes = (size_t)heap->desc.NumDescriptors * sizeof(*slot);
      if (ptr < start || ptr - start >= bytes ||
          (ptr - start) % sizeof(*slot))
         continue;

      size_t index = (ptr - start) / sizeof(*slot);
      if (heap->texture_bo) {
         struct agx_texture_packed *textures = agx_bo_map(heap->texture_bo);
         d12_refresh_texture_descriptor(&textures[index], slot);
      }
      if (heap->pbe_bo) {
         struct agx_pbe_packed *pbes = agx_bo_map(heap->pbe_bo);
         d12_refresh_pbe_descriptor(&pbes[index], slot);
      }
      if (heap->sampler_bo) {
         /* The shader-visible D3D12 limit is 2,048, while one AGX command
          * names at most 1,024 native sampler states.  Rebuild immediately
          * so descriptor updates remain visible to already-recorded command
          * lists, exactly like the coherent logical descriptor heap. */
         d12_descriptor_heap_rebuild_samplers(heap);
      }

      /* All three BOs are coherent. Order the native descriptor write after
       * the application's 40-byte slot write before another thread records
       * a dispatch that publishes these bases. */
      __sync_synchronize();
      break;
   }
   simple_mtx_unlock(&device->descriptor_heap_lock);
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_heap, ID3D12DescriptorHeap,
                                d12_heap_from_iface)

static HRESULT STDMETHODCALLTYPE
d12_heap_GetDevice(ID3D12DescriptorHeap *iface, REFIID riid, void **out)
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);
   return ID3D12Device_QueryInterface(&heap->device->iface, riid, out);
}

static D12_AGGREGATE_RETURN(D3D12_DESCRIPTOR_HEAP_DESC) STDMETHODCALLTYPE
d12_heap_GetDesc(ID3D12DescriptorHeap *iface
                 D12_AGGREGATE_OUT(D3D12_DESCRIPTOR_HEAP_DESC))
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);
   D12_RETURN_AGGREGATE(heap->desc);
}

static D12_AGGREGATE_RETURN(D3D12_CPU_DESCRIPTOR_HANDLE) STDMETHODCALLTYPE
d12_heap_GetCPUDescriptorHandleForHeapStart(
   ID3D12DescriptorHeap *iface
   D12_AGGREGATE_OUT(D3D12_CPU_DESCRIPTOR_HANDLE))
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);
   D3D12_CPU_DESCRIPTOR_HANDLE handle = {.ptr = (SIZE_T)heap->slots};
   D12_RETURN_AGGREGATE(handle);
}

static D12_AGGREGATE_RETURN(D3D12_GPU_DESCRIPTOR_HANDLE) STDMETHODCALLTYPE
d12_heap_GetGPUDescriptorHandleForHeapStart(
   ID3D12DescriptorHeap *iface
   D12_AGGREGATE_OUT(D3D12_GPU_DESCRIPTOR_HANDLE))
{
   struct d12_descriptor_heap *heap = d12_heap_from_iface(iface);

   /*
    * The GPU handle is the CPU address of the slot array.
    *
    * The handle stays an opaque CPU token so applications can perform the
    * advertised 40-byte arithmetic. SetDescriptorHeaps supplies the owning
    * object when an unbounded table needs to translate that token to the
    * coherent BO's GPU address.
    */
   D3D12_GPU_DESCRIPTOR_HANDLE handle = {
      .ptr = (heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
              heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
                ? (UINT64)(uintptr_t)heap->slots
                : 0,
   };
   D12_RETURN_AGGREGATE(handle);
}

static const ID3D12DescriptorHeapVtbl d12_heap_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12DescriptorHeap_DEFAULTS,
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

   switch (desc->Type) {
   case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
   case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
      if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
         mesa_loge("CreateDescriptorHeap: RTV/DSV heaps cannot be "
                   "shader-visible");
         return E_INVALIDARG;
      }
      break;

   case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
      /* Shader-visible storage is allocated below from a coherent BO so
       * unbounded ranges can index it directly. */
      break;

   case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
      /* Sampler creation is CPU-immediate too.  The packed hardware state
       * lives in the same fixed-size slot; binding/upload is a later
       * texturing concern and does not make heap object creation invalid. */
      break;

   default:
      mesa_loge("CreateDescriptorHeap: invalid type %d", (int)desc->Type);
      return E_INVALIDARG;
   }

   struct d12_descriptor_heap *heap = calloc(1, sizeof(*heap));
   if (!heap)
      return E_OUTOFMEMORY;

   if ((size_t)desc->NumDescriptors >
       SIZE_MAX / sizeof(struct d12_descriptor)) {
      free(heap);
      return E_OUTOFMEMORY;
   }
   size_t slots_size =
      (size_t)desc->NumDescriptors * sizeof(struct d12_descriptor);
   bool shader_visible =
      desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

   if (shader_visible) {
      heap->bo = agx_bo_create(&device->dev, slots_size, 0,
                               AGX_BO_WRITEBACK,
                               "d3d12 shader-visible descriptors");
      if (heap->bo)
         heap->slots = agx_bo_map(heap->bo);

      if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
         size_t texture_size =
            (size_t)desc->NumDescriptors * AGX_TEXTURE_LENGTH;
         /* One extra permanently-null PBE slot is the robust sink for
          * dynamically indexed typed-buffer atomics. Unlike texture fetch,
          * AGX buffer image atomics do not suppress coordinates beyond the
          * descriptor's element count. */
         size_t pbe_size =
            ((size_t)desc->NumDescriptors + 1) * AGX_PBE_LENGTH;
         heap->texture_bo =
            agx_bo_create(&device->dev, texture_size, 0, AGX_BO_WRITEBACK,
                          "d3d12 bindless texture descriptors");
         heap->pbe_bo =
            agx_bo_create(&device->dev, pbe_size, 0, AGX_BO_WRITEBACK,
                          "d3d12 bindless PBE descriptors");
      } else if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
         size_t sampler_size =
            (size_t)MIN2(desc->NumDescriptors, 1024) * AGX_SAMPLER_LENGTH;
         heap->sampler_bo =
            agx_bo_create(&device->dev, sampler_size, 0, AGX_BO_WRITEBACK,
                          "d3d12 bindless sampler descriptors");
      }
   } else {
      heap->slots = calloc(desc->NumDescriptors,
                           sizeof(struct d12_descriptor));
   }

   if (heap->bo && heap->slots)
      memset(heap->slots, 0, slots_size);

   /*
    * Start every slot as a NULL descriptor rather than as zeroes.
    *
    * A shader may read a descriptor the application never wrote -- D3D12
    * calls the contents undefined, but the read still has to be survivable,
    * and the suite tests exactly that (test_undefined_structured_raw_*). A
    * zeroed slot means address 0, and the GPU faults on it: `address: 0x0,
    * unit: DCMP, reason: Unmapped`, one of the last GPU timeouts in the
    * suite. Pointing undefined slots at the zero-filled null region makes
    * such a read return zeroes, which is what a null descriptor does.
    */
   if (heap->slots) {
      for (UINT i = 0; i < desc->NumDescriptors; i++) {
         heap->slots[i].address = device->null_va;
         heap->slots[i].size_B = D12_NULL_REGION_SIZE;
      }
   }

   if (heap->texture_bo && heap->pbe_bo) {
      struct agx_texture_packed *textures = agx_bo_map(heap->texture_bo);
      struct agx_pbe_packed *pbes = agx_bo_map(heap->pbe_bo);
      for (uint64_t i = 0; i <= desc->NumDescriptors; i++) {
         if (i < desc->NumDescriptors)
            agx_set_null_texture(&textures[i]);
         agx_set_null_pbe(&pbes[i]);
      }
   }
   if (heap->sampler_bo) {
      struct agx_sampler_packed *samplers = agx_bo_map(heap->sampler_bo);
      for (UINT i = 0; i < MIN2(desc->NumDescriptors, 1024); i++)
         agx_pack_txf_sampler(&samplers[i]);
   }

   bool native_ok = !shader_visible ||
      (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
          ? heap->texture_bo && heap->pbe_bo
          : desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
               ? heap->sampler_bo != NULL
               : true);
   if (!heap->slots || !native_ok) {
      if (heap->texture_bo)
         agx_bo_unreference(&device->dev, heap->texture_bo);
      if (heap->pbe_bo)
         agx_bo_unreference(&device->dev, heap->pbe_bo);
      if (heap->sampler_bo)
         agx_bo_unreference(&device->dev, heap->sampler_bo);
      if (heap->bo)
         agx_bo_unreference(&device->dev, heap->bo);
      else
         free(heap->slots);
      free(heap);
      return E_OUTOFMEMORY;
   }

   heap->iface.lpVtbl = (ID3D12DescriptorHeapVtbl *)&d12_heap_vtbl;
   heap->refcount = 1;
   heap->device = device;
   heap->desc = *desc;
   d12_descriptor_heap_rebuild_samplers(heap);
   d12_private_data_store_init(&heap->private_data, (IUnknown *)&heap->iface);
   d12_device_add_ref(device);

   simple_mtx_lock(&device->descriptor_heap_lock);
   heap->registry_next = device->descriptor_heaps;
   device->descriptor_heaps = heap;
   simple_mtx_unlock(&device->descriptor_heap_lock);

   HRESULT hr = d12_heap_QueryInterface(&heap->iface, riid, out);
   d12_heap_Release(&heap->iface);
   return hr;
}
