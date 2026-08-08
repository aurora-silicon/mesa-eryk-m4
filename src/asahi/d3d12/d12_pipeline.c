/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12PipelineState and ID3D12RootSignature.
 *
 * This is where the DXIL front end meets the runtime: CreateComputePipeline
 * State takes the application's DXIL bytecode, compiles it to AGX machine
 * code through our own translator (no SPIR-V, no external compiler), and
 * uploads the result to GPU memory ready to dispatch.
 */

#include "d12_private.h"

#include "agx_linker.h"

#include "agx_compile.h"
#include "agx_helpers.h"
#include "agx_nir.h"
#include "disasm.h"
#include "dxil/d12_dxil.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_xfb_info.h"
#include "poly/nir/poly_nir.h"

/* The runtime mirrors Honeykrisp's helper-program scratch allocation. Keep a
 * targeted dump for verifying which compiled stages exercise that path. */
static void
d12_report_scratch(const char *what, const struct agx_shader_info *info)
{
   if (getenv("D3D12AGX_DUMP_SCRATCH") &&
       (info->scratch_size || info->preamble_scratch_size))
      mesa_logi("%s uses scratch (%u dwords main, %u dwords preamble)", what,
                info->scratch_size, info->preamble_scratch_size);
}

/*
 * Root signature. D3D12 requires the object; our binding model puts
 * resource addresses directly in uniform registers (see d12_dxil.h), so
 * there is no state to derive from the blob yet.
 */

static struct d12_root_signature *
d12_root_sig_from_iface(ID3D12RootSignature *iface)
{
   return (struct d12_root_signature *)iface;
}

static struct d12_private_data_entry **
d12_private_data_find(struct d12_private_data_store *store, REFGUID guid)
{
   struct d12_private_data_entry **entry = &store->entries;

   while (*entry && !d12_guid_equal(&(*entry)->guid, guid))
      entry = &(*entry)->next;

   return entry;
}

static void
d12_private_data_entry_destroy(struct d12_private_data_entry *entry)
{
   if (entry->is_interface && entry->object)
      IUnknown_Release(entry->object);
   free(entry->bytes);
   free(entry);
}

/* DirectX-Guids in the current build does not export this d3dcommon IID. */
static const GUID d12_iid_destruction_notifier = {
   0xa06eb39a, 0x50da, 0x425b,
   {0x8c, 0x31, 0x4e, 0xec, 0xd6, 0xc2, 0x70, 0xf3},
};

static inline struct d12_destruction_notifier *
d12_notifier_from_iface(ID3DDestructionNotifier *iface)
{
   return (struct d12_destruction_notifier *)iface;
}

static HRESULT STDMETHODCALLTYPE
d12_notifier_QueryInterface(ID3DDestructionNotifier *iface, REFIID riid,
                            void **out)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);
   return IUnknown_QueryInterface(notifier->parent, riid, out);
}

static ULONG STDMETHODCALLTYPE
d12_notifier_AddRef(ID3DDestructionNotifier *iface)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);
   return IUnknown_AddRef(notifier->parent);
}

static ULONG STDMETHODCALLTYPE
d12_notifier_Release(ID3DDestructionNotifier *iface)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);
   return IUnknown_Release(notifier->parent);
}

static HRESULT STDMETHODCALLTYPE
d12_notifier_RegisterDestructionCallback(ID3DDestructionNotifier *iface,
                                         PFN_DESTRUCTION_CALLBACK callback,
                                         void *data, UINT *callback_id)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);

   if (!callback)
      return DXGI_ERROR_INVALID_CALL;

   simple_mtx_lock(&notifier->lock);

   if (notifier->notified) {
      simple_mtx_unlock(&notifier->lock);
      return DXGI_ERROR_INVALID_CALL;
   }

   if (notifier->callback_count == notifier->callback_capacity) {
      uint32_t capacity = notifier->callback_capacity
                              ? 2 * notifier->callback_capacity
                              : 4;
      struct d12_destruction_callback *callbacks =
         realloc(notifier->callbacks, capacity * sizeof(*callbacks));

      if (!callbacks) {
         simple_mtx_unlock(&notifier->lock);
         return E_OUTOFMEMORY;
      }

      notifier->callbacks = callbacks;
      notifier->callback_capacity = capacity;
   }

   struct d12_destruction_callback *entry =
      &notifier->callbacks[notifier->callback_count++];
   entry->callback = callback;
   entry->data = data;
   entry->id = 0;

   if (callback_id) {
      if (!(++notifier->next_callback_id))
         ++notifier->next_callback_id;

      entry->id = notifier->next_callback_id;
      *callback_id = entry->id;
   }

   simple_mtx_unlock(&notifier->lock);
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_notifier_UnregisterDestructionCallback(ID3DDestructionNotifier *iface,
                                           UINT callback_id)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);

   if (!callback_id)
      return DXGI_ERROR_NOT_FOUND;

   simple_mtx_lock(&notifier->lock);

   for (uint32_t i = 0; i < notifier->callback_count; ++i) {
      if (notifier->callbacks[i].id != callback_id)
         continue;

      memmove(&notifier->callbacks[i], &notifier->callbacks[i + 1],
              (notifier->callback_count - i - 1) *
                 sizeof(notifier->callbacks[0]));
      --notifier->callback_count;
      simple_mtx_unlock(&notifier->lock);
      return S_OK;
   }

   simple_mtx_unlock(&notifier->lock);
   return DXGI_ERROR_NOT_FOUND;
}

static const ID3DDestructionNotifierVtbl d12_notifier_vtbl = {
   .QueryInterface = d12_notifier_QueryInterface,
   .AddRef = d12_notifier_AddRef,
   .Release = d12_notifier_Release,
   .RegisterDestructionCallback = d12_notifier_RegisterDestructionCallback,
   .UnregisterDestructionCallback = d12_notifier_UnregisterDestructionCallback,
};

static void
d12_notifier_notify_and_destroy(struct d12_destruction_notifier *notifier)
{
   simple_mtx_lock(&notifier->lock);
   struct d12_destruction_callback *callbacks = notifier->callbacks;
   uint32_t callback_count = notifier->callback_count;
   notifier->callbacks = NULL;
   notifier->callback_count = 0;
   notifier->callback_capacity = 0;
   notifier->notified = true;
   simple_mtx_unlock(&notifier->lock);

   for (uint32_t i = 0; i < callback_count; ++i)
      callbacks[i].callback(callbacks[i].data);

   free(callbacks);
   simple_mtx_destroy(&notifier->lock);
}

void
d12_private_data_store_init(struct d12_private_data_store *store,
                            IUnknown *parent)
{
   simple_mtx_init(&store->lock, mtx_plain);
   store->destruction_notifier.iface.lpVtbl =
      (ID3DDestructionNotifierVtbl *)&d12_notifier_vtbl;
   store->destruction_notifier.parent = parent;
   simple_mtx_init(&store->destruction_notifier.lock, mtx_plain);
}

bool
d12_private_data_query_notifier(struct d12_private_data_store *store,
                                REFIID riid, void **out)
{
   if (!d12_guid_equal(riid, &d12_iid_destruction_notifier))
      return false;

   d12_notifier_AddRef(&store->destruction_notifier.iface);
   *out = &store->destruction_notifier.iface;
   return true;
}

void
d12_private_data_store_destroy(struct d12_private_data_store *store)
{
   d12_notifier_notify_and_destroy(&store->destruction_notifier);

   simple_mtx_lock(&store->lock);
   struct d12_private_data_entry *entry = store->entries;
   store->entries = NULL;
   simple_mtx_unlock(&store->lock);

   while (entry) {
      struct d12_private_data_entry *next = entry->next;
      d12_private_data_entry_destroy(entry);
      entry = next;
   }

   simple_mtx_destroy(&store->lock);
}

static HRESULT
d12_private_data_replace(struct d12_private_data_store *store, REFGUID guid,
                         struct d12_private_data_entry *replacement)
{
   simple_mtx_lock(&store->lock);
   struct d12_private_data_entry **slot = d12_private_data_find(store, guid);
   struct d12_private_data_entry *old = *slot;

   if (replacement) {
      replacement->next = old ? old->next : NULL;
      *slot = replacement;
   } else if (old) {
      *slot = old->next;
   }

   simple_mtx_unlock(&store->lock);

   if (old)
      d12_private_data_entry_destroy(old);

   return old || replacement ? S_OK : S_FALSE;
}

HRESULT
d12_private_data_set(struct d12_private_data_store *store, REFGUID guid,
                     UINT size, const void *data)
{
   if (!size || !data)
      return d12_private_data_replace(store, guid, NULL);

   struct d12_private_data_entry *entry = calloc(1, sizeof(*entry));
   if (!entry)
      return E_OUTOFMEMORY;

   entry->bytes = malloc(size);
   if (!entry->bytes) {
      free(entry);
      return E_OUTOFMEMORY;
   }

   entry->guid = *guid;
   entry->size = size;
   memcpy(entry->bytes, data, size);
   return d12_private_data_replace(store, guid, entry);
}

HRESULT
d12_private_data_set_interface(struct d12_private_data_store *store,
                               REFGUID guid, const IUnknown *object)
{
   struct d12_private_data_entry *entry = calloc(1, sizeof(*entry));
   if (!entry)
      return E_OUTOFMEMORY;

   entry->guid = *guid;
   entry->size = sizeof(entry->object);
   entry->object = (IUnknown *)object;
   entry->is_interface = true;
   if (entry->object)
      IUnknown_AddRef(entry->object);
   return d12_private_data_replace(store, guid, entry);
}

HRESULT
d12_private_data_get(struct d12_private_data_store *store, REFGUID guid,
                     UINT *size, void *data)
{
   if (!size)
      return E_INVALIDARG;

   simple_mtx_lock(&store->lock);
   struct d12_private_data_entry **slot = d12_private_data_find(store, guid);
   struct d12_private_data_entry *entry = *slot;

   if (!entry) {
      *size = 0;
      simple_mtx_unlock(&store->lock);
      return DXGI_ERROR_NOT_FOUND;
   }

   UINT requested_size = *size;
   *size = entry->size;

   if (!data) {
      simple_mtx_unlock(&store->lock);
      return S_OK;
   }

   if (requested_size < entry->size) {
      simple_mtx_unlock(&store->lock);
      return DXGI_ERROR_MORE_DATA;
   }

   if (entry->is_interface) {
      if (entry->object)
         IUnknown_AddRef(entry->object);
      *(IUnknown **)data = entry->object;
   } else {
      memcpy(data, entry->bytes, entry->size);
   }

   simple_mtx_unlock(&store->lock);
   return S_OK;
}

HRESULT
d12_private_data_set_name(struct d12_private_data_store *store, LPCWSTR name)
{
   /* wsl's host stub typedefs WCHAR as native wchar_t (32-bit on Linux),
    * while the D3D12 ABI and callers use 16-bit UTF-16 code units.  Treat the
    * incoming pointer as wire-format UTF-16; sizeof(*name) would report four
    * and read pairs of code units at a time on this host. */
   const uint16_t *utf16 = (const uint16_t *)name;
   size_t length = 0;
   while (utf16[length])
      length++;

   return d12_private_data_set(store, &WKPDID_D3DDebugObjectNameW,
                               (length + 1) * sizeof(*utf16), utf16);
}

