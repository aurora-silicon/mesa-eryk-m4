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
      d12_device_release(rs->device);
      free(rs);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_root_sig_GetPrivateData(ID3D12RootSignature *iface, REFGUID guid,
                            UINT *size, void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_root_sig_SetPrivateData(ID3D12RootSignature *iface, REFGUID guid,
                            UINT size, const void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_root_sig_SetPrivateDataInterface(ID3D12RootSignature *iface, REFGUID guid,
                                     const IUnknown *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_root_sig_SetName(ID3D12RootSignature *iface, LPCWSTR name)
{
   return S_OK;
}

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
      return E_INVALIDARG;
   }

   d12_root_layout_build_map(&rs->layout, &rs->map);

   rs->iface.lpVtbl = (ID3D12RootSignatureVtbl *)&d12_root_sig_vtbl;
   rs->refcount = 1;
   rs->device = device;
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
      p_atomic_inc(&pso->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12PipelineState::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_pso_AddRef(ID3D12PipelineState *iface)
{
   return p_atomic_inc_return(&d12_pso_from_iface(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_pso_Release(ID3D12PipelineState *iface)
{
   struct d12_pipeline_state *pso = d12_pso_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&pso->refcount);

   if (refs == 0) {
      if (pso->code_bo)
         agx_bo_unreference(&pso->device->dev, pso->code_bo);
      d12_device_release(pso->device);
      free(pso);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_pso_GetPrivateData(ID3D12PipelineState *iface, REFGUID guid, UINT *size,
                       void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_pso_SetPrivateData(ID3D12PipelineState *iface, REFGUID guid, UINT size,
                       const void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_pso_SetPrivateDataInterface(ID3D12PipelineState *iface, REFGUID guid,
                                const IUnknown *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_pso_SetName(ID3D12PipelineState *iface, LPCWSTR name)
{
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_pso_GetDevice(ID3D12PipelineState *iface, REFIID riid, void **out)
{
   struct d12_pipeline_state *pso = d12_pso_from_iface(iface);
   return ID3D12Device_QueryInterface(&pso->device->iface, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_pso_GetCachedBlob(ID3D12PipelineState *iface, ID3DBlob **blob)
{
   return E_NOTIMPL;
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

   const uint8_t *bc;
   size_t bc_size;
   const char *why = NULL;
   if (!d12_dxil_find_bitcode(dxil, dxil_size, &bc, &bc_size, &why)) {
      mesa_loge("CreateComputePipelineState: %s", why);
      return E_INVALIDARG;
   }

   struct d12_module mod;
   if (!d12_dxil_module_parse(&mod, bc, bc_size)) {
      mesa_loge("CreateComputePipelineState: DXIL parse failed: %s",
                mod.error ? mod.error : "unknown");
      return E_INVALIDARG;
   }

   const char *error = NULL;
   nir_shader *nir = d12_dxil_to_nir(&mod, MESA_SHADER_COMPUTE, map, &error);
   d12_dxil_module_finish(&mod);

   if (!nir) {
      mesa_loge("CreateComputePipelineState: DXIL->NIR failed: %s", error);
      return E_INVALIDARG;
   }

   NIR_PASS(_, nir, agx_nir_lower_texture);
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
       * Fix the graphics preamble address before turning this on.
       */
      .promote_constants = false,
      /* Uniform registers below this are the driver's: the root pointer. */
      .reserved_preamble = D12_UNIFORM_RESERVED,
   };

   struct agx_shader_part part;
   memset(&part, 0, sizeof(part));
   agx_compile_shader_nir(nir, &key, &part);

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
   pso->device = device;
   pso->is_compute = true;
   pso->info = part.info;
   pso->code_va = pso->code_bo->va->addr;
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
static bool
compile_gfx_stage(struct d12_device *device,
                  const struct d12_binding_map *map, const void *dxil,
                  size_t dxil_size, mesa_shader_stage stage,
                  struct agx_tilebuffer_layout *tib,
                  struct agx_unlinked_uvs_layout *uvs,
                  struct agx_shader_part *out)
{
   const uint8_t *bc;
   size_t bc_size;
   const char *why = NULL;
   if (!d12_dxil_find_bitcode(dxil, dxil_size, &bc, &bc_size, &why)) {
      mesa_loge("graphics PSO: %s", why);
      return false;
   }

   struct d12_module mod;
   if (!d12_dxil_module_parse(&mod, bc, bc_size)) {
      mesa_loge("graphics PSO: DXIL parse failed: %s",
                mod.error ? mod.error : "unknown");
      return false;
   }

   const char *error = NULL;
   nir_shader *nir = d12_dxil_to_nir(&mod, stage, map, &error);
   d12_dxil_module_finish(&mod);

   if (!nir) {
      mesa_loge("graphics PSO: DXIL->NIR failed: %s", error);
      return false;
   }

   NIR_PASS(_, nir, agx_nir_lower_texture);
   agx_preprocess_nir(nir);
   NIR_PASS(_, nir, d12_dxil_lower_root);

   if (stage == MESA_SHADER_FRAGMENT) {
      unsigned bindless_base = 0;
      NIR_PASS(_, nir, agx_nir_lower_tilebuffer, tib, NULL, &bindless_base,
               NULL, NULL);
      /* Discards become sample_mask writes; hk orders this before the
       * MSAA lowering for the same reason (hk_shader.c).
       */
      NIR_PASS(_, nir, agx_nir_lower_sample_mask);
      NIR_PASS(_, nir, agx_nir_lower_monolithic_msaa, 1);
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
      NIR_PASS(_, nir, agx_nir_lower_vs_input_to_prolog);
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
      /* See the compute path: promotion needs a preamble we do not bind. */
      /* See the note on the compute key above. */
      .promote_constants = false,
      .reserved_preamble = D12_UNIFORM_RESERVED,
      /* The tilebuffer store is lowered INTO this shader, so it is
       * self-contained and must terminate normally. Setting no_stop here
       * would leave the fragment shader running off the end.
       */
      .no_stop = false,
   };

   memset(out, 0, sizeof(*out));
   agx_compile_shader_nir(nir, &key, out);

   return out->binary && out->info.binary_size;
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

HRESULT
d12_pipeline_state_create_graphics(struct d12_device *device,
                                   const void *vs_dxil, size_t vs_size,
                                   const void *ps_dxil, size_t ps_size,
                                   enum pipe_format rt_format,
                                   const D3D12_DEPTH_STENCIL_DESC *ds_desc,
                                   DXGI_FORMAT dsv_format,
                                   const D3D12_RASTERIZER_DESC *rs_desc,
                                   const struct d12_binding_map *map,
                                   REFIID riid, void **out)
{
   if (!out)
      return E_POINTER;
   if (!vs_dxil || !vs_size || !ps_dxil || !ps_size)
      return E_INVALIDARG;

   struct d12_pipeline_state *pso = calloc(1, sizeof(*pso));
   if (!pso)
      return E_OUTOFMEMORY;

   /* Depth/stencil state, resolved to hardware enums once here rather than
    * per draw. With no DSV format the tests are inert regardless of what
    * the description asks for, matching D3D12's own rule.
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

   if (ds_desc && dsv_format != DXGI_FORMAT_UNKNOWN) {
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

   /*
    * Facing and culling. Measured on M2 (G14G): with the hardware's
    * front_face_ccw set, a triangle wound counter-clockwise in NDC is the
    * FRONT face -- cull_front removes it and cull_back does not.
    *
    * D3D12 defines facing after the viewport transform, where +y runs down
    * the framebuffer; our viewport negates y, so NDC-counter-clockwise is
    * screen-clockwise, which is D3D12's front face when
    * FrontCounterClockwise is FALSE. Hence the inversion here.
    */
   pso->raster.front_face_ccw = true;
   if (rs_desc) {
      pso->raster.front_face_ccw = !rs_desc->FrontCounterClockwise;
      pso->raster.cull_front = rs_desc->CullMode == D3D12_CULL_MODE_FRONT;
      pso->raster.cull_back = rs_desc->CullMode == D3D12_CULL_MODE_BACK;

      if (rs_desc->FillMode == D3D12_FILL_MODE_WIREFRAME)
         mesa_logw("CreateGraphicsPipelineState: wireframe fill ignored");
   }

   /* One render target, single sampled, in the format the pipeline
    * actually declares rather than an assumed RGBA8.
    */
   enum pipe_format formats[8] = {rt_format != PIPE_FORMAT_NONE
                                     ? rt_format
                                     : PIPE_FORMAT_R8G8B8A8_UNORM};
   struct agx_tilebuffer_layout tib =
      agx_build_tilebuffer_layout(formats, 1, 1, false);

   struct agx_shader_part vs_part, fs_part;
   if (!compile_gfx_stage(device, map, vs_dxil, vs_size, MESA_SHADER_VERTEX, &tib,
                          &pso->uvs, &vs_part)) {
      free(pso);
      return E_INVALIDARG;
   }

   if (!compile_gfx_stage(device, map, ps_dxil, ps_size, MESA_SHADER_FRAGMENT,
                          &tib, NULL, &fs_part)) {
      free(vs_part.binary);
      free(pso);
      return E_INVALIDARG;
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
   agx_fast_link(&pso->fs.linked, &device->dev, true, &fs_part, NULL, NULL, 1);

   pso->vs.info = vs_part.info;
   pso->fs.info = fs_part.info;

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

   /* Assign vertex output slots; no flat/linear varyings in the first
    * triangle, so both masks are empty.
    */
   agx_assign_uvs(&pso->varyings, &pso->uvs, 0, 0);

   pso->iface.lpVtbl = (ID3D12PipelineStateVtbl *)&d12_pso_vtbl;
   pso->refcount = 1;
   pso->device = device;
   pso->is_compute = false;
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