static HRESULT STDMETHODCALLTYPE
d12_root_sig_QueryInterface(ID3D12RootSignature *iface, REFIID riid,
                            void **out)
{
   struct d12_root_signature *rs = d12_root_sig_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12RootSignature)) {
      p_atomic_inc(&rs->refcount);
      *out = iface;
      return S_OK;
   }

   if (d12_private_data_query_notifier(&rs->private_data, riid, out))
      return S_OK;

   d12_log_unknown_riid("ID3D12RootSignature::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_root_sig_AddRef(ID3D12RootSignature *iface)
{
   return p_atomic_inc_return(&d12_root_sig_from_iface(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_root_sig_Release(ID3D12RootSignature *iface)
{
   struct d12_root_signature *rs = d12_root_sig_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&rs->refcount);

   if (refs == 0) {
      d12_private_data_store_destroy(&rs->private_data);
      free(rs->static_samplers);
      d12_root_layout_cleanup(&rs->layout);
      d12_device_release(rs->device);
      free(rs);
   }

   return refs;
}

static void
d12_pack_static_sampler(struct d12_descriptor *out,
                        const D3D12_STATIC_SAMPLER_DESC1 *in)
{
   D3D12_SAMPLER_DESC2 desc = {
      .Filter = in->Filter,
      .AddressU = in->AddressU,
      .AddressV = in->AddressV,
      .AddressW = in->AddressW,
      .MipLODBias = in->MipLODBias,
      .MaxAnisotropy = in->MaxAnisotropy,
      .ComparisonFunc = in->ComparisonFunc,
      .MinLOD = in->MinLOD,
      .MaxLOD = in->MaxLOD,
      .Flags = in->Flags,
   };

   bool uint_border =
      (desc.Flags & D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR) ||
      in->BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT ||
      in->BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT;

   if (uint_border) {
      desc.Flags |= D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR;
      if (in->BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK ||
          in->BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT)
         desc.UintBorderColor[3] = 1;
      else if (in->BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE ||
               in->BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT)
         for (unsigned c = 0; c < 4; c++)
            desc.UintBorderColor[c] = 1;
   } else {
      if (in->BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK)
         desc.FloatBorderColor[3] = 1.0f;
      else if (in->BorderColor == D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE)
         for (unsigned c = 0; c < 4; c++)
            desc.FloatBorderColor[c] = 1.0f;
   }

   d12_pack_sampler_descriptor(out, &desc);
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_root_sig, ID3D12RootSignature,
                                d12_root_sig_from_iface)

static HRESULT STDMETHODCALLTYPE
d12_root_sig_GetDevice(ID3D12RootSignature *iface, REFIID riid, void **out)
{
   struct d12_root_signature *rs = d12_root_sig_from_iface(iface);
   return ID3D12Device_QueryInterface(&rs->device->iface, riid, out);
}

static const ID3D12RootSignatureVtbl d12_root_sig_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12RootSignature_DEFAULTS,
   .QueryInterface = d12_root_sig_QueryInterface,
   .AddRef = d12_root_sig_AddRef,
   .Release = d12_root_sig_Release,
   .GetPrivateData = d12_root_sig_GetPrivateData,
   .SetPrivateData = d12_root_sig_SetPrivateData,
   .SetPrivateDataInterface = d12_root_sig_SetPrivateDataInterface,
   .SetName = d12_root_sig_SetName,
   .GetDevice = d12_root_sig_GetDevice,
};

HRESULT
d12_root_signature_create(struct d12_device *device, const void *blob,
                          size_t blob_size, REFIID riid, void **out)
{
   if (!out)
      return E_POINTER;
   if (!blob || !blob_size)
      return E_INVALIDARG;

   struct d12_root_signature *rs = calloc(1, sizeof(*rs));
   if (!rs)
      return E_OUTOFMEMORY;

   /* The blob is parsed, not ignored. It is the only description of which
    * shader register each root parameter feeds, and accepting it without
    * reading it means every later binding is a guess.
    */
   if (!d12_root_signature_parse(blob, blob_size, &rs->layout)) {
      free(rs);
      /* A valid shader container may simply have no embedded root signature.
       * Native D3D12 distinguishes that case from a malformed argument with
       * E_FAIL (the conformance suite pins the otherwise surprising result).
       */
      return d12_dxbc_is_dxbc_shader(blob, blob_size) ? E_FAIL : E_INVALIDARG;
   }

   d12_root_layout_build_map(&rs->layout, &rs->map);

   if (rs->layout.num_static_samplers) {
      rs->static_samplers = calloc(rs->layout.num_static_samplers,
                                   sizeof(*rs->static_samplers));
      if (!rs->static_samplers) {
         d12_root_layout_cleanup(&rs->layout);
         free(rs);
         return E_OUTOFMEMORY;
      }

      for (uint32_t i = 0; i < rs->layout.num_static_samplers; i++) {
         const D3D12_STATIC_SAMPLER_DESC1 *s =
            &rs->layout.static_samplers[i];
         if (d12_binding_map_find_root_range(
                &rs->map,
                d12_static_sampler_root_range(&rs->layout, i),
                s->ShaderRegister) == ~0u) {
            free(rs->static_samplers);
            d12_root_layout_cleanup(&rs->layout);
            free(rs);
            return E_INVALIDARG;
         }
         d12_pack_static_sampler(&rs->static_samplers[i], s);
      }
   }

   rs->iface.lpVtbl = (ID3D12RootSignatureVtbl *)&d12_root_sig_vtbl;
   rs->refcount = 1;
   rs->device = device;
   d12_private_data_store_init(&rs->private_data, (IUnknown *)&rs->iface);
   d12_device_add_ref(device);

   HRESULT hr = d12_root_sig_QueryInterface(&rs->iface, riid, out);
   d12_root_sig_Release(&rs->iface);
   return hr;
}

/*
 * Pipeline state
 */

static struct d12_pipeline_state *
d12_pso_from_iface(ID3D12PipelineState *iface)
{
   return (struct d12_pipeline_state *)iface;
}

static ULONG
d12_pso_add_ref(struct d12_pipeline_state *pso)
{
   ULONG refs = p_atomic_inc_return(&pso->refcount);

   /* A library-owned PSO can have no public references while its compiled
    * object and private-data store remain cached. Reacquiring it makes the
    * device-child reference live again. */
   if (refs == 1 && !pso->device_ref_active) {
      d12_device_add_ref(pso->device);
      pso->device_ref_active = true;
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_pso_QueryInterface(ID3D12PipelineState *iface, REFIID riid, void **out)
{
   struct d12_pipeline_state *pso = d12_pso_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12Pageable) ||
       d12_guid_equal(riid, &IID_ID3D12PipelineState)) {
      d12_pso_add_ref(pso);
      *out = iface;
      return S_OK;
   }

   if (d12_private_data_query_notifier(&pso->private_data, riid, out))
      return S_OK;

   d12_log_unknown_riid("ID3D12PipelineState::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_pso_AddRef(ID3D12PipelineState *iface)
{
   return d12_pso_add_ref(d12_pso_from_iface(iface));
}

static void
d12_pso_destroy(struct d12_pipeline_state *pso)
{
   if (pso->code_bo)
      agx_bo_unreference(&pso->device->dev, pso->code_bo);
   if (pso->vs.code_bo)
      agx_bo_unreference(&pso->device->dev, pso->vs.code_bo);
   if (pso->fs.code_bo)
      agx_bo_unreference(&pso->device->dev, pso->fs.code_bo);
   if (pso->sw_vs_bo)
      agx_bo_unreference(&pso->device->dev, pso->sw_vs_bo);
   if (pso->sw_tes_bo)
      agx_bo_unreference(&pso->device->dev, pso->sw_tes_bo);
   if (pso->gs_count_bo)
      agx_bo_unreference(&pso->device->dev, pso->gs_count_bo);
   if (pso->gs_pre_bo)
      agx_bo_unreference(&pso->device->dev, pso->gs_pre_bo);
   if (pso->gs_main_bo)
      agx_bo_unreference(&pso->device->dev, pso->gs_main_bo);
   if (pso->gs_rast.code_bo)
      agx_bo_unreference(&pso->device->dev, pso->gs_rast.code_bo);
   if (pso->tcs_bo)
      agx_bo_unreference(&pso->device->dev, pso->tcs_bo);
   if (pso->tes.code_bo)
      agx_bo_unreference(&pso->device->dev, pso->tes.code_bo);
   if (pso->ms_bo)
      agx_bo_unreference(&pso->device->dev, pso->ms_bo);
   if (pso->ms_setup_bo)
      agx_bo_unreference(&pso->device->dev, pso->ms_setup_bo);
   if (pso->ms_rast.code_bo)
      agx_bo_unreference(&pso->device->dev, pso->ms_rast.code_bo);
   if (pso->as_bo)
      agx_bo_unreference(&pso->device->dev, pso->as_bo);
   if (pso->as_setup_bo)
      agx_bo_unreference(&pso->device->dev, pso->as_setup_bo);
   free(pso->cached_blob);
   d12_private_data_store_destroy(&pso->private_data);
   if (pso->device_ref_active)
      d12_device_release(pso->device);
   free(pso);
}

static ULONG STDMETHODCALLTYPE
d12_pso_Release(ID3D12PipelineState *iface)
{
   struct d12_pipeline_state *pso = d12_pso_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&pso->refcount);

   if (!refs) {
      if (!p_atomic_read(&pso->library_refcount)) {
         /* Ordinary PSOs must release their BOs while their device reference
          * is still live. */
         d12_pso_destroy(pso);
      } else if (pso->device_ref_active) {
         d12_device_release(pso->device);
         pso->device_ref_active = false;
      }
   }

   return refs;
}

void
d12_pipeline_state_library_ref(struct d12_pipeline_state *pso)
{
   p_atomic_inc(&pso->library_refcount);
}

void
d12_pipeline_state_library_unref(struct d12_pipeline_state *pso)
{
   uint32_t refs = p_atomic_dec_return(&pso->library_refcount);
   if (!refs && !p_atomic_read(&pso->refcount))
      d12_pso_destroy(pso);
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_pso, ID3D12PipelineState,
                                d12_pso_from_iface)

static HRESULT STDMETHODCALLTYPE
d12_pso_GetDevice(ID3D12PipelineState *iface, REFIID riid, void **out)
{
   struct d12_pipeline_state *pso = d12_pso_from_iface(iface);
   return ID3D12Device_QueryInterface(&pso->device->iface, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_pso_GetCachedBlob(ID3D12PipelineState *iface, ID3DBlob **blob)
{
   struct d12_pipeline_state *pso = d12_pso_from_iface(iface);
   if (!blob)
      return E_POINTER;

   *blob = NULL;
   if (!pso->cached_blob || !pso->cached_blob_size)
      return E_NOTIMPL;

   *blob = d12_blob_create(pso->cached_blob, pso->cached_blob_size);
   return *blob ? S_OK : E_OUTOFMEMORY;
}

static const ID3D12PipelineStateVtbl d12_pso_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12PipelineState_DEFAULTS,
   .QueryInterface = d12_pso_QueryInterface,
   .AddRef = d12_pso_AddRef,
   .Release = d12_pso_Release,
   .GetPrivateData = d12_pso_GetPrivateData,
   .SetPrivateData = d12_pso_SetPrivateData,
   .SetPrivateDataInterface = d12_pso_SetPrivateDataInterface,
   .SetName = d12_pso_SetName,
   .GetDevice = d12_pso_GetDevice,
   .GetCachedBlob = d12_pso_GetCachedBlob,
};

static bool
d12_container_stage(const void *data, size_t size_B,
                    mesa_shader_stage *stage)
{
   return d12_dxil_container_stage(data, size_B, stage) ||
          d12_dxbc_container_stage(data, size_B, stage);
}

static D3D12_SHADER_VISIBILITY
d12_stage_visibility(mesa_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:    return D3D12_SHADER_VISIBILITY_VERTEX;
   case MESA_SHADER_TESS_CTRL: return D3D12_SHADER_VISIBILITY_HULL;
   case MESA_SHADER_TESS_EVAL: return D3D12_SHADER_VISIBILITY_DOMAIN;
   case MESA_SHADER_GEOMETRY:  return D3D12_SHADER_VISIBILITY_GEOMETRY;
   case MESA_SHADER_FRAGMENT:  return D3D12_SHADER_VISIBILITY_PIXEL;
   case MESA_SHADER_TASK:      return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
   case MESA_SHADER_MESH:      return D3D12_SHADER_VISIBILITY_MESH;
   default:                    return D3D12_SHADER_VISIBILITY_ALL;
   }
}

/* Application samplers remain in the USC sampler-state file. Count them
 * before agx_nir_lower_texture reserves hardware state 0 for texture Load
 * and rewrites D3D sampler S to hardware state S + 1. The compiler only
 * reports states it itself promoted into the preamble, which is not enough
 * to size the runtime state file for an ordinary Sample instruction. */
static unsigned
d12_nir_sampler_state_count(nir_shader *nir,
                            const struct d12_binding_map *map)
{
   unsigned count = 0;

   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_tex)
               continue;

            nir_tex_instr *tex = nir_instr_as_tex(instr);
            if (!nir_tex_instr_need_sampler(tex))
               continue;

            /* A sampler handle selects the command's global sampler heap;
             * it consumes no entry in the per-stage USC sampler file. */
            if (nir_tex_instr_src_index(
                   tex, nir_tex_src_sampler_handle) >= 0)
               continue;

            if (nir_tex_instr_src_index(tex,
                                        nir_tex_src_sampler_offset) >= 0) {
               if (!map || map->sampler_count >= 17)
                  return UINT_MAX;
               count = MAX2(count, map->sampler_count + 1);
            } else {
               if (tex->sampler_index >= 16)
                  return UINT_MAX;
               count = MAX2(count, tex->sampler_index + 2);
            }
         }
      }
   }

   return count;
}

/*
 * The real work: application DXIL -> our translator -> NIR -> AGX, then
 * upload. This is the campaign's shader path running inside the driver.
 */
HRESULT
d12_pipeline_state_create_compute(struct d12_device *device,
                                  const void *dxil, size_t dxil_size,
                                  const struct d12_binding_map *map,
                                  REFIID riid, void **out)
{
   if (!out)
      return E_POINTER;
   if (!dxil || !dxil_size)
      return E_INVALIDARG;

   struct d12_binding_map filtered_map;
   if (map) {
      filtered_map = *map;
      filtered_map.visibility_filter = D3D12_SHADER_VISIBILITY_ALL;
      map = &filtered_map;
   }

   /* The container declares its stage; honour it or refuse. Forcing a
    * non-compute shader through this path compiles its store_output
    * against the compute backend, which asserts "Unhandled intrinsic"
    * and takes the process with it -- a crash where an E_INVALIDARG
    * belongs. Same rule the standalone tool already follows
    * (docs/findings.md).
    */
   mesa_shader_stage declared;
   if (d12_container_stage(dxil, dxil_size, &declared) &&
       declared != MESA_SHADER_COMPUTE) {
      mesa_loge("CreateComputePipelineState: bytecode declares a %s shader",
                _mesa_shader_stage_to_string(declared));
      return E_INVALIDARG;
   }

   const uint8_t *bc;
   size_t bc_size;
   const char *why = NULL;
   nir_shader *dxbc_nir = NULL;
   if (!d12_dxil_find_bitcode(dxil, dxil_size, &bc, &bc_size, &why)) {
      /* Shader Model 5.x compute has no DXIL either -- same fallback as
       * the graphics path. */
      if (d12_dxbc_is_dxbc_shader(dxil, dxil_size)) {
         const char *dxbc_error = "unknown";
         dxbc_nir = d12_dxbc_to_nir(dxil, dxil_size, MESA_SHADER_COMPUTE, map,
                                    &dxbc_error);
         if (!dxbc_nir) {
            mesa_loge("CreateComputePipelineState: DXBC->NIR failed: %s",
                      dxbc_error);
            return E_INVALIDARG;
         }
      } else {
         mesa_loge("CreateComputePipelineState: %s", why);
         return E_INVALIDARG;
      }
   }

   nir_shader *nir = dxbc_nir;
   const char *error = NULL;

   if (!nir) {
      struct d12_module mod;
      if (!d12_dxil_module_parse(&mod, bc, bc_size)) {
         mesa_loge("CreateComputePipelineState: DXIL parse failed: %s",
                   mod.error ? mod.error : "unknown");
         return E_INVALIDARG;
      }

      nir = d12_dxil_to_nir(&mod, MESA_SHADER_COMPUTE, map, &error);
      d12_dxil_module_finish(&mod);
   }

   if (!nir) {
      mesa_loge("CreateComputePipelineState: DXIL->NIR failed: %s", error);
      return E_INVALIDARG;
   }

   unsigned runtime_samplers = d12_nir_sampler_state_count(nir, map);
   if (runtime_samplers == UINT_MAX) {
      mesa_loge("CreateComputePipelineState: shader sampler state range "
                "exceeds the 16 API sampler limit");
      ralloc_free(nir);
      return E_INVALIDARG;
   }

   NIR_PASS(_, nir, agx_nir_lower_texture);
   /* Multisampled image stores lower the same way in every stage; the
    * FS-only placement left compute MS stores to hit the backend raw.
    */
   NIR_PASS(_, nir, agx_nir_lower_multisampled_image_store);
   agx_preprocess_nir(nir);

   /* Must follow preprocessing: emitting the uniform read earlier lets the
    * compiler reassign its register.
    */
   NIR_PASS(_, nir, d12_dxil_lower_root);

   struct agx_shader_key key = {
      .dev = agx_gather_device_key(&device->dev),
      /* Allocas live in per-thread scratch, and large shaders spill. */
      .has_scratch = true,
      /*
       * Constant promotion parks constants in uniform registers that only
       * the shader's preamble fills. Both USC builders push a PRESHADER
       * word, so this looks safe -- and for compute it is. Turning it on
       * was measured on 2026-08-04 and it fails every graphics test
       * (g4_triangle, g5_depth) while changing nothing about compute.
       *
       * The graphics path takes the preamble's address from
       * st->info.preamble_offset but the code from st->linked.bo, and
       * those are the PRE-link info and the POST-fast-link binary. The
       * offset does not survive agx_fast_link, so the GPU runs whatever
       * sits at that offset in the linked binary. That is the "high half
       * of a 64-bit address came back 0x86 instead of 0x2f" this comment
       * used to describe -- not a missing preamble, a misaddressed one.
       *
       * Graphics stages now retain their original preamble code, but
       * constant promotion remains disabled until its separate register and
       * rodata interactions have a broad regression pass. Texture promotion
       * below is independently required and validated.
       */
      .promote_constants = false,
      /* Uniform registers below this are the driver's: the root pointer. */
      .reserved_preamble = D12_UNIFORM_RESERVED,
   };

   struct agx_shader_part part;
   memset(&part, 0, sizeof(part));
   agx_compile_shader_nir(nir, &key, &part);
   part.info.sampler_state_count =
      MAX2(part.info.sampler_state_count, runtime_samplers);
   d12_report_scratch("shader", &part.info);

   if (getenv("D3D12AGX_DUMP_TEX") && part.binary) {
      fprintf(stderr,
              "driver cs: has_preamble=%d preamble_off=0x%x main_off=0x%x "
              "push_count=%u nr_preamble_gprs=%u rodata.size_16=%u\n",
              part.info.has_preamble, part.info.preamble_offset,
              part.info.main_offset, part.info.push_count,
              part.info.nr_preamble_gprs, part.info.rodata.size_16);
      if (part.info.has_preamble) {
         fprintf(stderr, "; driver preamble\n");
         agx2_disassemble(part.binary + part.info.preamble_offset,
                          part.info.main_offset > part.info.preamble_offset
                             ? part.info.main_offset - part.info.preamble_offset
                             : part.info.binary_size - part.info.preamble_offset,
                          stderr);
      }
      fprintf(stderr, "; driver main\n");
      agx2_disassemble(part.binary + part.info.main_offset,
                       part.info.binary_size - part.info.main_offset, stderr);
   }

   if (!part.binary || !part.info.binary_size) {
      mesa_loge("CreateComputePipelineState: AGX compile produced no code");
      free(part.binary);
      return E_FAIL;
   }

   struct d12_pipeline_state *pso = calloc(1, sizeof(*pso));
   if (!pso) {
      free(part.binary);
      return E_OUTOFMEMORY;
   }

   /* Shader code must live in executable, low-VA memory so the USC can
    * address it 32-bit-relative to the shader base.
    */
   pso->code_bo = agx_bo_create(&device->dev, part.info.binary_size, 0,
                                AGX_BO_EXEC | AGX_BO_LOW_VA,
                                "d3d12 shader");
   if (!pso->code_bo) {
      free(part.binary);
      free(pso);
      return E_OUTOFMEMORY;
   }

   memcpy(agx_bo_map(pso->code_bo), part.binary, part.info.binary_size);

   pso->iface.lpVtbl = (ID3D12PipelineStateVtbl *)&d12_pso_vtbl;
   pso->refcount = 1;
   pso->device_ref_active = true;
   pso->device = device;
   pso->is_compute = true;
   pso->info = part.info;
   pso->code_va = pso->code_bo->va->addr;
   d12_private_data_store_init(&pso->private_data, (IUnknown *)&pso->iface);
   d12_device_add_ref(device);

   mesa_logi("compiled compute shader: %u bytes AGX, %u GPRs, main +0x%x, "
             "numthreads %ux%ux%u, push %u, preamble %d",
             part.info.binary_size, part.info.nr_gprs,
             part.info.main_offset, part.info.workgroup_size[0],
             part.info.workgroup_size[1], part.info.workgroup_size[2],
             part.info.push_count, part.info.has_preamble);

   if (getenv("D3D12AGX_DUMP_SHADERS")) {
      if (part.info.has_preamble) {
         fprintf(stderr, "; preamble +0x%x\n", part.info.preamble_offset);
         agx2_disassemble(part.binary + part.info.preamble_offset,
                          part.info.main_offset - part.info.preamble_offset,
                          stderr);
      }
      fprintf(stderr, "; main +0x%x\n", part.info.main_offset);
      agx2_disassemble(part.binary + part.info.main_offset,
                       part.info.binary_size - part.info.main_offset, stderr);
   }

   free(part.binary);

   HRESULT hr = d12_pso_QueryInterface(&pso->iface, riid, out);
   d12_pso_Release(&pso->iface);
   return hr;
}

/*
 * Graphics: compile one stage of DXIL to an AGX shader part, running the
 * stage's mandatory lowering (tilebuffer for fragment, UVS for vertex).
 */
/*
 * DXIL container -> NIR, with nothing lowered yet.
 *
 * Split out because the geometry and tessellation lowerings have to run
 * on raw NIR, before agx_preprocess_nir: poly_nir_lower_gs REWRITES
 * info.stage to COMPUTE, so anything that reads the stage back has to
 * come after it.
 */
static nir_shader *
dxil_to_nir_stage(const struct d12_binding_map *map, const void *dxil,
                  size_t dxil_size, mesa_shader_stage stage)
{
   struct d12_binding_map filtered_map;
   if (map) {
      filtered_map = *map;
      filtered_map.visibility_filter = d12_stage_visibility(stage);
      map = &filtered_map;
   }

   mesa_shader_stage declared;
   if (d12_container_stage(dxil, dxil_size, &declared) &&
       declared != stage) {
      mesa_loge("graphics PSO: %s bytecode declares a %s shader",
                _mesa_shader_stage_to_string(stage),
                _mesa_shader_stage_to_string(declared));
      return NULL;
   }

   const uint8_t *bc;
   size_t bc_size;
   const char *why = NULL;
   if (!d12_dxil_find_bitcode(dxil, dxil_size, &bc, &bc_size, &why)) {
      /*
       * No DXIL. Shader Model 5.x has none -- fxc emits a SHEX/SHDR
       * token stream instead -- and SM 5.1 is legal D3D12, so fall
       * through to the DXBC front end rather than failing. This is the
       * single largest conformance gap measured against vkd3d-proton:
       * 202 of 557 tests stop right here.
       */
      if (d12_dxbc_is_dxbc_shader(dxil, dxil_size)) {
         const char *dxbc_error = "unknown";
         nir_shader *nir =
            d12_dxbc_to_nir(dxil, dxil_size, stage, map, &dxbc_error);
         if (!nir)
            mesa_loge("graphics PSO: DXBC->NIR failed: %s", dxbc_error);
         return nir;
      }

      mesa_loge("graphics PSO: %s", why);
      return NULL;
   }

   struct d12_module mod;
   if (!d12_dxil_module_parse(&mod, bc, bc_size)) {
      mesa_loge("graphics PSO: DXIL parse failed: %s",
                mod.error ? mod.error : "unknown");
      return NULL;
   }

   const char *error = NULL;
   nir_shader *nir = d12_dxil_to_nir(&mod, stage, map, &error);
   d12_dxil_module_finish(&mod);

   if (!nir)
      mesa_loge("graphics PSO: DXIL->NIR failed: %s", error);

   return nir;
}

/*
 * The vertex-fetch keys a PSO compiles with, indexed by DXIL input
 * signature element id. Empty means "no input layout", which keeps the
 * original static_vi prolog path byte for byte.
 */
struct d12_vs_fetch {
   struct agx_velem_key keys[D12_MAX_VB_ATTRIBS];
   unsigned nr;
};

/*
 * Resolve a D3D12_INPUT_LAYOUT_DESC against the vertex shader's own DXIL
 * input signature.
 *
 * D3D12 binds attributes by (SemanticName, SemanticIndex) -- NOT by
 * position in either list. An input layout may describe attributes the
 * shader never reads, list them in any order, and the shader may read
 * inputs the layout does not supply. Matching positionally happens to
 * work for a layout written to match its shader and silently corrupts
 * every other one, so the match is done by name.
 *
 * Semantic names are ASCII and compared case-insensitively, as D3D does.
 */
static void
resolve_input_layout(struct d12_pipeline_state *pso,
                     const D3D12_INPUT_LAYOUT_DESC *layout, const void *vs_dxil,
                     size_t vs_size, struct d12_vs_fetch *fetch)
{
   memset(fetch, 0, sizeof(*fetch));
   pso->nr_vb_attribs = 0;

   if (!layout || !layout->NumElements || !layout->pInputElementDescs)
      return;

   /*
    * DXBC and DXIL keep their input signatures in different places: DXIL
    * in the LLVM bitcode module, DXBC in the container's ISGN chunk. Using
    * the DXIL reader on a DXBC shader silently returns zero elements, so
    * nothing matches the input layout, no attributes are fetched, and
    * load_input reaches the AGX backend as "Unhandled intrinsic" -- which
    * was the sole blocker on all eight tests in that cluster, every one of
    * them a _dxbc variant.
    */
   struct d12_input_sig_elem sig[D12_MAX_SIG_ELEMENTS];
   unsigned nr_sig =
      d12_dxbc_is_dxbc_shader(vs_dxil, vs_size)
         ? d12_dxbc_input_signature(vs_dxil, vs_size, sig, ARRAY_SIZE(sig))
         : d12_dxil_input_signature(vs_dxil, vs_size, sig, ARRAY_SIZE(sig));

   for (unsigned e = 0; e < layout->NumElements; e++) {
      const D3D12_INPUT_ELEMENT_DESC *el = &layout->pInputElementDescs[e];
      if (!el->SemanticName)
         continue;

      /* Find the shader input this element feeds. An element with no
       * match is legal and simply unused. */
      unsigned id = UINT32_MAX;
      for (unsigned i = 0; i < nr_sig; i++) {
         if (sig[i].semantic_index == el->SemanticIndex &&
             !strcasecmp(sig[i].name, el->SemanticName)) {
            id = sig[i].id;
            break;
         }
      }

      if (id == UINT32_MAX)
         continue;

      if (id >= D12_MAX_VB_ATTRIBS || el->InputSlot >= D12_MAX_VB_SLOTS) {
         mesa_logw("input layout: %s%u out of range (id %u, slot %u)",
                   el->SemanticName, el->SemanticIndex, id, el->InputSlot);
         continue;
      }

      enum pipe_format fmt = d12_format_to_pipe(el->Format);
      if (fmt == PIPE_FORMAT_NONE) {
         mesa_logw("input layout: %s%u has unsupported DXGI format %u",
                   el->SemanticName, el->SemanticIndex,
                   (unsigned)el->Format);
         continue;
      }

      bool instanced =
         el->InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;

      /* D3D12_APPEND_ALIGNED_ELEMENT means "pack after the previous
       * element in the same slot". */
      uint32_t offset = el->AlignedByteOffset;
      if (offset == D3D12_APPEND_ALIGNED_ELEMENT) {
         offset = 0;
         for (unsigned k = 0; k < e; k++) {
            const D3D12_INPUT_ELEMENT_DESC *pe =
               &layout->pInputElementDescs[k];
            if (pe->InputSlot != el->InputSlot)
               continue;
            enum pipe_format pf = d12_format_to_pipe(pe->Format);
            if (pf == PIPE_FORMAT_NONE)
               continue;
            uint32_t end = (pe->AlignedByteOffset ==
                            D3D12_APPEND_ALIGNED_ELEMENT)
                              ? offset
                              : pe->AlignedByteOffset;
            offset = MAX2(offset, end + util_format_get_blocksize(pf));
         }
      }

      enum pipe_format interchange = agx_vbo_internal_format(fmt);

      pso->vb_attribs[id] = (struct d12_vb_attrib){
         .live = true,
         .slot = (uint8_t)el->InputSlot,
         .offset_B = offset,
         .format = fmt,
         .interchange_align = util_format_get_blocksize(interchange),
      };

      /* stride stays 0: it is published per draw, see the dynamic-stride
       * note in compile_gfx_stage. */
      fetch->keys[id] = (struct agx_velem_key){
         .format = fmt,
         .stride = 0,
         /* StepRate 0 means element zero for every instance in the draw.
          * agx_nir_lower_vbo already implements that distinct mode when
          * divisor is zero, so it must not be collapsed to divisor 1.
          */
         .divisor = instanced ? el->InstanceDataStepRate : 0,
         .instanced = instanced,
      };

      fetch->nr = MAX2(fetch->nr, id + 1);
      pso->nr_vb_attribs = fetch->nr;
   }

   /*
    * An input the SHADER reads but the layout does not supply is legal:
    * D3D12 defines the fetch as reading zeroes. Give it a well-formed key
    * anyway, because agx_nir_lower_vbo has to fetch SOMETHING for every
    * load_input it sees -- a zeroed key means PIPE_FORMAT_NONE, which has
    * no channels to describe. Left un-live, so the draw path points its
    * base at the zero page.
    */
   for (unsigned i = 0; i < nr_sig; i++) {
      uint32_t id = sig[i].id;
      if (id >= D12_MAX_VB_ATTRIBS || pso->vb_attribs[id].live)
         continue;

      fetch->keys[id] = (struct agx_velem_key){
         .format = PIPE_FORMAT_R32G32B32A32_FLOAT,
      };
      fetch->nr = MAX2(fetch->nr, id + 1);
      pso->vb_attribs[id].interchange_align = 4;
      pso->nr_vb_attribs = fetch->nr;
   }
}

static bool
d12_lower_coverage_to_epilog_intr(nir_builder *b,
                                  nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location != FRAG_RESULT_SAMPLE_MASK)
      return false;

   /* The split fragment path writes colour in a linked epilog.  A hardware
    * sample-mask instruction in the API part does not constrain that later
    * tilebuffer store, so pass SV_Coverage through the established epilog ABI
    * as its explicit write-sample mask instead. */
   b->cursor = nir_instr_remove(&intr->instr);
   nir_export_agx(b, nir_u2u16(b, intr->src[0].ssa),
                  .base = AGX_ABI_FOUT_WRITE_SAMPLES);
   *(bool *)data = true;
   return true;
}

static bool
d12_nir_lower_coverage_to_epilog(nir_shader *nir, bool *lowered)
{
   return nir_shader_intrinsics_pass(
      nir, d12_lower_coverage_to_epilog_intr, nir_metadata_control_flow,
      lowered);
}

static bool
d12_lower_a8_output_instr(nir_builder *b, nir_intrinsic_instr *intr,
                          void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   unsigned rt = sem.location - FRAG_RESULT_DATA0;
   uint8_t mask = *(const uint8_t *)data;
   if (sem.location < FRAG_RESULT_DATA0 || rt >= 8 ||
       !(mask & BITFIELD_BIT(rt)))
      return false;

   nir_def *value = intr->src[0].ssa;
   if (value->num_components < 4)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *alpha = nir_channel(b, value, 3);
   nir_src_rewrite(&intr->src[0], nir_replicate(b, alpha,
                                                value->num_components));
   return true;
}

static bool
d12_nir_lower_a8_outputs(nir_shader *nir, uint8_t mask)
{
   if (!mask)
      return false;

   return nir_shader_intrinsics_pass(
      nir, d12_lower_a8_output_instr, nir_metadata_control_flow, &mask);
}

static bool
compile_gfx_stage(struct d12_device *device,
                  const struct d12_binding_map *map, const void *dxil,
                  size_t dxil_size, mesa_shader_stage stage,
                  struct agx_tilebuffer_layout *tib,
                  uint16_t api_sample_mask,
                  uint8_t a8_mask,
                  struct agx_unlinked_uvs_layout *uvs,
                  const struct d12_vs_fetch *fetch,
                  uint64_t *outputs_written, bool *use_fs_epilog,
                  struct agx_fs_epilog_link_info *fs_epilog_link,
                  uint8_t *cull_distance_size,
                  struct agx_shader_part *out)
{
   nir_shader *nir;
   if (!dxil && stage == MESA_SHADER_FRAGMENT) {
      /* D3D12 permits a null pixel shader, notably for depth-only draws.
       * AGX still needs a fragment program bound, so compile the same empty
       * NIR stage Honeykrisp uses for API-level null fragment shaders.
       */
      nir_builder b = nir_builder_init_simple_shader(
         MESA_SHADER_FRAGMENT, &agx_nir_options, "d12 empty fragment shader");
      nir = b.shader;
   } else {
      nir = dxil_to_nir_stage(map, dxil, dxil_size, stage);
   }
   if (!nir)
      return false;

   /* The bytecode front ends lower I/O directly to intrinsics and therefore
    * set compact clip/cull sizes themselves. nir_shader_gather_info cannot
    * reconstruct those array lengths from variables (there are none), so
    * retain them across the generic metadata refresh below. */
   const uint8_t translated_clip_size = nir->info.clip_distance_array_size;
   const uint8_t translated_cull_size = nir->info.cull_distance_array_size;
   if (stage != MESA_SHADER_FRAGMENT && cull_distance_size) {
      *cull_distance_size = nir->info.cull_distance_array_size;
   }

   /* AGX exposes integer pixel coordinates. D3D12, like Vulkan, defines
    * SV_Position.xy at the sample position (pixel centre without sample
    * shading), so add the standard half-pixel/sample offset before the
    * backend splits frag_coord into pixel-coordinate and Z/W loads. */
   if (stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_lower_wpos_center);

   if (stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, d12_nir_lower_a8_outputs, a8_mask);

   /*
    * The DXIL and DXBC front ends do not gather shader-wide metadata.  In
    * particular, fragment UAV atomics otherwise leave writes_memory false,
    * causing AGX preprocessing to omit the side-effect lowering and compile
    * the shader with tag writes disabled.  Honeykrisp gathers this at the
    * same boundary, before agx_preprocess_nir.
    */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   nir->info.clip_distance_array_size =
      MAX2(nir->info.clip_distance_array_size, translated_clip_size);
   nir->info.cull_distance_array_size =
      MAX2(nir->info.cull_distance_array_size, translated_cull_size);

   unsigned runtime_samplers = d12_nir_sampler_state_count(nir, map);
   if (runtime_samplers == UINT_MAX) {
      mesa_loge("graphics shader sampler state range exceeds the 16 API "
                "sampler limit");
      ralloc_free(nir);
      return false;
   }

   if (outputs_written)
      *outputs_written = nir->info.outputs_written;

   if (stage == MESA_SHADER_FRAGMENT && api_sample_mask != 0xff) {
      /* Match Honeykrisp's FS prolog: the D3D12 PSO sample mask kills the
       * samples it does not cover before the application shader runs. */
      nir_builder b =
         nir_builder_at(nir_before_impl(nir_shader_get_entrypoint(nir)));
      nir_demote_samples(
         &b, nir_imm_intN_t(&b, api_sample_mask ^ 0xff, 16));
      nir->info.fs.uses_discard = true;
   }

   bool split_fs = false;
   if (stage == MESA_SHADER_FRAGMENT && use_fs_epilog) {
      /* A shader-written coverage mask has to execute before tilebuffer
       * stores.  A monolithic NIR shader may legally write SV_Target before
       * SV_Coverage; lowering that literally would place sample_mask after
       * store_local_pixel, which AGX explicitly forbids.  Honeykrisp's split
       * path gives the required ordering: the API shader applies coverage and
       * exports colour, then the epilog writes the tilebuffer. */
      const bool writes_coverage =
         nir->info.outputs_written &
         BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK);
      split_fs = (*use_fs_epilog || writes_coverage) &&
                 (nir->info.outputs_written &
                  BITFIELD64_RANGE(FRAG_RESULT_DATA0, 8));
      *use_fs_epilog = split_fs;
   }

   NIR_PASS(_, nir, agx_nir_lower_texture);
   agx_preprocess_nir(nir);
   NIR_PASS(_, nir, d12_dxil_lower_root_mask, api_sample_mask);

   if (stage == MESA_SHADER_FRAGMENT && split_fs) {
      /*
       * Blending reads the old tile value, so it lives in the standard AGX
       * fragment epilog rather than in the API shader. Export the API
       * shader's colours/depth/stencil for that epilog and leave both parts
       * unterminated; agx_fast_link appends the final stop.
       *
       * This is the same split Honeykrisp uses. Keeping it conditional means
       * the established monolithic path remains byte-for-byte unchanged for
       * the overwhelmingly common no-blend case.
       */
      bool coverage_to_epilog = false;
      NIR_PASS(_, nir, d12_nir_lower_coverage_to_epilog,
               &coverage_to_epilog);
      fs_epilog_link->sample_mask_after_force_early |= coverage_to_epilog;
      NIR_PASS(_, nir, agx_nir_lower_discard_zs_emit);
      NIR_PASS(_, nir, agx_nir_lower_fs_output_to_epilog, fs_epilog_link);
      NIR_PASS(_, nir, agx_nir_lower_sample_intrinsics, false);
      NIR_PASS(_, nir, agx_nir_lower_sample_mask);
      NIR_PASS(_, nir, agx_nir_lower_fs_active_samples_to_register);
      NIR_PASS(_, nir, agx_nir_lower_interpolation);

      /* The passes above may introduce driver-owned sysvals. */
      NIR_PASS(_, nir, d12_dxil_lower_root_mask, api_sample_mask);
      NIR_PASS(_, nir, agx_nir_lower_multisampled_image_store);
      NIR_PASS(_, nir, agx_nir_lower_texture);
   } else if (stage == MESA_SHADER_FRAGMENT) {
      unsigned bindless_base = 0;
      NIR_PASS(_, nir, agx_nir_lower_tilebuffer, tib, NULL, &bindless_base,
               NULL, NULL);
      /* Depth/stencil-ref writes become zs_emit, a coverage write
       * becomes demote_samples, and discards become sample_mask writes.
       * hk orders these the same way (hk_shader.c): zs_emit before the
       * sample-mask lowering, everything before MSAA lowering.
       */
      NIR_PASS(_, nir, agx_nir_lower_discard_zs_emit);
      NIR_PASS(_, nir, agx_nir_lower_sample_intrinsics, false);
      NIR_PASS(_, nir, agx_nir_lower_sample_mask);
      NIR_PASS(_, nir, agx_nir_lower_monolithic_msaa,
               MAX2(tib->nr_samples, 1));
      NIR_PASS(_, nir, agx_nir_lower_interpolation);
      /* The passes above can introduce driver-owned sysvals
       * (api_sample_mask, shader_part_tests_zs); rerun the lowering that
       * resolves them. No-op when none appeared.
       */
      NIR_PASS(_, nir, d12_dxil_lower_root_mask, api_sample_mask);
      NIR_PASS(_, nir, agx_nir_lower_multisampled_image_store);
      NIR_PASS(_, nir, agx_nir_lower_texture);
   } else {
      /* Real vertex attributes arrive through the prolog ABI: the main
       * shader reads registers the prolog exported, so load_input must
       * become load_exported_agx here (hk_shader.c does the same). A
       * DRAW with a nonempty input layout also needs an attribute-
       * fetching prolog keyed from that layout, which does not exist
       * yet: the static_vi prolog only fixes up vertex/instance IDs.
       */
      /* D3D12 has no point size: a point is always one pixel. AGX reads
       * the varying regardless, so a shader that never writes it
       * rasterises points at whatever was left there -- measured as
       * either nothing at all or the entire render target. hk lowers the
       * same default for its hardware vertex stage (hk_shader.c:1135).
       */
      NIR_PASS(_, nir, nir_lower_default_point_size);

      /*
       * With a real input layout the shader FETCHES its attributes, and
       * the prolog goes back to being nothing but a vertex-ID fixup.
       *
       * The alternative -- agx_nir_lower_vs_input_to_prolog plus an
       * attribute-fetching prolog -- is what Honeykrisp does when its
       * pipeline is built in pieces, and it is not available to us: that
       * prolog answers load_vbo_base_agx from AGX_ABI_VUNI_VBO_BASE,
       * uniform register 4*n counting from ZERO, which runs straight
       * through D12_UNIFORM_ROOT at 4. hk takes this same monolithic path
       * whenever it can (hk_shader.c, !use_prolog).
       *
       * Strides are DYNAMIC because D3D12 puts StrideInBytes in the
       * VERTEX BUFFER VIEW, which is draw state, not in the input layout,
       * which is pipeline state. Baking it into the key would mean
       * recompiling whenever an application rebinds the same geometry
       * with a different stride.
       */
      if (fetch && fetch->nr) {
         struct agx_robustness rs = {
            .soft_fault = agx_has_soft_fault(&device->dev),
            .level = AGX_ROBUSTNESS_D3D,
         };

         NIR_PASS(_, nir, agx_nir_lower_vbo, fetch->keys, rs, true);
         NIR_PASS(_, nir, d12_dxil_lower_vbo_params);
      } else {
         NIR_PASS(_, nir, agx_nir_lower_vs_input_to_prolog);
      }

      NIR_PASS(_, nir, nir_lower_io_to_scalar, nir_var_shader_out, NULL,
               NULL);
      NIR_PASS(_, nir, agx_nir_lower_cull_distance_vs);
      if (cull_distance_size)
         *cull_distance_size = nir->info.cull_distance_array_size;
      NIR_PASS(_, nir, agx_nir_lower_uvs, uvs);

      /* Indexed output stores read their UVS index through
       * load_uvs_index_agx, which the driver must lower. Honeykrisp
       * defers it to draw time through its root descriptor because
       * linking happens there; our assignment is deterministic from the
       * unlinked layout (agx_assign_uvs with the same zero masks the PSO
       * uses), so the index is a compile-time constant. The PSO-level
       * agx_assign_uvs later recomputes the identical layout.
       */
      struct agx_varyings_vs linked = {0};
      agx_assign_uvs(&linked, uvs, 0, 0);
      NIR_PASS(_, nir, d12_dxil_lower_uvs_index, linked.slots);
   }

   struct agx_shader_key key = {
      .dev = agx_gather_device_key(&device->dev),
      /* Allocas live in per-thread scratch, and large shaders spill. */
      .has_scratch = true,
      /* Constant promotion is still outside this texture-binding change;
       * see the validation note on the compute key above. */
      .promote_constants = false,
      /* Honeykrisp's proven path promotes texture descriptors into a
       * preamble. This avoids leaving a bindless texture-handle load in the
       * fragment main. The compiled part's preamble must be retained below;
       * agx_fast_link intentionally copies only main/prolog/epilog. */
      .promote_textures = true,
      .reserved_preamble = D12_UNIFORM_RESERVED,
      /* A split fragment main falls through into its epilog. All other
       * programs are self-contained and terminate normally. */
      .no_stop = stage == MESA_SHADER_FRAGMENT && split_fs,
   };

   memset(out, 0, sizeof(*out));
   agx_compile_shader_nir(nir, &key, out);
   out->info.sampler_state_count =
      MAX2(out->info.sampler_state_count, runtime_samplers);
   d12_report_scratch("shader", &out->info);

   if (getenv("D3D12AGX_DUMP_SHADERS") && out->binary) {
      fprintf(stderr, "; --- %s stage, push_count %u, preamble %d ---\n",
              _mesa_shader_stage_to_string(stage), out->info.push_count,
              out->info.has_preamble);
      if (out->info.has_preamble) {
         fprintf(stderr, "; preamble +0x%x\n", out->info.preamble_offset);
         agx2_disassemble(out->binary + out->info.preamble_offset,
                          out->info.main_offset - out->info.preamble_offset,
                          stderr);
      }
      fprintf(stderr, "; main +0x%x\n", out->info.main_offset);
      agx2_disassemble(out->binary + out->info.main_offset,
                       out->info.binary_size - out->info.main_offset, stderr);
   }

   return out->binary && out->info.binary_size;
}

/*
 * Finish one of the programs poly_nir_lower_gs produced. `uvs` non-NULL
 * marks the RASTERIZATION program, which is a hardware vertex shader and
 * therefore needs the unified-vertex-store lowering; the others are
 * compute.
 */
static bool
compile_poly_program(struct d12_device *device, nir_shader *nir,
                     struct agx_unlinked_uvs_layout *uvs,
                     struct agx_shader_part *out)
{
   NIR_PASS(_, nir, agx_nir_lower_texture);
   agx_preprocess_nir(nir);
   NIR_PASS(_, nir, d12_dxil_lower_root);

   if (uvs) {
      /* This program is the HARDWARE vertex stage, so it needs the whole
       * hardware-VS lowering sequence, not just the unified vertex
       * store. hk_lower_hw_vs (hk_shader.c:1045) is the reference and the
       * order is load-bearing: agx_nir_lower_uvs runs last, over scalar
       * outputs. Handing it vec4 stores -- which is what the geometry
       * rasterization program emits -- produced a render command that
       * died outright, leaving the target not even cleared.
       */
      NIR_PASS(_, nir, nir_lower_default_point_size);
      NIR_PASS(_, nir, nir_lower_point_size, 1.0f, 511.95f);
      NIR_PASS(_, nir, nir_lower_io_to_scalar, nir_var_shader_out, NULL,
               NULL);
      NIR_PASS(_, nir, agx_nir_lower_cull_distance_vs);

      NIR_PASS(_, nir, agx_nir_lower_uvs, uvs);

      struct agx_varyings_vs linked = {0};
      agx_assign_uvs(&linked, uvs, 0, 0);
      NIR_PASS(_, nir, d12_dxil_lower_uvs_index, linked.slots);
   }

   struct agx_shader_key key = {
      .dev = agx_gather_device_key(&device->dev),
      .has_scratch = true,
      .promote_constants = false,
      .reserved_preamble = D12_UNIFORM_RESERVED,
      .no_stop = false,
   };

   memset(out, 0, sizeof(*out));
   agx_compile_shader_nir(nir, &key, out);
   d12_report_scratch("shader", &out->info);

   return out->binary && out->info.binary_size;
}

/* Build NIR's byte-addressed transform-feedback description from D3D12's
 * semantic declaration. D3D12 packs declaration entries per OutputSlot in
 * declaration order; a NULL semantic is an explicit gap. */
static nir_xfb_info *
d12_build_xfb_info(void *mem_ctx,
                   const D3D12_STREAM_OUTPUT_DESC *so,
                   const void *bytecode, size_t bytecode_size,
                   bool *valid)
{
   *valid = false;
   if (!so || !so->NumEntries) {
      *valid = true;
      return NULL;
   }

   if (!so->pSODeclaration || so->NumEntries > 64 ||
       so->NumStrides > NIR_MAX_XFB_BUFFERS ||
       (so->NumStrides && !so->pBufferStrides) ||
       (so->RasterizedStream != D3D12_SO_NO_RASTERIZED_STREAM &&
        so->RasterizedStream >= NIR_MAX_XFB_STREAMS))
      return NULL;

   struct d12_input_sig_elem sig[D12_MAX_SIG_ELEMENTS];
   bool dxbc = d12_dxbc_is_dxbc_shader(bytecode, bytecode_size);
   unsigned nr_sig =
      dxbc ? d12_dxbc_output_signature(bytecode, bytecode_size, sig,
                                       ARRAY_SIZE(sig))
           : d12_dxil_output_signature(bytecode, bytecode_size, sig,
                                       ARRAY_SIZE(sig));
   if (!nr_sig)
      return NULL;

   unsigned output_count = 0;
   for (unsigned i = 0; i < so->NumEntries; i++)
      output_count += so->pSODeclaration[i].SemanticName != NULL;
   if (!output_count)
      return NULL;

   nir_xfb_info *xfb =
      rzalloc_size(mem_ctx, nir_xfb_info_size(output_count));
   if (!xfb)
      return NULL;

   uint16_t packed_offset[NIR_MAX_XFB_BUFFERS] = {0};
   bool buffer_seen[NIR_MAX_XFB_BUFFERS] = {0};

   for (unsigned i = 0; i < so->NumEntries; i++) {
      const D3D12_SO_DECLARATION_ENTRY *entry = &so->pSODeclaration[i];
      if (entry->Stream >= NIR_MAX_XFB_STREAMS ||
          entry->OutputSlot >= NIR_MAX_XFB_BUFFERS ||
          entry->StartComponent > 3 || !entry->ComponentCount ||
          entry->StartComponent + entry->ComponentCount > 4 ||
          entry->OutputSlot >= so->NumStrides)
         goto fail;

      unsigned buffer = entry->OutputSlot;
      unsigned bytes = entry->ComponentCount * 4;
      if (packed_offset[buffer] > UINT16_MAX - bytes)
         goto fail;

      if (!entry->SemanticName) {
         packed_offset[buffer] += bytes;
         continue;
      }

      const struct d12_input_sig_elem *match = NULL;
      for (unsigned s = 0; s < nr_sig; s++) {
         if (sig[s].semantic_index == entry->SemanticIndex &&
             !strcasecmp(sig[s].name, entry->SemanticName)) {
            match = &sig[s];
            break;
         }
      }
      if (!match || match->id >= 32 ||
          match->start_component + entry->StartComponent +
                entry->ComponentCount >
             4)
         goto fail;

      if (buffer_seen[buffer] &&
          xfb->buffer_to_stream[buffer] != entry->Stream)
         goto fail;

      unsigned component = match->start_component + entry->StartComponent;
      unsigned location;
      if (!strcasecmp(match->name, "SV_Position")) {
         location = VARYING_SLOT_POS;
      } else if (dxbc && !strcasecmp(match->name, "SV_ClipDistance")) {
         location = VARYING_SLOT_CLIP_DIST0 + match->semantic_index;
      } else {
         location = VARYING_SLOT_VAR0 + match->id;
      }

      nir_xfb_output_info *out = &xfb->outputs[xfb->output_count++];
      *out = (nir_xfb_output_info){
         .buffer = buffer,
         .offset = packed_offset[buffer],
         .location = location,
         .component_mask = BITFIELD_RANGE(component, entry->ComponentCount),
         .component_offset = component,
      };

      xfb->buffers[buffer].varying_count++;
      xfb->buffers_written |= BITFIELD_BIT(buffer);
      xfb->streams_written |= BITFIELD_BIT(entry->Stream);
      xfb->buffer_to_stream[buffer] = entry->Stream;
      buffer_seen[buffer] = true;
      packed_offset[buffer] += bytes;
   }

   for (unsigned b = 0; b < so->NumStrides; b++) {
      UINT stride = so->pBufferStrides[b];
      if ((stride & 3) || stride > UINT16_MAX || packed_offset[b] > stride)
         goto fail;
      xfb->buffers[b].stride = stride;
   }

   *valid = true;
   return xfb;

fail:
   ralloc_free(xfb);
   return NULL;
}

static enum mesa_prim
d12_so_primitive(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
{
   switch (type) {
   case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:    return MESA_PRIM_POINTS;
   case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:     return MESA_PRIM_LINES;
   case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE: return MESA_PRIM_TRIANGLES;
   default:                                     return MESA_PRIM_UNKNOWN;
   }
}

/* MIT-derived from Honeykrisp's passthrough-GS builder. The DXBC/DXIL
 * front ends use intrinsic I/O rather than variables, so Mesa's generic
 * variable-based nir_create_passthrough_gs cannot preserve their slots. */
static nir_shader *
d12_build_so_passthrough_gs(uint64_t outputs, uint8_t clip_size,
                            uint8_t cull_size, enum mesa_prim prim,
                            const nir_xfb_info *xfb)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_GEOMETRY, &agx_nir_options, "d12 stream-output passthrough");
   nir_shader *s = b.shader;

   enum mesa_prim out_prim = prim == MESA_PRIM_POINTS
                                ? MESA_PRIM_POINTS
                                : prim == MESA_PRIM_LINES
                                     ? MESA_PRIM_LINE_STRIP
                                     : MESA_PRIM_TRIANGLE_STRIP;
   unsigned vertices = mesa_vertices_per_prim(prim);

   s->info.inputs_read = outputs;
   s->info.outputs_written = outputs;
   s->info.clip_distance_array_size = clip_size;
   s->info.cull_distance_array_size = cull_size;
   s->info.gs.input_primitive = prim;
   s->info.gs.output_primitive = out_prim;
   s->info.gs.vertices_in = vertices;
   s->info.gs.vertices_out = vertices;
   s->info.gs.invocations = 1;
   s->info.gs.active_stream_mask = 1;

   if (xfb) {
      s->xfb_info = ralloc_memdup(s, xfb,
                                  nir_xfb_info_size(xfb->output_count));
      s->info.has_transform_feedback_varyings = true;
      for (unsigned i = 0; i < NIR_MAX_XFB_BUFFERS; i++)
         s->info.xfb_stride[i] = xfb->buffers[i].stride / 4;
   }

   nir_def *zero = nir_imm_int(&b, 0);
   nir_def *one = nir_imm_int(&b, 1);
   for (unsigned v = 0; v < vertices; v++) {
      nir_def *vertex = nir_imm_int(&b, v);

      u_foreach_bit64(loc, outputs) {
         unsigned adjusted_loc = loc;
         nir_def *offset = zero;
         unsigned num_slots = 1;
         bool scalar = loc == VARYING_SLOT_LAYER ||
                       loc == VARYING_SLOT_VIEW_INDEX ||
                       loc == VARYING_SLOT_VIEWPORT ||
                       loc == VARYING_SLOT_PSIZ;
         unsigned comps = scalar ? 1 : 4;

         if (loc == VARYING_SLOT_CLIP_DIST1 ||
             loc == VARYING_SLOT_CULL_DIST1) {
            adjusted_loc--;
            offset = one;
         }
         if (adjusted_loc == VARYING_SLOT_CLIP_DIST0 ||
             adjusted_loc == VARYING_SLOT_CULL_DIST0) {
            num_slots = clip_size + cull_size;
            comps = loc > adjusted_loc ? num_slots - 4 : MIN2(num_slots, 4);
         }

         nir_io_semantics sem = {
            .location = adjusted_loc,
            .num_slots = num_slots,
         };
         nir_def *value = nir_load_per_vertex_input(
            &b, comps, 32, vertex, offset, .io_semantics = sem);
         for (unsigned c = 0; c < comps; c++) {
            nir_store_output(&b, nir_channel(&b, value, c), offset,
                             .io_semantics = sem,
                             .src_type = nir_type_uint32, .component = c);
         }
      }
      nir_emit_vertex(&b, 0);
   }

   return s;
}

/*
 * A geometry shader expands into the main body and rasterization shader,
 * plus count/pre-GS programs when transform feedback needs them. The draw
 * path dispatches the latter only for XFB, matching hk_cmd_draw.c.
 */
static bool
compile_gs_nir(struct d12_device *device, nir_shader *nir,
               struct poly_gs_info *gs_info,
               struct agx_unlinked_uvs_layout *rast_uvs,
               struct agx_shader_part *count_out,
               struct agx_shader_part *pre_out,
               struct agx_shader_part *main_out,
               struct agx_shader_part *rast_out)
{
   nir_shader *gs_count = NULL, *gs_copy = NULL, *pre_gs = NULL;
   NIR_PASS(_, nir, poly_nir_lower_gs, &gs_count, &gs_copy, &pre_gs, gs_info);

   if (gs_info->shape < POLY_GS_SHAPE_DYNAMIC_INDEXED ||
       gs_info->shape > POLY_GS_SHAPE_STATIC_PER_INSTANCE) {
      mesa_loge("graphics PSO: geometry lowering produced invalid "
                "rasterization shape %d", gs_info->shape);
      return false;
   }

   if (!gs_copy) {
      mesa_loge("graphics PSO: geometry lowering produced no rasterization "
                "program");
      return false;
   }

   memset(count_out, 0, sizeof(*count_out));
   memset(pre_out, 0, sizeof(*pre_out));
   if (gs_count && !compile_poly_program(device, gs_count, NULL, count_out))
      return false;
   if (pre_gs && !compile_poly_program(device, pre_gs, NULL, pre_out))
      return false;
   if (gs_info->xfb && (!pre_gs || !pre_out->binary)) {
      mesa_loge("graphics PSO: transform feedback produced no pre-GS program");
      return false;
   }

   if (!compile_poly_program(device, nir, NULL, main_out))
      return false;

   return compile_poly_program(device, gs_copy, rast_uvs, rast_out);
}

static bool
compile_gs_stage(struct d12_device *device, const struct d12_binding_map *map,
                 const void *dxil, size_t dxil_size,
                 const nir_xfb_info *xfb,
                 D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type,
                 const struct d12_pipeline_state *pso,
                 struct poly_gs_info *gs_info,
                 struct agx_unlinked_uvs_layout *rast_uvs,
                 struct agx_shader_part *count_out,
                 struct agx_shader_part *pre_out,
                 struct agx_shader_part *main_out,
                 struct agx_shader_part *rast_out)
{
   nir_shader *nir =
      dxil_to_nir_stage(map, dxil, dxil_size, MESA_SHADER_GEOMETRY);
   if (!nir)
      return false;

   enum mesa_prim input = nir->info.gs.input_primitive;
   bool compatible = false;
   if (pso->has_tess) {
      /* Tessellation produces points, lines (isolines), or triangles.
       * Unlike an IA topology, its output never carries adjacency. */
      enum mesa_prim tess_output =
         pso->tess_points ? MESA_PRIM_POINTS
         : pso->tess_mode == TESS_PRIMITIVE_ISOLINES ? MESA_PRIM_LINES
                                                     : MESA_PRIM_TRIANGLES;
      compatible = input == tess_output;
   } else {
      switch (primitive_topology_type) {
      case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
         compatible = input == MESA_PRIM_POINTS;
         break;
      case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
         compatible = input == MESA_PRIM_LINES ||
                      input == MESA_PRIM_LINES_ADJACENCY;
         break;
      case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
         compatible = input == MESA_PRIM_TRIANGLES ||
                      input == MESA_PRIM_TRIANGLES_ADJACENCY;
         break;
      default:
         compatible = false;
         break;
      }
   }

   if (!compatible) {
      mesa_loge("graphics PSO: geometry input primitive %u is incompatible "
                "with topology type %u%s", input, primitive_topology_type,
                pso->has_tess ? " after tessellation" : "");
      ralloc_free(nir);
      return false;
   }

   if (xfb) {
      nir->xfb_info = ralloc_memdup(nir, xfb,
                                    nir_xfb_info_size(xfb->output_count));
      nir->info.has_transform_feedback_varyings = true;
      for (unsigned i = 0; i < NIR_MAX_XFB_BUFFERS; i++)
         nir->info.xfb_stride[i] = xfb->buffers[i].stride / 4;
   }

   return compile_gs_nir(device, nir, gs_info, rast_uvs, count_out, pre_out,
                         main_out, rast_out);
}

/*
 * The hull shader. It runs as compute, one workgroup per patch, writing
 * per-vertex and per-patch outputs into the TCS buffer. Everything the
 * runtime needs about the patch layout has to be gathered BEFORE the
 * lowering, which rewrites the output stores into global stores and
 * takes the shader's tess info with it.
 */
static bool
compile_tcs_stage(struct d12_device *device, const struct d12_binding_map *map,
                  const void *dxil, size_t dxil_size,
                  struct d12_pipeline_state *pso,
                  struct agx_shader_part *out)
{
   nir_shader *nir =
      dxil_to_nir_stage(map, dxil, dxil_size, MESA_SHADER_TESS_CTRL);
   if (!nir)
      return false;

   pso->tess_ccw |= nir->info.tess.ccw;
   pso->tess_points |= nir->info.tess.point_mode;
   if (nir->info.tess.spacing)
      pso->tess_spacing = nir->info.tess.spacing;
   if (nir->info.tess._primitive_mode)
      pso->tess_mode = nir->info.tess._primitive_mode;

   pso->tcs_output_patch_size = nir->info.tess.tcs_vertices_out;
   pso->tcs_per_vertex_outputs = poly_tcs_per_vertex_outputs(nir);
   pso->tcs_nr_patch_outputs = util_last_bit(nir->info.patch_outputs_written);
   pso->tcs_output_stride = poly_tcs_output_stride(nir);

   /* DXBC fork/join phases can require more invocations than there are output
    * control points. The translator records the largest phase width; DXIL has
    * no phase-width metadata here and therefore falls back to one invocation
    * per output control point. */
   nir->info.workgroup_size[0] =
      MAX2(nir->info.workgroup_size[0], pso->tcs_output_patch_size);
   nir->info.workgroup_size[1] = 1;
   nir->info.workgroup_size[2] = 1;

   NIR_PASS(_, nir, poly_nir_lower_tcs);
   nir->info.stage = MESA_SHADER_COMPUTE;
   memset(&nir->info.cs, 0, sizeof(nir->info.cs));

   return compile_poly_program(device, nir, NULL, out);
}

/*
 * The domain shader, as the HARDWARE vertex shader: the tessellator
 * produces an index buffer and the hardware invokes this once per index,
 * which is what poly_nir_lower_tes(to_hw_vs = true) sets up.
 */
static bool
compile_tes_stage(struct d12_device *device, const struct d12_binding_map *map,
                  const void *dxil, size_t dxil_size,
                  struct d12_pipeline_state *pso,
                  struct agx_unlinked_uvs_layout *uvs,
                  struct agx_shader_part *out)
{
   nir_shader *nir =
      dxil_to_nir_stage(map, dxil, dxil_size, MESA_SHADER_TESS_EVAL);
   if (!nir)
      return false;

   /* Gather before lowering: it destroys info. */
   pso->tess_ccw |= nir->info.tess.ccw;
   pso->tess_points |= nir->info.tess.point_mode;
   if (nir->info.tess.spacing)
      pso->tess_spacing = nir->info.tess.spacing;
   if (nir->info.tess._primitive_mode)
      pso->tess_mode = nir->info.tess._primitive_mode;

   NIR_PASS(_, nir, poly_nir_lower_tes, true);

   return compile_poly_program(device, nir, uvs, out);
}

/*
 * The domain shader immediately ahead of a geometry/XFB stage.  The
 * tessellator still produces an indexed indirect draw, but instead of feeding
 * the hardware vertex stage directly, the domain shader runs as compute and
 * writes the ordinary poly vertex-output buffer consumed by the geometry
 * stage.  This is the same tessellation -> software TES -> GS composition
 * used by Honeykrisp.
 */
static bool
compile_sw_tes_stage(struct d12_device *device,
                     const struct d12_binding_map *map, const void *dxil,
                     size_t dxil_size, struct agx_shader_part *out,
                     uint64_t *out_outputs, uint8_t *out_clip_size,
                     uint8_t *out_cull_size)
{
   nir_shader *nir =
      dxil_to_nir_stage(map, dxil, dxil_size, MESA_SHADER_TESS_EVAL);
   if (!nir)
      return false;

   NIR_PASS(_, nir, poly_nir_lower_tes, false);

   *out_outputs = nir->info.outputs_written;
   if (out_clip_size)
      *out_clip_size = nir->info.clip_distance_array_size;
   if (out_cull_size)
      *out_cull_size = nir->info.cull_distance_array_size;

   NIR_PASS(_, nir, poly_nir_lower_vs_before_gs);
   nir->info.stage = MESA_SHADER_COMPUTE;
   memset(&nir->info.cs, 0, sizeof(nir->info.cs));
   nir->xfb_info = NULL;
   nir->info.workgroup_size[0] = 64;
   nir->info.workgroup_size[1] = 1;
   nir->info.workgroup_size[2] = 1;

   return compile_poly_program(device, nir, NULL, out);
}

/*
 * The vertex shader ahead of a geometry shader does not feed the
 * rasterizer -- it writes its outputs to a buffer the GS reads. That
 * makes it a compute shader, and poly_nir_lower_vs_before_gs is what
 * rewrites its stores into that layout. It must run before
 * agx_preprocess_nir, like every other poly lowering.
 */
static bool
compile_sw_vs_stage(struct d12_device *device,
                    const struct d12_binding_map *map, const void *dxil,
                    size_t dxil_size, const struct d12_vs_fetch *fetch,
                    struct agx_shader_part *out, uint64_t *out_outputs,
                    uint8_t *out_clip_size, uint8_t *out_cull_size)
{
   nir_shader *nir =
      dxil_to_nir_stage(map, dxil, dxil_size, MESA_SHADER_VERTEX);
   if (!nir)
      return false;

   /* CAPTURE THE OUTPUT MASK FIRST. The lowering rewrites every
    * store_output into a global store, so after it runs the shader has no
    * NIR outputs at all and agx_shader_info::outputs reports 0. That mask
    * is not decoration: the geometry program derives EVERY vertex address
    * in the output buffer from it, so a zero mask sends every vertex to
    * the same address and the rasterization program reads a position of
    * (0,0,0,0) -- w=0, which rasterises as the entire render target.
    * hk captures it in the same place, before the branch
    * (hk_shader.c:1138).
    */
   *out_outputs = nir->info.outputs_written;
   if (out_clip_size)
      *out_clip_size = nir->info.clip_distance_array_size;
   if (out_cull_size)
      *out_cull_size = nir->info.cull_distance_array_size;

   /*
    * VERTEX ATTRIBUTES, and this has to happen while the shader is still
    * a VERTEX shader -- agx_nir_lower_vbo asserts on the stage, and the
    * switch to compute is a few lines below.
    *
    * Skipping it is why every geometry and tessellation test that read a
    * real vertex attribute aborted the process with "Unhandled intrinsic
    * load_input": the hardware VS path grew attribute fetch in G10, and
    * the SOFTWARE VS that a GS or tessellation pipeline substitutes for
    * it did not. Our own g6/g7 probes never caught it because they
    * derive their geometry from SV_VertexID.
    *
    * The emitted load_vertex_id is resolved further down by
    * d12_dxil_lower_sw_vs, which turns it into the dispatch's invocation
    * index pulled through the index buffer.
    */
   if (fetch && fetch->nr) {
      struct agx_robustness rs = {
         .soft_fault = agx_has_soft_fault(&device->dev),
         .level = AGX_ROBUSTNESS_D3D,
      };

      NIR_PASS(_, nir, agx_nir_lower_vbo, fetch->keys, rs, true);
      NIR_PASS(_, nir, d12_dxil_lower_vbo_params);
   }

   NIR_PASS(_, nir, poly_nir_lower_vs_before_gs);

   /* The pass does NOT rewrite the stage -- the caller must, and the AGX
    * backend asserts on it ("only for SW VS"). hk does exactly this
    * immediately after the same pass (hk_shader.c:1143-1147).
    */
   nir->info.stage = MESA_SHADER_COMPUTE;
   memset(&nir->info.cs, 0, sizeof(nir->info.cs));
   nir->xfb_info = NULL;

   /* The launch programs a local size of 64; say so, so the backend's
    * thread-index maths agrees with what the hardware is told. */
   nir->info.workgroup_size[0] = 64;
   nir->info.workgroup_size[1] = 1;
   nir->info.workgroup_size[2] = 1;

   /* A compute shader has no vertex ID. poly_nir_lower_sw_vs is what
    * turns load_vertex_id into global_invocation_id.x (pulling through
    * the index buffer and adding the base vertex) and load_instance_id
    * into .y. Without it the shader keeps asking for a sysval that does
    * not exist in a dispatch, every thread computes the same address,
    * and the vertex output buffer stays zero -- which then rasterises as
    * a position of (0,0,0,0).
    *
    * It emits load_index_size_poly / load_vertex_param_buffer_poly /
    * load_first_vertex, so it has to run before d12_dxil_lower_root
    * resolves those.
    */
   NIR_PASS(_, nir, d12_dxil_lower_sw_vs);

   return compile_poly_program(device, nir, NULL, out);
}

/*
 * D3D12 comparison functions map one-for-one onto the AGX ZS functions, but
 * write the mapping out rather than relying on the constant offset: a silent
 * off-by-one here means NEVER where ALWAYS was asked for, which rejects every
 * fragment and looks like a missing draw rather than a wrong enum.
 */
static enum agx_zs_func
d12_translate_compare(D3D12_COMPARISON_FUNC func)
{
   switch (func) {
   case D3D12_COMPARISON_FUNC_NEVER:         return AGX_ZS_FUNC_NEVER;
   case D3D12_COMPARISON_FUNC_LESS:          return AGX_ZS_FUNC_LESS;
   case D3D12_COMPARISON_FUNC_EQUAL:         return AGX_ZS_FUNC_EQUAL;
   case D3D12_COMPARISON_FUNC_LESS_EQUAL:    return AGX_ZS_FUNC_LEQUAL;
   case D3D12_COMPARISON_FUNC_GREATER:       return AGX_ZS_FUNC_GREATER;
   case D3D12_COMPARISON_FUNC_NOT_EQUAL:     return AGX_ZS_FUNC_NOT_EQUAL;
   case D3D12_COMPARISON_FUNC_GREATER_EQUAL: return AGX_ZS_FUNC_GEQUAL;
   case D3D12_COMPARISON_FUNC_ALWAYS:        return AGX_ZS_FUNC_ALWAYS;
   default:
      /* Includes _NONE (0), which D3D12 uses to mean "no test". */
      return AGX_ZS_FUNC_ALWAYS;
   }
}

static enum agx_stencil_op
d12_translate_stencil_op(D3D12_STENCIL_OP op)
{
   switch (op) {
   case D3D12_STENCIL_OP_KEEP:     return AGX_STENCIL_OP_KEEP;
   case D3D12_STENCIL_OP_ZERO:     return AGX_STENCIL_OP_ZERO;
   case D3D12_STENCIL_OP_REPLACE:  return AGX_STENCIL_OP_REPLACE;
   case D3D12_STENCIL_OP_INCR_SAT: return AGX_STENCIL_OP_INCR_SAT;
   case D3D12_STENCIL_OP_DECR_SAT: return AGX_STENCIL_OP_DECR_SAT;
   case D3D12_STENCIL_OP_INVERT:   return AGX_STENCIL_OP_INVERT;
   case D3D12_STENCIL_OP_INCR:     return AGX_STENCIL_OP_INCR_WRAP;
   case D3D12_STENCIL_OP_DECR:     return AGX_STENCIL_OP_DECR_WRAP;
   default:                        return AGX_STENCIL_OP_KEEP;
   }
}

static void
d12_translate_stencil_face(struct d12_stencil_face *out,
                           const D3D12_DEPTH_STENCILOP_DESC *in)
{
   out->compare = d12_translate_compare(in->StencilFunc);
   out->pass = d12_translate_stencil_op(in->StencilPassOp);
   out->depth_fail = d12_translate_stencil_op(in->StencilDepthFailOp);
   out->stencil_fail = d12_translate_stencil_op(in->StencilFailOp);
}

static bool
d12_translate_blend_factor(D3D12_BLEND in, enum pipe_blendfactor *out)
{
   switch (in) {
   case D3D12_BLEND_ZERO:             *out = PIPE_BLENDFACTOR_ZERO; break;
   case D3D12_BLEND_ONE:              *out = PIPE_BLENDFACTOR_ONE; break;
   case D3D12_BLEND_SRC_COLOR:        *out = PIPE_BLENDFACTOR_SRC_COLOR; break;
   case D3D12_BLEND_INV_SRC_COLOR:    *out = PIPE_BLENDFACTOR_INV_SRC_COLOR; break;
   case D3D12_BLEND_SRC_ALPHA:        *out = PIPE_BLENDFACTOR_SRC_ALPHA; break;
   case D3D12_BLEND_INV_SRC_ALPHA:    *out = PIPE_BLENDFACTOR_INV_SRC_ALPHA; break;
   case D3D12_BLEND_DEST_ALPHA:       *out = PIPE_BLENDFACTOR_DST_ALPHA; break;
   case D3D12_BLEND_INV_DEST_ALPHA:   *out = PIPE_BLENDFACTOR_INV_DST_ALPHA; break;
   case D3D12_BLEND_DEST_COLOR:       *out = PIPE_BLENDFACTOR_DST_COLOR; break;
   case D3D12_BLEND_INV_DEST_COLOR:   *out = PIPE_BLENDFACTOR_INV_DST_COLOR; break;
   case D3D12_BLEND_SRC_ALPHA_SAT:    *out = PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE; break;
   case D3D12_BLEND_BLEND_FACTOR:     *out = PIPE_BLENDFACTOR_CONST_COLOR; break;
   case D3D12_BLEND_INV_BLEND_FACTOR: *out = PIPE_BLENDFACTOR_INV_CONST_COLOR; break;
   case D3D12_BLEND_SRC1_COLOR:       *out = PIPE_BLENDFACTOR_SRC1_COLOR; break;
   case D3D12_BLEND_INV_SRC1_COLOR:   *out = PIPE_BLENDFACTOR_INV_SRC1_COLOR; break;
   case D3D12_BLEND_SRC1_ALPHA:       *out = PIPE_BLENDFACTOR_SRC1_ALPHA; break;
   case D3D12_BLEND_INV_SRC1_ALPHA:   *out = PIPE_BLENDFACTOR_INV_SRC1_ALPHA; break;
   case D3D12_BLEND_ALPHA_FACTOR:     *out = PIPE_BLENDFACTOR_CONST_ALPHA; break;
   case D3D12_BLEND_INV_ALPHA_FACTOR: *out = PIPE_BLENDFACTOR_INV_CONST_ALPHA; break;
   default:
      return false;
   }

   return true;
}

static bool
d12_blend_factor_uses_src1(D3D12_BLEND factor)
{
   switch (factor) {
   case D3D12_BLEND_SRC1_COLOR:
   case D3D12_BLEND_INV_SRC1_COLOR:
   case D3D12_BLEND_SRC1_ALPHA:
   case D3D12_BLEND_INV_SRC1_ALPHA:
      return true;
   default:
      return false;
   }
}

static bool
d12_translate_blend_op(D3D12_BLEND_OP in, enum pipe_blend_func *out)
{
   switch (in) {
   case D3D12_BLEND_OP_ADD:          *out = PIPE_BLEND_ADD; break;
   case D3D12_BLEND_OP_SUBTRACT:     *out = PIPE_BLEND_SUBTRACT; break;
   case D3D12_BLEND_OP_REV_SUBTRACT: *out = PIPE_BLEND_REVERSE_SUBTRACT; break;
   case D3D12_BLEND_OP_MIN:          *out = PIPE_BLEND_MIN; break;
   case D3D12_BLEND_OP_MAX:          *out = PIPE_BLEND_MAX; break;
   default:
      return false;
   }

   return true;
}

static bool
d12_translate_logic_op(D3D12_LOGIC_OP in, enum pipe_logicop *out)
{
   switch (in) {
   case D3D12_LOGIC_OP_CLEAR:         *out = PIPE_LOGICOP_CLEAR; break;
   case D3D12_LOGIC_OP_SET:           *out = PIPE_LOGICOP_SET; break;
   case D3D12_LOGIC_OP_COPY:          *out = PIPE_LOGICOP_COPY; break;
   case D3D12_LOGIC_OP_COPY_INVERTED: *out = PIPE_LOGICOP_COPY_INVERTED; break;
   case D3D12_LOGIC_OP_NOOP:          *out = PIPE_LOGICOP_NOOP; break;
   case D3D12_LOGIC_OP_INVERT:        *out = PIPE_LOGICOP_INVERT; break;
   case D3D12_LOGIC_OP_AND:           *out = PIPE_LOGICOP_AND; break;
   case D3D12_LOGIC_OP_NAND:          *out = PIPE_LOGICOP_NAND; break;
   case D3D12_LOGIC_OP_OR:            *out = PIPE_LOGICOP_OR; break;
   case D3D12_LOGIC_OP_NOR:           *out = PIPE_LOGICOP_NOR; break;
   case D3D12_LOGIC_OP_XOR:           *out = PIPE_LOGICOP_XOR; break;
   case D3D12_LOGIC_OP_EQUIV:         *out = PIPE_LOGICOP_EQUIV; break;
   case D3D12_LOGIC_OP_AND_REVERSE:   *out = PIPE_LOGICOP_AND_REVERSE; break;
   case D3D12_LOGIC_OP_AND_INVERTED:  *out = PIPE_LOGICOP_AND_INVERTED; break;
   case D3D12_LOGIC_OP_OR_REVERSE:    *out = PIPE_LOGICOP_OR_REVERSE; break;
   case D3D12_LOGIC_OP_OR_INVERTED:   *out = PIPE_LOGICOP_OR_INVERTED; break;
   default:
      return false;
   }

   return true;
}

/*
 * ms_setup: turn the mesh shader's GPU-written counts into an indirect
 * draw. The counts do not exist until the mesh shader has run, so the
 * CPU cannot fill this in -- the same reason tessellation needs its
 * prefix sum before the draw.
 *
 * Reads  { u32 vertices, u32 primitives } from the counts buffer.
 * Writes { index_count = primitives * 3, 1, 0, 0, 0 }.
 */
static nir_shader *
build_ms_setup(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "d12 mesh draw setup");
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.internal = false;

   nir_def *params = nir_load_preamble(&b, 1, 64, .base = D12_UNIFORM_MESH_PARAMS);

   nir_def *counts = nir_load_global(
      &b, 1, 64, nir_iadd_imm(&b, params, D12_MESH_OFF_COUNTS),
      .align_mul = 8);
   /* NOT the dispatch slot: an amplification shader writes its
    * DispatchMesh grid there, so the draw descriptor has its own.
    */
   nir_def *draw = nir_load_global(
      &b, 1, 64, nir_iadd_imm(&b, params, D12_MESH_OFF_DRAW), .align_mul = 8);

   nir_def *prims =
      nir_load_global(&b, 1, 32, nir_iadd_imm(&b, counts, 4), .align_mul = 4);

   /* A D3D12 mesh shader emits triangles: three indices per primitive. */
   nir_def *idx = nir_imul_imm(&b, prims, 3);

   nir_store_global(&b, idx, draw, .write_mask = 1, .align_mul = 4);
   nir_store_global(&b, nir_imm_int(&b, 1), nir_iadd_imm(&b, draw, 4),
                    .write_mask = 1, .align_mul = 4);
   for (unsigned i = 2; i < 5; i++) {
      nir_store_global(&b, nir_imm_int(&b, 0), nir_iadd_imm(&b, draw, i * 4),
                       .write_mask = 1, .align_mul = 4);
   }

   return b.shader;
}

/*
 * as_setup: the amplification shader hands off with DispatchMesh(x,y,z),
 * a THREAD GROUP count written to D12_MESH_OFF_DISPATCH. The mesh
 * dispatch that follows must be indirect off it, and an AGX indirect
 * grid is six words -- three THREAD counts then three local sizes -- so
 * the group-to-thread multiply cannot happen on the CPU.
 */
static nir_shader *
build_as_setup(const unsigned wg[3])
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "d12 amplification setup");
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.internal = false;

   nir_def *params =
      nir_load_preamble(&b, 1, 64, .base = D12_UNIFORM_MESH_PARAMS);
   nir_def *disp = nir_load_global(
      &b, 1, 64, nir_iadd_imm(&b, params, D12_MESH_OFF_DISPATCH),
      .align_mul = 8);
   nir_def *grid = nir_load_global(
      &b, 1, 64, nir_iadd_imm(&b, params, D12_MESH_OFF_GRID), .align_mul = 8);

   for (unsigned i = 0; i < 3; i++) {
      nir_def *groups = nir_load_global(
         &b, 1, 32, nir_iadd_imm(&b, disp, i * 4), .align_mul = 4);

      nir_store_global(&b, nir_imul_imm(&b, groups, wg[i]),
                       nir_iadd_imm(&b, grid, i * 4), .write_mask = 1,
                       .align_mul = 4);
      nir_store_global(&b, nir_imm_int(&b, wg[i]),
                       nir_iadd_imm(&b, grid, (3 + i) * 4), .write_mask = 1,
                       .align_mul = 4);
   }

   return b.shader;
}

/*
 * ms_rast: the hardware vertex shader for a mesh pipeline. The mesh
 * shader wrote per-vertex outputs as a flat array of vec4 slots; this
 * reads the slot for gl_VertexID and hands it to the rasterizer.
 *
 * Slot 0 is POSITION by DXIL's mesh signature convention; the remaining
 * live slots become VAR0.. varyings for the pixel shader.
 */
static nir_shader *
build_ms_rast(uint64_t outputs)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, &agx_nir_options, "d12 mesh rast");
   b.shader->info.internal = false;

   nir_def *params = nir_load_preamble(&b, 1, 64, .base = D12_UNIFORM_MESH_PARAMS);
   nir_def *verts = nir_load_global(
      &b, 1, 64, nir_iadd_imm(&b, params, D12_MESH_OFF_VERTICES),
      .align_mul = 8);

   nir_def *vid = nir_load_vertex_id(&b);

   /* The per-vertex stride is FIXED at D12_MESH_MAX_SLOTS slots, not the
    * live slot count -- that is what the translator emits for
    * storeVertexOutput, and the two have to agree. Striding by the live
    * count instead put vertex 1 at byte 16 while the shader wrote it at
    * byte 512, so only vertex 0 was ever read back.
    */
   unsigned nr_slots = MAX2(util_last_bit64(outputs), 1);
   nir_def *base = nir_iadd(
      &b, verts,
      nir_u2u64(&b, nir_imul_imm(&b, vid,
                                 D12_MESH_MAX_SLOTS * D12_MESH_SLOT_STRIDE)));

   for (unsigned slot = 0; slot < nr_slots; slot++) {
      /* Slot 0 is POSITION by the mesh output layout and is ALWAYS
       * present. The mask cannot be consulted for it: a mesh shader
       * writes its outputs with global stores, so its NIR
       * outputs_written is empty and gating on it emitted a shader that
       * wrote nothing but the default point size.
       */
      if (slot != 0 && !(outputs & BITFIELD64_BIT(slot)))
         continue;

      nir_def *v = nir_load_global(
         &b, 4, 32, nir_iadd_imm(&b, base, slot * D12_MESH_SLOT_STRIDE),
         .align_mul = 16);

      unsigned location = slot == 0 ? VARYING_SLOT_POS
                                    : VARYING_SLOT_VAR0 + (slot - 1);

      nir_store_output(&b, v, nir_imm_int(&b, 0), .base = slot,
                       .write_mask = 0xf, .src_type = nir_type_float32,
                       .io_semantics.location = location,
                       .io_semantics.num_slots = 1);

      b.shader->info.outputs_written |= BITFIELD64_BIT(location);
   }

   return b.shader;
}

HRESULT
d12_pipeline_state_create_mesh(struct d12_device *device, const void *ms_dxil,
                               size_t ms_size, const void *as_dxil,
                               size_t as_size, const void *ps_dxil,
                               size_t ps_size, enum pipe_format rt_format,
                               unsigned nr_rt,
                               const DXGI_FORMAT *rt_formats,
                               unsigned nr_samples, UINT sample_mask,
                               const D3D12_DEPTH_STENCIL_DESC *ds_desc,
                               DXGI_FORMAT dsv_format,
                               const D3D12_RASTERIZER_DESC *rs_desc,
                               const D3D12_BLEND_DESC *blend_desc,
                               const struct d12_binding_map *map, REFIID riid,
                               void **out)
{
   /* Build the pipeline the ordinary way first: the pixel shader, the
    * depth/stencil and rasterizer state and the tilebuffer layout are
    * all identical to a vertex pipeline. The MESH-specific programs are
    * then attached, and the rasterization VS replaces pso->vs.
    *
    * A mesh shader translates to plain compute writing through
    * D12_UNIFORM_MESH_PARAMS, so it needs no lowering of its own -- but
    * nothing generates its rasterization shader either, because there is
    * no poly emulation for mesh. Both internal programs are built here.
    */
   HRESULT hr = d12_pipeline_state_create_graphics(
      device, ms_dxil, ms_size, ps_dxil, ps_size, NULL, 0, NULL, 0, NULL, 0,
      rt_format, nr_rt, rt_formats, nr_samples, sample_mask, ds_desc,
      dsv_format, rs_desc, blend_desc, NULL, NULL,
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED,
      D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
      D3D12_PIPELINE_STATE_FLAG_NONE, map, riid, out);
   if (FAILED(hr))
      return hr;

   struct d12_pipeline_state *pso =
      d12_pso_from_iface((ID3D12PipelineState *)*out);

   /* The mesh shader itself, as compute. */
   struct agx_shader_part ms_part;
   nir_shader *ms = dxil_to_nir_stage(map, ms_dxil, ms_size,
                                      MESA_SHADER_COMPUTE);
   if (!ms) {
      ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
      *out = NULL;
      return E_INVALIDARG;
   }

   pso->ms_workgroup[0] = MAX2(ms->info.workgroup_size[0], 1);
   pso->ms_workgroup[1] = MAX2(ms->info.workgroup_size[1], 1);
   pso->ms_workgroup[2] = MAX2(ms->info.workgroup_size[2], 1);
   pso->ms_outputs = ms->info.outputs_written;

   if (!compile_poly_program(device, ms, NULL, &ms_part)) {
      ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
      *out = NULL;
      return E_INVALIDARG;
   }

   struct agx_shader_part setup_part;
   if (!compile_poly_program(device, build_ms_setup(), NULL, &setup_part)) {
      ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
      *out = NULL;
      return E_INVALIDARG;
   }

   const struct {
      struct agx_shader_part *part;
      struct agx_bo **bo;
      uint64_t *va;
      struct agx_shader_info *info;
      const char *name;
   } cs[] = {
      {&ms_part, &pso->ms_bo, &pso->ms_va, &pso->ms_info, "d3d12 mesh"},
      {&setup_part, &pso->ms_setup_bo, &pso->ms_setup_va, &pso->ms_setup_info,
       "d3d12 mesh setup"},
   };

   for (unsigned i = 0; i < ARRAY_SIZE(cs); i++) {
      *cs[i].bo = agx_bo_create(&device->dev, cs[i].part->info.binary_size, 0,
                                AGX_BO_EXEC | AGX_BO_LOW_VA, cs[i].name);
      if (!*cs[i].bo) {
         ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
         *out = NULL;
         return E_OUTOFMEMORY;
      }
      memcpy(agx_bo_map(*cs[i].bo), cs[i].part->binary,
             cs[i].part->info.binary_size);
      *cs[i].va = (*cs[i].bo)->va->addr;
      *cs[i].info = cs[i].part->info;
   }

   /* The rasterization vertex shader, linked like any other. */
   struct agx_shader_part rast_part;
   if (!compile_poly_program(device, build_ms_rast(pso->ms_outputs),
                             &pso->ms_uvs, &rast_part)) {
      ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
      *out = NULL;
      return E_INVALIDARG;
   }

   struct agx_vs_prolog_key pk = {.hw = true, .static_vi = true};
   struct agx_shader_key pbk = {
      .dev = agx_gather_device_key(&device->dev),
      .has_scratch = true,
      .secondary = true,
      .no_stop = true,
   };
   nir_builder pb = nir_builder_init_simple_shader(0, &agx_nir_options, NULL);
   agx_nir_vs_prolog(&pb, &pk);
   struct agx_shader_part prolog;
   memset(&prolog, 0, sizeof(prolog));
   agx_compile_shader_nir(pb.shader, &pbk, &prolog);
   ralloc_free(pb.shader);

   agx_fast_link(&pso->ms_rast.linked, &device->dev, false, &rast_part,
                 &prolog, NULL, 0);
   free(prolog.binary);
   pso->ms_rast.info = rast_part.info;
   agx_assign_uvs(&pso->ms_varyings, &pso->ms_uvs, 0, 0);

   /* Same preamble rule as every other linked graphics stage. */
   if (rast_part.info.has_preamble) {
      pso->ms_rast.code_bo =
         agx_bo_create(&device->dev, rast_part.info.binary_size, 0,
                       AGX_BO_EXEC | AGX_BO_LOW_VA, "d3d12 mesh rast");
      if (!pso->ms_rast.code_bo) {
         ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
         *out = NULL;
         return E_OUTOFMEMORY;
      }
      memcpy(agx_bo_map(pso->ms_rast.code_bo), rast_part.binary,
             rast_part.info.binary_size);
      pso->ms_rast.preamble_va =
         pso->ms_rast.code_bo->va->addr + rast_part.info.preamble_offset;
   }

   unsigned samplers = rast_part.info.sampler_state_count;
   if (rast_part.info.uses_txf)
      samplers = MAX2(samplers, 1);

   agx_pack(&pso->ms_rast.counts, COUNTS, cfg) {
      cfg.uniform_register_count = rast_part.info.push_count;
      cfg.preshader_register_count = rast_part.info.nr_preamble_gprs;
      cfg.sampler_state_register_count =
         agx_translate_sampler_state_count(samplers, false);
      cfg.texture_state_register_count = rast_part.info.texture_state_count;
   }

   /* The amplification shader, if there is one. */
   if (as_dxil && as_size) {
      nir_shader *as =
         dxil_to_nir_stage(map, as_dxil, as_size, MESA_SHADER_COMPUTE);
      if (!as) {
         ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
         *out = NULL;
         return E_INVALIDARG;
      }

      pso->as_workgroup[0] = MAX2(as->info.workgroup_size[0], 1);
      pso->as_workgroup[1] = MAX2(as->info.workgroup_size[1], 1);
      pso->as_workgroup[2] = MAX2(as->info.workgroup_size[2], 1);

      struct agx_shader_part as_part, as_setup_part;
      if (!compile_poly_program(device, as, NULL, &as_part) ||
          !compile_poly_program(device, build_as_setup(pso->ms_workgroup),
                                NULL, &as_setup_part)) {
         ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
         *out = NULL;
         return E_INVALIDARG;
      }

      const struct {
         struct agx_shader_part *part;
         struct agx_bo **bo;
         uint64_t *va;
         struct agx_shader_info *info;
         const char *name;
      } acs[] = {
         {&as_part, &pso->as_bo, &pso->as_va, &pso->as_info,
          "d3d12 amplification"},
         {&as_setup_part, &pso->as_setup_bo, &pso->as_setup_va,
          &pso->as_setup_info, "d3d12 amplification setup"},
      };

      for (unsigned i = 0; i < ARRAY_SIZE(acs); i++) {
         *acs[i].bo =
            agx_bo_create(&device->dev, acs[i].part->info.binary_size, 0,
                          AGX_BO_EXEC | AGX_BO_LOW_VA, acs[i].name);
         if (!*acs[i].bo) {
            ID3D12PipelineState_Release((ID3D12PipelineState *)*out);
            *out = NULL;
            return E_OUTOFMEMORY;
         }
         memcpy(agx_bo_map(*acs[i].bo), acs[i].part->binary,
                acs[i].part->info.binary_size);
         *acs[i].va = (*acs[i].bo)->va->addr;
         *acs[i].info = acs[i].part->info;
      }

      /* The payload size is declared in ASState, but the module is gone
       * by the time the shader is compiled. Allocate a fixed bound: the
       * D3D12 limit is 16 KB and the shader writes through the pointer
       * at D12_MESH_OFF_PAYLOAD, so over-allocating is safe and
       * under-allocating would corrupt whatever follows.
       */
      pso->as_payload_size = 16 * 1024;
      pso->has_amp = true;
      mesa_logi("  + amplification: as %u B, setup %u B, wg %ux%ux%u",
                as_part.info.binary_size, as_setup_part.info.binary_size,
                pso->as_workgroup[0], pso->as_workgroup[1],
                pso->as_workgroup[2]);
   }

   mesa_logi("compiled mesh pipeline: ms %u B, setup %u B, rast %u B, "
             "wg %ux%ux%u, outputs 0x%llx",
             ms_part.info.binary_size, setup_part.info.binary_size,
             rast_part.info.binary_size, pso->ms_workgroup[0],
             pso->ms_workgroup[1], pso->ms_workgroup[2],
             (unsigned long long)pso->ms_outputs);

   pso->has_mesh = true;
   return S_OK;
}

HRESULT
d12_pipeline_state_create_graphics(struct d12_device *device,
                                   const void *vs_dxil, size_t vs_size,
                                   const void *ps_dxil, size_t ps_size,
                                   const void *gs_dxil, size_t gs_size,
                                   const void *hs_dxil, size_t hs_size,
                                   const void *ds_dxil, size_t ds_size,
                                   enum pipe_format rt_format, unsigned nr_rt,
                                   const DXGI_FORMAT *rt_formats,
                                   unsigned nr_samples, UINT sample_mask,
                                   const D3D12_DEPTH_STENCIL_DESC *ds_desc,
                                   DXGI_FORMAT dsv_format,
                                   const D3D12_RASTERIZER_DESC *rs_desc,
                                   const D3D12_BLEND_DESC *blend_desc,
                                   const D3D12_INPUT_LAYOUT_DESC *input_layout,
                                   const D3D12_STREAM_OUTPUT_DESC *so_desc,
                                   D3D12_PRIMITIVE_TOPOLOGY_TYPE
                                      primitive_topology_type,
                                   D3D12_INDEX_BUFFER_STRIP_CUT_VALUE
                                      strip_cut,
                                   D3D12_PIPELINE_STATE_FLAGS flags,
                                   const struct d12_binding_map *map,
                                   REFIID riid, void **out)
{
   if (!out)
      return E_POINTER;
   if (!vs_dxil || !vs_size || (!ps_dxil != !ps_size))
      return E_INVALIDARG;
   if (strip_cut != D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED &&
       strip_cut != D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF &&
       strip_cut != D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF)
      return E_INVALIDARG;

   struct d12_pipeline_state *pso = calloc(1, sizeof(*pso));
   if (!pso)
      return E_OUTOFMEMORY;

   pso->has_pixel_shader = ps_dxil && ps_size;
   pso->strip_cut = strip_cut;
   if (!pso->has_pixel_shader) {
      /* D3D12 treats declared RTV state as inactive when there is no pixel
       * shader. Compile the empty hardware fragment stage against a
       * zero-colour tilebuffer; depth/stencil remains fully active.
       */
      rt_format = PIPE_FORMAT_NONE;
      nr_rt = 0;
      rt_formats = NULL;
      nr_samples = 1;
   }

   if (nr_samples != 1 && nr_samples != 2 && nr_samples != 4) {
      free(pso);
      return E_INVALIDARG;
   }

   if (nr_rt > AGX_MAX_RENDER_TARGETS) {
      free(pso);
      return E_INVALIDARG;
   }

   /* Depth/stencil state, resolved to hardware enums once here rather than
    * per draw. DSVFormat may legally be UNKNOWN while depth/stencil state is
    * still used with a view bound at draw time. The draw path already makes
    * that state inert when no DSV is actually attached.
    */
   pso->ds.dsv_format = dsv_format;
   pso->ds.depth_func = AGX_ZS_FUNC_ALWAYS;
   pso->ds.stencil_read_mask = 0xFF;
   pso->ds.stencil_write_mask = 0xFF;

   /* Stencil-disabled defaults: pass everything, change nothing. Zeroes
    * would mean NEVER/KEEP-on-fail, which rejects fragments silently.
    */
   pso->ds.front = (struct d12_stencil_face){
      .compare = AGX_ZS_FUNC_ALWAYS,
      .pass = AGX_STENCIL_OP_KEEP,
      .depth_fail = AGX_STENCIL_OP_KEEP,
      .stencil_fail = AGX_STENCIL_OP_KEEP,
   };
   pso->ds.back = pso->ds.front;

   if (ds_desc) {
      pso->ds.depth_test = ds_desc->DepthEnable;
      pso->ds.depth_write =
         ds_desc->DepthEnable &&
         ds_desc->DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL;
      if (ds_desc->DepthEnable)
         pso->ds.depth_func = d12_translate_compare(ds_desc->DepthFunc);

      pso->ds.stencil_test = ds_desc->StencilEnable;
      if (ds_desc->StencilEnable) {
         pso->ds.stencil_read_mask = ds_desc->StencilReadMask;
         pso->ds.stencil_write_mask = ds_desc->StencilWriteMask;
         d12_translate_stencil_face(&pso->ds.front, &ds_desc->FrontFace);
         d12_translate_stencil_face(&pso->ds.back, &ds_desc->BackFace);
      }
   }

   /* D3D12 and Honeykrisp both use a Y-negative viewport here. A
    * same-hardware command-stream diff therefore gives the direct mapping:
    * the default FrontCounterClockwise=FALSE PSO uses front_face_ccw=false.
    * Inverting it culls both triangles of the standard full-screen strip.
    */
   pso->raster.front_face_ccw = false;
   pso->raster.depth_clip = true;
   pso->raster.dynamic_depth_bias =
      flags & D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS;
   if (rs_desc) {
      pso->raster.front_face_ccw = rs_desc->FrontCounterClockwise;
      pso->raster.cull_front = rs_desc->CullMode == D3D12_CULL_MODE_FRONT;
      pso->raster.cull_back = rs_desc->CullMode == D3D12_CULL_MODE_BACK;
      pso->raster.depth_clip = rs_desc->DepthClipEnable;
      pso->raster.depth_bias = rs_desc->DepthBias;
      pso->raster.depth_bias_clamp = rs_desc->DepthBiasClamp;
      pso->raster.slope_scaled_depth_bias =
         rs_desc->SlopeScaledDepthBias;

      if (rs_desc->FillMode == D3D12_FILL_MODE_WIREFRAME)
         mesa_logw("CreateGraphicsPipelineState: wireframe fill ignored");
   }

   /* The tilebuffer sample stride is part of the hardware ABI, so use the
    * PSO's declared count and require the bound RTV to agree at draw time.
    * Keep zero colour buffers for a depth-only pipeline.
    * Inventing RGBA8 here makes the fragment shader and render command carry
    * a colour tilebuffer that has no backing attachment; Honeykrisp keeps the
    * zero-colour layout and lets ZLS stand alone.
    */
   enum pipe_format formats[AGX_MAX_RENDER_TARGETS] = {0};
   if (nr_rt)
      formats[0] = rt_format;
   for (unsigned i = 1; i < nr_rt; i++) {
      if (rt_formats && d12_format_is_renderable(rt_formats[i]))
         formats[i] = d12_format_to_pipe(rt_formats[i]);
   }
   struct agx_tilebuffer_layout tib =
      agx_build_tilebuffer_layout(formats, nr_rt, nr_samples, false);
   pso->tib = tib;

   const D3D12_RENDER_TARGET_BLEND_DESC default_rt_blend = {
      .BlendEnable = false,
      .LogicOpEnable = false,
      .SrcBlend = D3D12_BLEND_ONE,
      .DestBlend = D3D12_BLEND_ZERO,
      .BlendOp = D3D12_BLEND_OP_ADD,
      .SrcBlendAlpha = D3D12_BLEND_ONE,
      .DestBlendAlpha = D3D12_BLEND_ZERO,
      .BlendOpAlpha = D3D12_BLEND_OP_ADD,
      .LogicOp = D3D12_LOGIC_OP_NOOP,
      .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
   };
   const D3D12_RENDER_TARGET_BLEND_DESC *rt_blends[AGX_MAX_RENDER_TARGETS];
   struct agx_blend_rt_key blend_keys[AGX_MAX_RENDER_TARGETS] = {0};
   bool use_fs_epilog = blend_desc && blend_desc->AlphaToCoverageEnable;
   enum pipe_logicop logicop = PIPE_LOGICOP_NOOP;

   for (unsigned i = 0; i < nr_rt; i++) {
      const D3D12_RENDER_TARGET_BLEND_DESC *rt_blend =
         blend_desc
            ? &blend_desc->RenderTarget[
                 blend_desc->IndependentBlendEnable ? i : 0]
            : &default_rt_blend;
      rt_blends[i] = rt_blend;

      if (rt_blend->BlendEnable && rt_blend->LogicOpEnable) {
         free(pso);
         return E_INVALIDARG;
      }

      /* AGX exposes one logic operation for the whole output merger. D3D12
       * only permits that shape when independent blending is disabled, in
       * which case RT0's description is replicated to every active target. */
      if (rt_blend->LogicOpEnable) {
         if ((blend_desc && blend_desc->IndependentBlendEnable) ||
             !d12_translate_logic_op(rt_blend->LogicOp, &logicop)) {
            free(pso);
            return E_INVALIDARG;
         }
      }

      enum pipe_blend_func rgb_func = PIPE_BLEND_ADD;
      enum pipe_blend_func alpha_func = PIPE_BLEND_ADD;
      enum pipe_blendfactor rgb_src = PIPE_BLENDFACTOR_ONE;
      enum pipe_blendfactor rgb_dst = PIPE_BLENDFACTOR_ZERO;
      enum pipe_blendfactor alpha_src = PIPE_BLENDFACTOR_ONE;
      enum pipe_blendfactor alpha_dst = PIPE_BLENDFACTOR_ZERO;

      if (rt_blend->BlendEnable &&
          (!d12_translate_blend_op(rt_blend->BlendOp, &rgb_func) ||
           !d12_translate_blend_op(rt_blend->BlendOpAlpha, &alpha_func) ||
           !d12_translate_blend_factor(rt_blend->SrcBlend, &rgb_src) ||
           !d12_translate_blend_factor(rt_blend->DestBlend, &rgb_dst) ||
           !d12_translate_blend_factor(rt_blend->SrcBlendAlpha, &alpha_src) ||
           !d12_translate_blend_factor(rt_blend->DestBlendAlpha,
                                       &alpha_dst))) {
         free(pso);
         return E_INVALIDARG;
      }

      blend_keys[i] = (struct agx_blend_rt_key){
         .colormask = rt_blend->RenderTargetWriteMask &
                      D3D12_COLOR_WRITE_ENABLE_ALL,
         .mode = agx_pack_blend_standard(
            rgb_func, rgb_src, rgb_dst, alpha_func, alpha_src, alpha_dst),
      };

      if (formats[i] != PIPE_FORMAT_NONE &&
          (rt_blend->BlendEnable || rt_blend->LogicOpEnable ||
           rt_blend->RenderTargetWriteMask !=
              D3D12_COLOR_WRITE_ENABLE_ALL))
         use_fs_epilog = true;
   }

   const D3D12_RENDER_TARGET_BLEND_DESC *rt_blend =
      nr_rt ? rt_blends[0] : &default_rt_blend;

   struct agx_shader_part vs_part, fs_part;
   uint8_t cull_distance_size = 0;
   struct agx_fs_epilog_link_info fs_epilog_link = {0};
   struct d12_vs_fetch fetch;
   resolve_input_layout(pso, input_layout, vs_dxil, vs_size, &fetch);

   if (!compile_gfx_stage(device, map, vs_dxil, vs_size, MESA_SHADER_VERTEX,
                          &tib, 0xffff, 0, &pso->uvs, &fetch, NULL, NULL, NULL,
                          &cull_distance_size, &vs_part)) {
      free(pso);
      return E_INVALIDARG;
   }

   uint64_t fs_outputs = 0;
   const uint16_t framebuffer_mask =
      BITFIELD_MASK(MIN2(nr_samples, 8));
   uint16_t active_sample_mask =
      (sample_mask | (uint16_t)~framebuffer_mask) & 0xff;
   uint8_t a8_mask = 0;
   for (unsigned i = 0; rt_formats && i < nr_rt; i++) {
      if (rt_formats[i] == DXGI_FORMAT_A8_UNORM)
         a8_mask |= BITFIELD_BIT(i);
   }
   if (!compile_gfx_stage(device, map, ps_dxil, ps_size, MESA_SHADER_FRAGMENT,
                          &tib, active_sample_mask, a8_mask, NULL, NULL,
                          &fs_outputs,
                          &use_fs_epilog, &fs_epilog_link,
                          &cull_distance_size, &fs_part)) {
      free(vs_part.binary);
      free(pso);
      return E_INVALIDARG;
   }

   for (unsigned i = 0; i < nr_rt; i++) {
      if (formats[i] != PIPE_FORMAT_NONE && blend_keys[i].colormask &&
          (fs_outputs & BITFIELD64_BIT(FRAG_RESULT_DATA0 + i)))
         pso->rt_write_mask |= BITFIELD_BIT(i);
   }

   /* Cull distance is an emulated pre-fragment operation on AGX. Keep it in
    * the standard fragment prolog, exactly like Honeykrisp, rather than in
    * the API shader main. Besides preserving the required ordering relative
    * to depth/stencil tests, the fast linker knows how to merge the prolog's
    * private coefficient-register bindings after those used by the main. */
   struct agx_shader_part fs_prolog = {0};
   if (cull_distance_size) {
      struct agx_fs_prolog_key prolog_key = {
         .api_sample_mask = 0xff,
         .cull_distance_size = cull_distance_size,
         .run_zs_tests = true,
         .cf_base = fs_part.info.varyings.fs.nr_cf,
      };

      nir_builder fb =
         nir_builder_init_simple_shader(0, &agx_nir_options, NULL);
      agx_nir_fs_prolog(&fb, &prolog_key);

      struct agx_shader_key prolog_backend_key = {
         .dev = agx_gather_device_key(&device->dev),
         .secondary = true,
         .no_stop = true,
         .fs.cf_base = prolog_key.cf_base,
      };
      agx_compile_shader_nir(fb.shader, &prolog_backend_key, &fs_prolog);
      ralloc_free(fb.shader);

      if (!fs_prolog.binary || !fs_prolog.info.binary_size) {
         free(fs_prolog.binary);
         free(fs_part.binary);
         free(vs_part.binary);
         free(pso);
         return E_INVALIDARG;
      }
   }

   /* D3D12 does not define blending for pure-integer render targets. The
    * validation is conditional on the pixel shader actually exporting that
    * target: an enabled blend state is inert, and legal, when no colour is
    * written. Capture outputs_written before tilebuffer lowering consumes the
    * fragment stores, then reject only the active invalid combination.
    */
   for (unsigned i = 0; i < nr_rt; i++) {
      if (rt_blends[i]->BlendEnable &&
          util_format_is_pure_integer(formats[i]) &&
          (fs_outputs & BITFIELD64_BIT(FRAG_RESULT_DATA0 + i))) {
         free(fs_part.binary);
         free(vs_part.binary);
         free(pso);
         return E_INVALIDARG;
      }
   }

   /*
    * D3D12 dual-source blending consumes the second colour export as SRC1;
    * it is not a second render target. Consequently a real RT1 is illegal,
    * and independent blend state for later attachments must stay disabled.
    * Higher targets are only a conflict when the pixel shader writes them.
    *
    * Validate after translation so the shader's actual output mask, rather
    * than a bytecode-container guess, decides the RT2+ cases.
    */
   const bool dual_source =
      d12_blend_factor_uses_src1(rt_blend->SrcBlend) ||
      d12_blend_factor_uses_src1(rt_blend->DestBlend) ||
      d12_blend_factor_uses_src1(rt_blend->SrcBlendAlpha) ||
      d12_blend_factor_uses_src1(rt_blend->DestBlendAlpha);

   if (dual_source && blend_desc && blend_desc->IndependentBlendEnable) {
      for (unsigned i = 1; i < ARRAY_SIZE(blend_desc->RenderTarget); ++i) {
         if (blend_desc->RenderTarget[i].BlendEnable ||
             blend_desc->RenderTarget[i].LogicOpEnable) {
            free(fs_part.binary);
            free(vs_part.binary);
            free(pso);
            return E_INVALIDARG;
         }
      }
   }

   if (dual_source && rt_formats) {
      for (unsigned i = 1; i < MIN2(nr_rt, 8); ++i) {
         if (rt_formats[i] == DXGI_FORMAT_UNKNOWN)
            continue;

         if (i == 1 ||
             (fs_outputs & BITFIELD64_BIT(FRAG_RESULT_DATA0 + i))) {
            free(fs_part.binary);
            free(vs_part.binary);
            free(pso);
            return E_INVALIDARG;
         }
      }
   }

   struct agx_shader_part fs_epilog = {0};
   if (use_fs_epilog) {
      /* An explicit SV_Coverage output is the final coverage mask. D3D12
       * does not additionally alpha-to-coverage that mask; doing both made
       * an explicit nonzero mask disappear whenever the shader's alpha was
       * zero. */
      struct agx_fs_epilog_key epilog_key = {
         .link = fs_epilog_link,
         .blend.alpha_to_coverage =
            blend_desc && blend_desc->AlphaToCoverageEnable &&
            !(fs_outputs & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK)),
         .blend.logicop_enable = rt_blend->LogicOpEnable,
         .blend.logicop_func = logicop,
         .nr_samples = nr_samples,
      };

      memset(epilog_key.remap, -1, sizeof(epilog_key.remap));
      for (unsigned i = 0; i < nr_rt; i++) {
         epilog_key.blend.rt[i] = blend_keys[i];
         epilog_key.rt_formats[i] = formats[i];
         if (formats[i] != PIPE_FORMAT_NONE)
            epilog_key.remap[i] = i;
      }

      nir_builder eb =
         nir_builder_init_simple_shader(0, &agx_nir_options, NULL);
      agx_nir_fs_epilog(&eb, &epilog_key);
      agx_preprocess_nir(eb.shader);

      struct agx_shader_key epilog_backend_key = {
         .dev = agx_gather_device_key(&device->dev),
         .secondary = true,
         /* The one-sample fast-link footer appends the terminating stop. */
         .no_stop = true,
      };
      agx_compile_shader_nir(eb.shader, &epilog_backend_key, &fs_epilog);
      ralloc_free(eb.shader);

      bool any_colour_write = false;
      for (unsigned i = 0; i < nr_rt; i++)
         any_colour_write |= blend_keys[i].colormask != 0;

      const bool intentionally_empty =
         !any_colour_write && !epilog_key.blend.logicop_enable &&
         !epilog_key.blend.alpha_to_coverage && !fs_epilog_link.write_z &&
         !fs_epilog_link.write_s;

      if ((!fs_epilog.binary || !fs_epilog.info.binary_size) &&
          !intentionally_empty) {
         free(fs_epilog.binary);
         free(fs_part.binary);
         free(vs_part.binary);
         free(pso);
         return E_INVALIDARG;
      }
   }

   /* The vertex shader needs a prolog: the AGX ABI has vertex/instance IDs
    * arriving in registers that the prolog fills (AGX_ABI_VIN_*). Without
    * one, SV_VertexID reads an uninitialized register and the geometry
    * lands nowhere. We fetch no attributes, so static_vi says "just fix up
    * the vertex ID".
    */
   struct agx_vs_prolog_key prolog_key = {
      .hw = true,
      .static_vi = true,
   };

   nir_builder pb =
      nir_builder_init_simple_shader(0, &agx_nir_options, NULL);
   agx_nir_vs_prolog(&pb, &prolog_key);

   struct agx_shader_key prolog_backend_key = {
      .dev = agx_gather_device_key(&device->dev),
      /* Allocas live in per-thread scratch, and large shaders spill. */
      .has_scratch = true,
      .secondary = true,
      .no_stop = true,
   };

   struct agx_shader_part prolog;
   memset(&prolog, 0, sizeof(prolog));
   agx_compile_shader_nir(pb.shader, &prolog_backend_key, &prolog);
   ralloc_free(pb.shader);

   /* Link: this uploads executable memory and bakes the USC/PPP words. */
   agx_fast_link(&pso->vs.linked, &device->dev, false, &vs_part, &prolog, NULL,
                 0);
   free(prolog.binary);

   /*
    * THE PREAMBLE RULE, which the ordinary vertex shader had never needed
    * before: agx_fast_link links main + prolog + epilog and does NOT copy
    * the preamble, so info.preamble_offset is meaningless inside
    * linked.bo. Every other stage that can have one already keeps its own
    * copy (gs_rast, tes, ms_rast); a plain VS could not have one until it
    * started fetching vertex attributes, because nothing else made it
    * compute a uniform.
    *
    * Missing this does not fail loudly. The preamble is what turns the
    * attribute parameter block into the base/clamp/stride uniforms, so
    * without it the fetch runs against whatever those registers held --
    * measured as a triangle covering the ENTIRE render target.
    */
   if (vs_part.info.has_preamble) {
      pso->vs.code_bo =
         agx_bo_create(&device->dev, vs_part.info.binary_size, 0,
                       AGX_BO_EXEC | AGX_BO_LOW_VA, "d3d12 vs preamble");
      if (!pso->vs.code_bo) {
         free(fs_epilog.binary);
         free(vs_part.binary);
         free(fs_part.binary);
         free(pso);
         return E_OUTOFMEMORY;
      }
      memcpy(agx_bo_map(pso->vs.code_bo), vs_part.binary,
             vs_part.info.binary_size);
      pso->vs.preamble_va =
         pso->vs.code_bo->va->addr + vs_part.info.preamble_offset;
   }
   agx_fast_link(&pso->fs.linked, &device->dev, true, &fs_part,
                 fs_prolog.binary ? &fs_prolog : NULL,
                 use_fs_epilog && fs_epilog.binary ? &fs_epilog : NULL,
                 use_fs_epilog ? 1 : 0);
   free(fs_prolog.binary);
   free(fs_epilog.binary);

   /* Texture promotion gives the fragment shader a preamble that copies
    * the descriptor reached through D12_UNIFORM_TEXTURES into hardware
    * texture state. agx_fast_link does not copy that preamble, so retain
    * the original compiled part exactly as the VS/TES/GS/MS paths do.
    * Pointing preamble_va into linked.bo executes unrelated linked main
    * code and is a GPU-fault-class error, not merely a bad sample. */
   if (fs_part.info.has_preamble) {
      pso->fs.code_bo =
         agx_bo_create(&device->dev, fs_part.info.binary_size, 0,
                       AGX_BO_EXEC | AGX_BO_LOW_VA, "d3d12 fs preamble");
      if (!pso->fs.code_bo) {
         free(vs_part.binary);
         free(fs_part.binary);
         free(pso);
         return E_OUTOFMEMORY;
      }
      memcpy(agx_bo_map(pso->fs.code_bo), fs_part.binary,
             fs_part.info.binary_size);
      pso->fs.preamble_va =
         pso->fs.code_bo->va->addr + fs_part.info.preamble_offset;
   }

   pso->vs.info = vs_part.info;
   pso->fs.info = fs_part.info;

   /*
    * GEOMETRY. Three more programs, compiled and linked here so a draw
    * only has to bind them. The rasterization shader replaces pso->vs as
    * the hardware vertex shader; the other two are compute, uploaded the
    * same way the compute PSO uploads its single program.
    *
    * A failure anywhere here fails PSO creation. Every unsupported
    * geometry configuration already refused BY NAME inside
    * compile_gs_stage, so reaching this point and then falling back to a
    * pipeline without a GS would be the silent-wrong this whole path
    * exists to avoid.
    */
   /*
    * TESSELLATION. The vertex shader becomes the same compute prepass the
    * geometry path uses, the hull shader is compute, and the DOMAIN
    * shader replaces pso->vs as the hardware vertex stage.
    */
   if (hs_dxil && hs_size && ds_dxil && ds_size) {
      struct agx_shader_part sw_vs_part, tcs_part, tes_part;

      if (!compile_sw_vs_stage(device, map, vs_dxil, vs_size, &fetch,
                               &sw_vs_part, &pso->sw_vs_outputs, NULL, NULL) ||
          !compile_tcs_stage(device, map, hs_dxil, hs_size, pso, &tcs_part) ||
          !compile_tes_stage(device, map, ds_dxil, ds_size, pso, &pso->tes_uvs,
                             &tes_part)) {
         free(pso);
         return E_INVALIDARG;
      }

      const struct {
         struct agx_shader_part *part;
         struct agx_bo **bo;
         uint64_t *va;
         struct agx_shader_info *info;
         const char *name;
      } cs[] = {
         {&sw_vs_part, &pso->sw_vs_bo, &pso->sw_vs_va, &pso->sw_vs_info,
          "d3d12 sw vs (tess)"},
         {&tcs_part, &pso->tcs_bo, &pso->tcs_va, &pso->tcs_info,
          "d3d12 hull"},
      };

      for (unsigned i = 0; i < ARRAY_SIZE(cs); i++) {
         *cs[i].bo = agx_bo_create(&device->dev, cs[i].part->info.binary_size,
                                   0, AGX_BO_EXEC | AGX_BO_LOW_VA,
                                   cs[i].name);
         if (!*cs[i].bo) {
            for (unsigned j = 0; j < i; j++)
               agx_bo_unreference(&device->dev, *cs[j].bo);
            free(pso);
            return E_OUTOFMEMORY;
         }
         memcpy(agx_bo_map(*cs[i].bo), cs[i].part->binary,
                cs[i].part->info.binary_size);
         *cs[i].va = (*cs[i].bo)->va->addr;
         *cs[i].info = cs[i].part->info;
      }

      struct agx_shader_part tes_prolog;
      nir_builder tb =
         nir_builder_init_simple_shader(0, &agx_nir_options, NULL);
      agx_nir_vs_prolog(&tb, &prolog_key);
      memset(&tes_prolog, 0, sizeof(tes_prolog));
      agx_compile_shader_nir(tb.shader, &prolog_backend_key, &tes_prolog);
      ralloc_free(tb.shader);

      agx_fast_link(&pso->tes.linked, &device->dev, false, &tes_part,
                    &tes_prolog, NULL, 0);
      free(tes_prolog.binary);
      pso->tes.info = tes_part.info;
      agx_assign_uvs(&pso->tes_varyings, &pso->tes_uvs, 0, 0);

      /* Same preamble rule as the geometry rasterization program: the
       * linker does not copy it, so keep the compiled part addressable.
       */
      if (tes_part.info.has_preamble) {
         pso->tes.code_bo =
            agx_bo_create(&device->dev, tes_part.info.binary_size, 0,
                          AGX_BO_EXEC | AGX_BO_LOW_VA, "d3d12 domain");
         if (!pso->tes.code_bo) {
            free(pso);
            return E_OUTOFMEMORY;
         }
         memcpy(agx_bo_map(pso->tes.code_bo), tes_part.binary,
                tes_part.info.binary_size);
         pso->tes.preamble_va =
            pso->tes.code_bo->va->addr + tes_part.info.preamble_offset;
      }

      unsigned tes_samplers = tes_part.info.sampler_state_count;
      if (tes_part.info.uses_txf)
         tes_samplers = MAX2(tes_samplers, 1);

      agx_pack(&pso->tes.counts, COUNTS, cfg) {
         cfg.uniform_register_count = tes_part.info.push_count;
         cfg.preshader_register_count = tes_part.info.nr_preamble_gprs;
         cfg.sampler_state_register_count =
            agx_translate_sampler_state_count(tes_samplers, false);
         cfg.texture_state_register_count = tes_part.info.texture_state_count;
      }

      mesa_logi("compiled tessellation pipeline: sw_vs %u B, tcs %u B, "
                "tes %u B, patch_size %u, stride %u, mode %u, spacing %u, "
                "ccw %d, points %d",
                sw_vs_part.info.binary_size, tcs_part.info.binary_size,
                tes_part.info.binary_size, pso->tcs_output_patch_size,
                pso->tcs_output_stride, pso->tess_mode, pso->tess_spacing,
                pso->tess_ccw, pso->tess_points);

      pso->has_tess = true;
   }

   bool has_app_gs = gs_dxil && gs_size;
   bool has_so = so_desc && so_desc->NumEntries;
   if (has_app_gs || has_so) {
      struct agx_shader_part sw_vs_part, gs_count_part, gs_pre_part,
         gs_main_part, gs_rast_part;
      uint8_t clip_size = 0, cull_size = 0;

      nir_xfb_info *xfb = NULL;
      if (has_so) {
         bool valid = false;
         const void *capture = has_app_gs ? gs_dxil
                               : pso->has_tess ? ds_dxil
                                               : vs_dxil;
         size_t capture_size = has_app_gs ? gs_size
                               : pso->has_tess ? ds_size
                                               : vs_size;
         xfb = d12_build_xfb_info(NULL, so_desc, capture, capture_size,
                                  &valid);
         if (!valid || !xfb) {
            mesa_loge("graphics PSO: invalid stream-output declaration or "
                      "unmatched output semantic");
            free(pso);
            return E_INVALIDARG;
         }
      }

      bool compiled;
      uint64_t gs_input_outputs;
      if (pso->has_tess) {
         compiled = compile_sw_tes_stage(
            device, map, ds_dxil, ds_size, &sw_vs_part,
            &pso->sw_tes_outputs, &clip_size, &cull_size);
         gs_input_outputs = pso->sw_tes_outputs;
      } else {
         compiled = compile_sw_vs_stage(
            device, map, vs_dxil, vs_size, &fetch, &sw_vs_part,
            &pso->sw_vs_outputs, &clip_size, &cull_size);
         gs_input_outputs = pso->sw_vs_outputs;
      }

      if (compiled && has_app_gs) {
         compiled = compile_gs_stage(
            device, map, gs_dxil, gs_size, xfb, primitive_topology_type, pso,
            &pso->gs_info,
            &pso->gs_uvs, &gs_count_part, &gs_pre_part, &gs_main_part,
            &gs_rast_part);
      } else if (compiled) {
         enum mesa_prim prim =
            pso->has_tess
               ? (pso->tess_points
                     ? MESA_PRIM_POINTS
                     : pso->tess_mode == TESS_PRIMITIVE_ISOLINES
                          ? MESA_PRIM_LINES
                          : MESA_PRIM_TRIANGLES)
               : d12_so_primitive(primitive_topology_type);
         if (prim == MESA_PRIM_UNKNOWN) {
            mesa_loge("graphics PSO: stream output needs a point, line, or "
                      "triangle primitive topology type");
            compiled = false;
         } else {
            nir_shader *passthrough = d12_build_so_passthrough_gs(
               gs_input_outputs, clip_size, cull_size, prim, xfb);
            compiled = compile_gs_nir(
               device, passthrough, &pso->gs_info, &pso->gs_uvs,
               &gs_count_part, &gs_pre_part, &gs_main_part, &gs_rast_part);
         }
      }
      ralloc_free(xfb);

      if (!compiled) {
         free(pso);
         return E_INVALIDARG;
      }

      /* The software VS, optional count/pre-GS, and geometry body are
       * ordinary compute programs. Count/pre-GS are present when XFB needs
       * capacity accounting and filled-size updates. */
      struct agx_bo **sw_bo =
         pso->has_tess ? &pso->sw_tes_bo : &pso->sw_vs_bo;
      uint64_t *sw_va = pso->has_tess ? &pso->sw_tes_va : &pso->sw_vs_va;
      struct agx_shader_info *sw_info =
         pso->has_tess ? &pso->sw_tes_info : &pso->sw_vs_info;

      const struct {
         struct agx_shader_part *part;
         struct agx_bo **bo;
         uint64_t *va;
         struct agx_shader_info *info;
         const char *name;
      } cs[] = {
         {&sw_vs_part, sw_bo, sw_va, sw_info,
          pso->has_tess ? "d3d12 sw domain" : "d3d12 sw vs"},
         {&gs_count_part, &pso->gs_count_bo, &pso->gs_count_va,
          &pso->gs_count_info, "d3d12 gs count"},
         {&gs_pre_part, &pso->gs_pre_bo, &pso->gs_pre_va,
          &pso->gs_pre_info, "d3d12 gs pre"},
         {&gs_main_part, &pso->gs_main_bo, &pso->gs_main_va,
          &pso->gs_main_info, "d3d12 gs main"},
      };

      for (unsigned i = 0; i < ARRAY_SIZE(cs); i++) {
         if (!cs[i].part->binary || !cs[i].part->info.binary_size)
            continue;

         /* Same rule as the compute PSO: executable, low-VA, so the USC
          * can address it 32-bit-relative to the shader base. */
         *cs[i].bo = agx_bo_create(&device->dev, cs[i].part->info.binary_size,
                                   0, AGX_BO_EXEC | AGX_BO_LOW_VA,
                                   cs[i].name);
         if (!*cs[i].bo) {
            /* Unwind whatever the earlier iteration already took. */
            for (unsigned j = 0; j < i; j++) {
               if (*cs[j].bo)
                  agx_bo_unreference(&device->dev, *cs[j].bo);
            }
            free(pso);
            return E_OUTOFMEMORY;
         }
         memcpy(agx_bo_map(*cs[i].bo), cs[i].part->binary,
                cs[i].part->info.binary_size);
         *cs[i].va = (*cs[i].bo)->va->addr;
         *cs[i].info = cs[i].part->info;
      }

      /* The rasterization program is an ordinary hardware vertex shader
       * and needs the SAME prolog pso->vs gets. It reads no vertex
       * attributes, but it does read load_vertex_id and load_instance_id
       * to work out which emitted vertex it represents, and fixing those
       * up is exactly what the static_vi prolog does. Linking it with no
       * prolog leaves both IDs undefined, so every invocation reads the
       * wrong slot of the vertex buffer.
       */
      struct agx_shader_part rast_prolog;
      nir_builder rb =
         nir_builder_init_simple_shader(0, &agx_nir_options, NULL);
      agx_nir_vs_prolog(&rb, &prolog_key);
      memset(&rast_prolog, 0, sizeof(rast_prolog));
      agx_compile_shader_nir(rb.shader, &prolog_backend_key, &rast_prolog);
      ralloc_free(rb.shader);

      agx_fast_link(&pso->gs_rast.linked, &device->dev, false, &gs_rast_part,
                    &rast_prolog, NULL, 0);
      free(rast_prolog.binary);

      /* Keep the compiled part so its PREAMBLE has an address. The linker
       * copies main/prolog/epilog into linked.bo and leaves the preamble
       * behind, so info.preamble_offset does not apply there -- the USC
       * then points the preshader at whatever happens to sit at that
       * offset, which decoded as a run of `jmp_incomplete 0` and killed
       * the render command outright: the target was not even cleared.
       */
      if (gs_rast_part.info.has_preamble) {
         pso->gs_rast.code_bo =
            agx_bo_create(&device->dev, gs_rast_part.info.binary_size, 0,
                          AGX_BO_EXEC | AGX_BO_LOW_VA, "d3d12 gs rast");
         if (!pso->gs_rast.code_bo) {
            free(pso);
            return E_OUTOFMEMORY;
         }
         memcpy(agx_bo_map(pso->gs_rast.code_bo), gs_rast_part.binary,
                gs_rast_part.info.binary_size);
         pso->gs_rast.preamble_va = pso->gs_rast.code_bo->va->addr +
                                    gs_rast_part.info.preamble_offset;
      }
      pso->gs_rast.info = gs_rast_part.info;

      /* The VDM reads these counts to know how much state to load. Leave
       * them zero and the rasterization program runs with no uniforms,
       * no textures and no preamble -- which is not a crash, just a
       * shader computing from whatever was already in the registers.
       */
      unsigned rast_samplers = gs_rast_part.info.sampler_state_count;
      if (gs_rast_part.info.uses_txf)
         rast_samplers = MAX2(rast_samplers, 1);

      agx_pack(&pso->gs_rast.counts, COUNTS, cfg) {
         cfg.uniform_register_count = gs_rast_part.info.push_count;
         cfg.preshader_register_count = gs_rast_part.info.nr_preamble_gprs;
         cfg.sampler_state_register_count =
            agx_translate_sampler_state_count(rast_samplers, false);
         cfg.texture_state_register_count =
            gs_rast_part.info.texture_state_count;
      }
      agx_assign_uvs(&pso->gs_varyings, &pso->gs_uvs, 0, 0);

      mesa_logi("compiled geometry pipeline: sw_vs %u B / %u GPRs, "
                "gs_main %u B, gs_rast %u B / %u GPRs, outputs 0x%llx",
                sw_vs_part.info.binary_size, sw_vs_part.info.nr_gprs,
                gs_main_part.info.binary_size, gs_rast_part.info.binary_size,
                gs_rast_part.info.nr_gprs,
                (unsigned long long)(pso->has_tess ? pso->sw_tes_outputs
                                                   : pso->sw_vs_outputs));

      pso->has_gs = true;
   }

   /*
    * A texel fetch consumes a sampler even though HLSL's Load names none:
    * the backend lowers txf to `smpi ... ss0` and the driver supplies that
    * state. The count here is what makes the hardware FETCH it -- leave it
    * at zero and sampler state 0 is never loaded, so the fetch reads
    * undefined state and returns mostly out-of-bounds. Honeykrisp does the
    * same MAX2 (hk_shader.c).
    */
   unsigned vs_samplers = vs_part.info.sampler_state_count;
   if (vs_part.info.uses_txf)
      vs_samplers = MAX2(vs_samplers, 1);

   unsigned fs_samplers = fs_part.info.sampler_state_count;
   if (fs_part.info.uses_txf)
      fs_samplers = MAX2(fs_samplers, 1);

   agx_pack(&pso->vs.counts, COUNTS, cfg) {
      cfg.uniform_register_count = vs_part.info.push_count;
      cfg.preshader_register_count = vs_part.info.nr_preamble_gprs;
      cfg.sampler_state_register_count =
         agx_translate_sampler_state_count(vs_samplers, false);
      cfg.texture_state_register_count = vs_part.info.texture_state_count;
   }

   agx_pack_fragment_face_2(&pso->fs.frag_face, 0, &fs_part.info);

   agx_pack(&pso->fs.fs_counts, FRAGMENT_SHADER_WORD_0, cfg) {
      cfg.cf_binding_count = pso->fs.linked.cf.nr_bindings;
      cfg.uniform_register_count = fs_part.info.push_count;
      cfg.preshader_register_count = fs_part.info.nr_preamble_gprs;
      cfg.texture_state_register_count = fs_part.info.texture_state_count;
      cfg.sampler_state_register_count =
         agx_translate_sampler_state_count(fs_samplers, false);
   }

   agx_assign_uvs(&pso->varyings, &pso->uvs, 0, 0);

   pso->iface.lpVtbl = (ID3D12PipelineStateVtbl *)&d12_pso_vtbl;
   pso->refcount = 1;
   pso->device_ref_active = true;
   pso->device = device;
   pso->is_compute = false;
   d12_private_data_store_init(&pso->private_data, (IUnknown *)&pso->iface);
   d12_device_add_ref(device);

   mesa_logi("compiled graphics pipeline: VS %u B / %u GPRs, FS %u B / %u "
             "GPRs, uvs size %u, cf bindings %u",
             vs_part.info.binary_size, vs_part.info.nr_gprs,
             fs_part.info.binary_size, fs_part.info.nr_gprs, pso->uvs.size,
             pso->fs.linked.cf.nr_bindings);

   if (getenv("D3D12AGX_DUMP_SHADERS")) {
      fprintf(stderr, "; VS main +0x%x\n", vs_part.info.main_offset);
      agx2_disassemble(vs_part.binary + vs_part.info.main_offset,
                       vs_part.info.binary_size - vs_part.info.main_offset,
                       stderr);
      fprintf(stderr, "; FS main +0x%x\n", fs_part.info.main_offset);
      agx2_disassemble(fs_part.binary + fs_part.info.main_offset,
                       fs_part.info.binary_size - fs_part.info.main_offset,
                       stderr);
   }

   free(vs_part.binary);
   free(fs_part.binary);

   HRESULT hr = d12_pso_QueryInterface(&pso->iface, riid, out);
   d12_pso_Release(&pso->iface);
   return hr;
}
