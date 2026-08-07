/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Root signature blobs: serialize a D3D12_ROOT_SIGNATURE_DESC into the
 * DXBC/RTS0 container D3D12 defines, and parse one back.
 *
 * This matters beyond format bookkeeping. A root signature is the only
 * thing that says which shader register a root parameter feeds:
 * SetGraphicsRootDescriptorTable names a root PARAMETER index, while the
 * shader asks for a REGISTER, and nothing else connects the two. Until it
 * was parsed, the driver assumed parameter index == binding slot, which is
 * true only for the shapes our own tests happened to use.
 *
 * The container layout was verified byte for byte against blobs produced
 * by dxc (SDK 26100) with -T rootsig_1_0, for a root-descriptor-only
 * signature, a descriptor-table signature, and a mixed one using every
 * parameter type, a non-zero register space and a non-zero base register.
 * Those blobs are archived alongside the tests.
 */

#include "d12_private.h"

#include <stdlib.h>
#include <string.h>

/* 'DXBC' and 'RTS0' as little-endian 32-bit tags. */
#define D12_FOURCC(a, b, c, d)                                               \
   ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) |           \
    ((uint32_t)(d) << 24))

#define D12_TAG_DXBC D12_FOURCC('D', 'X', 'B', 'C')
#define D12_TAG_RTS0 D12_FOURCC('R', 'T', 'S', '0')

/* Sizes of the serialized records, in bytes. */
#define D12_RS_HEADER_SIZE 24 /* version, nparam, poff, nsamp, soff, flags */
#define D12_RS_PARAM_SIZE 12  /* type, visibility, payload offset */
#define D12_RS_RANGE_SIZE 20
#define D12_RS_TABLE_SIZE 8    /* nranges, ranges offset */
#define D12_RS_DESCRIPTOR_SIZE 8  /* register, space */
#define D12_RS_CONSTANTS_SIZE 12  /* register, space, num values */
#define D12_RS_SAMPLER_SIZE 52

struct d12_writer {
   uint8_t *data;
   size_t size, used;
   bool failed;
};

static void
w_reserve(struct d12_writer *w, size_t n)
{
   if (w->failed)
      return;

   if (w->used + n > w->size) {
      size_t want = MAX2(w->size * 2, w->used + n + 256);
      uint8_t *grown = realloc(w->data, want);
      if (!grown) {
         w->failed = true;
         return;
      }
      w->data = grown;
      w->size = want;
   }
}

static void
w_u32(struct d12_writer *w, uint32_t v)
{
   w_reserve(w, 4);
   if (w->failed)
      return;

   /* Explicit little-endian: the container is a file format, not a struct. */
   w->data[w->used++] = (uint8_t)(v);
   w->data[w->used++] = (uint8_t)(v >> 8);
   w->data[w->used++] = (uint8_t)(v >> 16);
   w->data[w->used++] = (uint8_t)(v >> 24);
}

static void
w_u32_at(struct d12_writer *w, size_t offset, uint32_t v)
{
   if (w->failed || offset + 4 > w->used)
      return;

   w->data[offset] = (uint8_t)(v);
   w->data[offset + 1] = (uint8_t)(v >> 8);
   w->data[offset + 2] = (uint8_t)(v >> 16);
   w->data[offset + 3] = (uint8_t)(v >> 24);
}

static uint32_t
r_u32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
          ((uint32_t)p[3] << 24);
}

/*
 * Serialize. Offsets inside the RTS0 payload are relative to the start of
 * the payload, not to the file, so everything is written into a standalone
 * buffer and the container is wrapped around it afterwards.
 */
static bool
serialize_rts0(const D3D12_ROOT_SIGNATURE_DESC *desc, struct d12_writer *w)
{
   const uint32_t nparams = desc->NumParameters;
   const uint32_t nsamplers = desc->NumStaticSamplers;

   /* Parameters come first, then each parameter's payload, then any
    * descriptor ranges, then static samplers.
    */
   const uint32_t params_off = D12_RS_HEADER_SIZE;

   w_u32(w, 1); /* D3D_ROOT_SIGNATURE_VERSION_1_0 */
   w_u32(w, nparams);
   w_u32(w, nparams ? params_off : 0);
   w_u32(w, nsamplers);
   w_u32(w, 0); /* static sampler offset, patched below */
   w_u32(w, desc->Flags);

   /* Reserve the parameter records; their payload offsets are patched as
    * each payload is appended.
    */
   size_t param_record = w->used;
   for (uint32_t i = 0; i < nparams; i++) {
      w_u32(w, desc->pParameters[i].ParameterType);
      w_u32(w, desc->pParameters[i].ShaderVisibility);
      w_u32(w, 0);
   }

   for (uint32_t i = 0; i < nparams; i++) {
      const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];
      w_u32_at(w, param_record + i * D12_RS_PARAM_SIZE + 8,
               (uint32_t)w->used);

      switch (p->ParameterType) {
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
         const D3D12_ROOT_DESCRIPTOR_TABLE *t = &p->DescriptorTable;
         w_u32(w, t->NumDescriptorRanges);
         /* Ranges follow immediately after this 8-byte record. */
         w_u32(w, (uint32_t)(w->used + 4));

         for (uint32_t r = 0; r < t->NumDescriptorRanges; r++) {
            const D3D12_DESCRIPTOR_RANGE *rr = &t->pDescriptorRanges[r];
            w_u32(w, rr->RangeType);
            w_u32(w, rr->NumDescriptors);
            w_u32(w, rr->BaseShaderRegister);
            w_u32(w, rr->RegisterSpace);
            w_u32(w, rr->OffsetInDescriptorsFromTableStart);
         }
         break;
      }

      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         w_u32(w, p->Constants.ShaderRegister);
         w_u32(w, p->Constants.RegisterSpace);
         w_u32(w, p->Constants.Num32BitValues);
         break;

      case D3D12_ROOT_PARAMETER_TYPE_CBV:
      case D3D12_ROOT_PARAMETER_TYPE_SRV:
      case D3D12_ROOT_PARAMETER_TYPE_UAV:
         w_u32(w, p->Descriptor.ShaderRegister);
         w_u32(w, p->Descriptor.RegisterSpace);
         break;

      default:
         return false;
      }
   }

   /* Static samplers. Recorded faithfully so the blob round-trips, even
    * though nothing consumes them until texturing.
    */
   uint32_t samplers_off = (uint32_t)w->used;
   for (uint32_t i = 0; i < nsamplers; i++) {
      const D3D12_STATIC_SAMPLER_DESC *s = &desc->pStaticSamplers[i];
      w_u32(w, s->Filter);
      w_u32(w, s->AddressU);
      w_u32(w, s->AddressV);
      w_u32(w, s->AddressW);
      union { float f; uint32_t u; } mip = {.f = s->MipLODBias};
      w_u32(w, mip.u);
      w_u32(w, s->MaxAnisotropy);
      w_u32(w, s->ComparisonFunc);
      w_u32(w, s->BorderColor);
      union { float f; uint32_t u; } lo = {.f = s->MinLOD};
      union { float f; uint32_t u; } hi = {.f = s->MaxLOD};
      w_u32(w, lo.u);
      w_u32(w, hi.u);
      w_u32(w, s->ShaderRegister);
      w_u32(w, s->RegisterSpace);
      w_u32(w, s->ShaderVisibility);
   }

   w_u32_at(w, 16, samplers_off);
   return !w->failed;
}

/* Root signature 1.1 is wire version 2.  Parameter records and static
 * samplers keep their 1.0 shape; descriptor ranges and root descriptors add
 * the flags word described by D3D12_ROOT_SIGNATURE_DESC1.
 */
static bool
serialize_rts0_1_1(const D3D12_ROOT_SIGNATURE_DESC1 *desc,
                    struct d12_writer *w)
{
   const uint32_t nparams = desc->NumParameters;
   const uint32_t nsamplers = desc->NumStaticSamplers;
   const uint32_t params_off = D12_RS_HEADER_SIZE;

   w_u32(w, 2); /* D3D_ROOT_SIGNATURE_VERSION_1_1 */
   w_u32(w, nparams);
   w_u32(w, params_off);
   w_u32(w, nsamplers);
   w_u32(w, 0); /* static sampler offset, patched below */
   w_u32(w, desc->Flags);

   size_t param_record = w->used;
   for (uint32_t i = 0; i < nparams; i++) {
      w_u32(w, desc->pParameters[i].ParameterType);
      w_u32(w, desc->pParameters[i].ShaderVisibility);
      w_u32(w, 0);
   }

   for (uint32_t i = 0; i < nparams; i++) {
      const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];
      w_u32_at(w, param_record + i * D12_RS_PARAM_SIZE + 8,
               (uint32_t)w->used);

      switch (p->ParameterType) {
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
         const D3D12_ROOT_DESCRIPTOR_TABLE1 *t = &p->DescriptorTable;
         if (t->NumDescriptorRanges && !t->pDescriptorRanges)
            return false;

         w_u32(w, t->NumDescriptorRanges);
         w_u32(w, (uint32_t)(w->used + 4));

         for (uint32_t r = 0; r < t->NumDescriptorRanges; r++) {
            const D3D12_DESCRIPTOR_RANGE1 *rr = &t->pDescriptorRanges[r];
            w_u32(w, rr->RangeType);
            w_u32(w, rr->NumDescriptors);
            w_u32(w, rr->BaseShaderRegister);
            w_u32(w, rr->RegisterSpace);
            w_u32(w, rr->Flags);
            w_u32(w, rr->OffsetInDescriptorsFromTableStart);
         }
         break;
      }

      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         w_u32(w, p->Constants.ShaderRegister);
         w_u32(w, p->Constants.RegisterSpace);
         w_u32(w, p->Constants.Num32BitValues);
         break;

      case D3D12_ROOT_PARAMETER_TYPE_CBV:
      case D3D12_ROOT_PARAMETER_TYPE_SRV:
      case D3D12_ROOT_PARAMETER_TYPE_UAV:
         w_u32(w, p->Descriptor.ShaderRegister);
         w_u32(w, p->Descriptor.RegisterSpace);
         w_u32(w, p->Descriptor.Flags);
         break;

      default:
         return false;
      }
   }

   uint32_t samplers_off = (uint32_t)w->used;
   for (uint32_t i = 0; i < nsamplers; i++) {
      const D3D12_STATIC_SAMPLER_DESC *s = &desc->pStaticSamplers[i];
      w_u32(w, s->Filter);
      w_u32(w, s->AddressU);
      w_u32(w, s->AddressV);
      w_u32(w, s->AddressW);
      union {
         float f;
         uint32_t u;
      } mip = {.f = s->MipLODBias};
      w_u32(w, mip.u);
      w_u32(w, s->MaxAnisotropy);
      w_u32(w, s->ComparisonFunc);
      w_u32(w, s->BorderColor);
      union {
         float f;
         uint32_t u;
      } lo = {.f = s->MinLOD};
      union {
         float f;
         uint32_t u;
      } hi = {.f = s->MaxLOD};
      w_u32(w, lo.u);
      w_u32(w, hi.u);
      w_u32(w, s->ShaderRegister);
      w_u32(w, s->RegisterSpace);
      w_u32(w, s->ShaderVisibility);
   }

   w_u32_at(w, 16, samplers_off);
   return !w->failed;
}

static HRESULT
wrap_rts0(const struct d12_writer *payload, void **out_blob, size_t *out_size)
{
   /* Wrap in a DXBC container: tag, 16-byte checksum, version, total size,
    * part count, one part offset, then the part.
    */
   const size_t header = 4 + 16 + 4 + 4 + 4 + 4;
   const size_t total = header + 8 + payload->used;

   struct d12_writer out = {0};
   w_reserve(&out, total);
   if (out.failed)
      return E_OUTOFMEMORY;

   w_u32(&out, D12_TAG_DXBC);
   /* The checksum is an MD5-like digest that only the D3D runtime's own
    * loaders verify. We do not compute one, and we do not pretend to: it
    * is left zero so a blob of ours is identifiable as such.
    */
   for (unsigned i = 0; i < 4; i++)
      w_u32(&out, 0);
   w_u32(&out, 1);                  /* container version */
   w_u32(&out, (uint32_t)total);
   w_u32(&out, 1);                  /* part count */
   w_u32(&out, (uint32_t)header);   /* part[0] offset */
   w_u32(&out, D12_TAG_RTS0);
   w_u32(&out, (uint32_t)payload->used);

   w_reserve(&out, payload->used);
   if (out.failed) {
      free(out.data);
      return E_OUTOFMEMORY;
   }
   memcpy(out.data + out.used, payload->data, payload->used);
   out.used += payload->used;

   *out_blob = out.data;
   *out_size = out.used;
   return S_OK;
}

HRESULT
d12_serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC *desc,
                             void **out_blob, size_t *out_size)
{
   if (!desc || !out_blob || !out_size)
      return E_INVALIDARG;

   if (desc->NumParameters && !desc->pParameters)
      return E_INVALIDARG;
   if (desc->NumStaticSamplers && !desc->pStaticSamplers)
      return E_INVALIDARG;

   struct d12_writer payload = {0};
   if (!serialize_rts0(desc, &payload)) {
      free(payload.data);
      return E_INVALIDARG;
   }

   HRESULT hr = wrap_rts0(&payload, out_blob, out_size);
   free(payload.data);
   return hr;
}

HRESULT
d12_serialize_versioned_root_signature(
   const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, void **out_blob,
   size_t *out_size)
{
   if (!desc || !out_blob || !out_size)
      return E_INVALIDARG;

   if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
      return d12_serialize_root_signature(&desc->Desc_1_0, out_blob,
                                          out_size);

   if (desc->Version != D3D_ROOT_SIGNATURE_VERSION_1_1)
      return E_INVALIDARG;

   const D3D12_ROOT_SIGNATURE_DESC1 *v1 = &desc->Desc_1_1;
   if ((v1->NumParameters && !v1->pParameters) ||
       (v1->NumStaticSamplers && !v1->pStaticSamplers))
      return E_INVALIDARG;

   struct d12_writer payload = {0};
   if (!serialize_rts0_1_1(v1, &payload)) {
      free(payload.data);
      return E_INVALIDARG;
   }

   HRESULT hr = wrap_rts0(&payload, out_blob, out_size);
   free(payload.data);
   return hr;
}

/*
 * Parse. Bounds are checked against the part, not the file: a blob from an
 * application is untrusted input, and an offset that runs past the end must
 * be a rejection rather than a read into whatever follows.
 */
static bool
find_rts0(const void *blob, size_t size, const uint8_t **out, size_t *out_size)
{
   const uint8_t *p = blob;

   if (size < 32 || r_u32(p) != D12_TAG_DXBC)
      return false;

   uint32_t total = r_u32(p + 24);
   uint32_t nparts = r_u32(p + 28);
   if (total > size || nparts == 0 || nparts > 64)
      return false;
   if (32 + (size_t)nparts * 4 > size)
      return false;

   for (uint32_t i = 0; i < nparts; i++) {
      uint32_t off = r_u32(p + 32 + i * 4);
      /* size_t, or the addition wraps: off = 0xFFFFFFFC makes this 4, the
       * check passes, and the read below lands ~4 GiB past the blob. The
       * blob is application-supplied.
       */
      if ((size_t)off + 8 > size)
         continue;
      if (r_u32(p + off) != D12_TAG_RTS0)
         continue;

      uint32_t part_size = r_u32(p + off + 4);
      if ((size_t)off + 8 + part_size > size)
         return false;

      *out = p + off + 8;
      *out_size = part_size;
      return true;
   }

   return false;
}

bool
d12_root_signature_parse(const void *blob, size_t blob_size,
                         struct d12_root_layout *out)
{
   memset(out, 0, sizeof(*out));

   const uint8_t *rs;
   size_t rs_size;
   if (!find_rts0(blob, blob_size, &rs, &rs_size)) {
      mesa_loge("root signature blob has no RTS0 part");
      return false;
   }

   if (rs_size < D12_RS_HEADER_SIZE) {
      mesa_loge("root signature part is truncated");
      return false;
   }

   uint32_t version = r_u32(rs + 0);
   uint32_t nparams = r_u32(rs + 4);
   uint32_t params_off = r_u32(rs + 8);
   out->flags = r_u32(rs + 20);

   if (version != 1 && version != 2) {
      mesa_loge("root signature version %u unsupported", version);
      return false;
   }

   /* Version 1.1 adds a flags word to each descriptor range and to root
    * descriptors. The extra word only describes volatility promises the
    * application makes, so it changes the stride, not the meaning.
    */
   const uint32_t range_size =
      version == 2 ? D12_RS_RANGE_SIZE + 4 : D12_RS_RANGE_SIZE;
   const uint32_t descriptor_size =
      version == 2 ? D12_RS_DESCRIPTOR_SIZE + 4 : D12_RS_DESCRIPTOR_SIZE;

   if (nparams > D12_MAX_ROOT_PARAMS) {
      mesa_loge("root signature has %u parameters, limit is %u", nparams,
                D12_MAX_ROOT_PARAMS);
      return false;
   }

   if ((size_t)params_off + (size_t)nparams * D12_RS_PARAM_SIZE > rs_size) {
      mesa_loge("root signature parameter table runs past the blob");
      return false;
   }

   out->num_params = nparams;

   for (uint32_t i = 0; i < nparams; i++) {
      const uint8_t *rec = rs + params_off + i * D12_RS_PARAM_SIZE;
      struct d12_root_param *rp = &out->params[i];

      rp->type = r_u32(rec + 0);
      rp->visibility = r_u32(rec + 4);
      uint32_t payload = r_u32(rec + 8);

      switch (rp->type) {
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
         if ((size_t)payload + D12_RS_TABLE_SIZE > rs_size)
            goto truncated;

         uint32_t nranges = r_u32(rs + payload);
         uint32_t ranges_off = r_u32(rs + payload + 4);

         if (nranges > D12_MAX_ROOT_RANGES) {
            mesa_loge("descriptor table has %u ranges, limit is %u", nranges,
                      D12_MAX_ROOT_RANGES);
            return false;
         }
         if ((size_t)ranges_off + (size_t)nranges * range_size > rs_size)
            goto truncated;

         rp->num_ranges = nranges;
         for (uint32_t r = 0; r < nranges; r++) {
            const uint8_t *rr = rs + ranges_off + r * range_size;
            struct d12_root_range *out_range = &rp->ranges[r];

            out_range->type = r_u32(rr + 0);
            out_range->num_descriptors = r_u32(rr + 4);
            out_range->base_register = r_u32(rr + 8);
            out_range->space = r_u32(rr + 12);
            /* v1.1 puts its flags word before the offset. */
            out_range->offset_in_descriptors =
               r_u32(rr + (version == 2 ? 20 : 16));
         }

         /*
          * A descriptor table is bound to ONE heap, and D3D12 has two heap
          * types a shader can see -- CBV/SRV/UAV, and SAMPLER. A table
          * mixing sampler ranges with any of the others could not be
          * satisfied by a single heap, so the API rejects it outright.
          *
          * test_create_root_signature builds exactly that table and expects
          * E_INVALIDARG. Accepting it also leaked: the test only releases
          * the root signature on the paths where creation was supposed to
          * fail, so a stray success showed up a second time as a device
          * refcount left at 1.
          */
         if (nranges > 0) {
            bool has_sampler = false, has_other = false;

            for (uint32_t r = 0; r < nranges; r++) {
               if (rp->ranges[r].type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                  has_sampler = true;
               else
                  has_other = true;
            }

            if (has_sampler && has_other) {
               mesa_loge("descriptor table mixes sampler and non-sampler "
                         "ranges; no single heap can satisfy it");
               return false;
            }
         }
         break;
      }

      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         if ((size_t)payload + D12_RS_CONSTANTS_SIZE > rs_size)
            goto truncated;
         rp->base_register = r_u32(rs + payload + 0);
         rp->space = r_u32(rs + payload + 4);
         rp->num_constants = r_u32(rs + payload + 8);
         break;

      case D3D12_ROOT_PARAMETER_TYPE_CBV:
      case D3D12_ROOT_PARAMETER_TYPE_SRV:
      case D3D12_ROOT_PARAMETER_TYPE_UAV:
         if ((size_t)payload + descriptor_size > rs_size)
            goto truncated;
         rp->base_register = r_u32(rs + payload + 0);
         rp->space = r_u32(rs + payload + 4);
         break;

      default:
         mesa_loge("root parameter %u has unknown type %u", i, rp->type);
         return false;
      }
   }

   return true;

truncated:
   mesa_loge("root signature parameter payload runs past the blob");
   return false;
}

/*
 * The resource class a root descriptor parameter binds.
 */
unsigned
d12_root_param_class(const struct d12_root_param *rp)
{
   switch (rp->type) {
   case D3D12_ROOT_PARAMETER_TYPE_SRV:
      return D12_RES_SRV;
   case D3D12_ROOT_PARAMETER_TYPE_UAV:
      return D12_RES_UAV;
   case D3D12_ROOT_PARAMETER_TYPE_CBV:
   case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
      return D12_RES_CBV;
   default:
      return ~0u;
   }
}

unsigned
d12_descriptor_range_class(D3D12_DESCRIPTOR_RANGE_TYPE type)
{
   switch (type) {
   case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
      return D12_RES_SRV;
   case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
      return D12_RES_UAV;
   case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
      return D12_RES_CBV;
   case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
      return D12_RES_SAMPLER;
   default:
      return ~0u;
   }
}

/*
 * ID3DBlob, the container D3D12 hands serialized root signatures back in.
 * Five methods and a byte array; the payload trails the object so there is
 * one allocation and one free.
 */
struct d12_blob {
   ID3DBlob iface; /* must be first */
   uint32_t refcount;
   size_t size;
   uint8_t data[];
};

static HRESULT STDMETHODCALLTYPE
d12_blob_QueryInterface(ID3DBlob *iface, REFIID riid, void **out)
{
   struct d12_blob *blob = (struct d12_blob *)iface;

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D10Blob)) {
      p_atomic_inc(&blob->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3DBlob::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_blob_AddRef(ID3DBlob *iface)
{
   struct d12_blob *blob = (struct d12_blob *)iface;
   return p_atomic_inc_return(&blob->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_blob_Release(ID3DBlob *iface)
{
   struct d12_blob *blob = (struct d12_blob *)iface;
   uint32_t refs = p_atomic_dec_return(&blob->refcount);

   if (refs == 0)
      free(blob);

   return refs;
}

static LPVOID STDMETHODCALLTYPE
d12_blob_GetBufferPointer(ID3DBlob *iface)
{
   struct d12_blob *blob = (struct d12_blob *)iface;
   return blob->data;
}

static SIZE_T STDMETHODCALLTYPE
d12_blob_GetBufferSize(ID3DBlob *iface)
{
   struct d12_blob *blob = (struct d12_blob *)iface;
   return blob->size;
}

static const ID3D10BlobVtbl d12_blob_vtbl = {
   .QueryInterface = d12_blob_QueryInterface,
   .AddRef = d12_blob_AddRef,
   .Release = d12_blob_Release,
   .GetBufferPointer = d12_blob_GetBufferPointer,
   .GetBufferSize = d12_blob_GetBufferSize,
};

ID3DBlob *
d12_blob_create(const void *data, size_t size)
{
   struct d12_blob *blob = malloc(sizeof(*blob) + size);
   if (!blob)
      return NULL;

   blob->iface.lpVtbl = (ID3D10BlobVtbl *)&d12_blob_vtbl;
   blob->refcount = 1;
   blob->size = size;
   memcpy(blob->data, data, size);
   return &blob->iface;
}

/*
 * Assign a binding slot to every resource the signature can bind, in root
 * parameter order. This is the assignment BOTH sides use: shaders are
 * compiled against it and the command list fills the table through it, so
 * it has to be a property of the signature rather than something either
 * side works out for itself.
 *
 * Root constants get a slot too. They appear to a shader as a constant
 * buffer at their register, so they need somewhere for their address to go
 * once the values are uploaded at draw time.
 */
void
d12_root_layout_build_map(const struct d12_root_layout *layout,
                          struct d12_binding_map *out)
{
   memset(out, 0, sizeof(*out));
   unsigned root_range = 0;

   for (uint32_t i = 0; i < layout->num_params; i++) {
      const struct d12_root_param *rp = &layout->params[i];

      switch (rp->type) {
      case D3D12_ROOT_PARAMETER_TYPE_CBV:
      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         d12_binding_map_add_range(out, D12_RES_CBV, rp->space,
                                   rp->base_register, root_range++);
         break;

      case D3D12_ROOT_PARAMETER_TYPE_SRV:
         d12_binding_map_add_range(out, D12_RES_SRV, rp->space,
                                   rp->base_register, root_range++);
         break;

      case D3D12_ROOT_PARAMETER_TYPE_UAV:
         d12_binding_map_add_range(out, D12_RES_UAV, rp->space,
                                   rp->base_register, root_range++);
         break;

      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
         for (uint32_t r = 0; r < rp->num_ranges; r++) {
            const struct d12_root_range *range = &rp->ranges[r];
            unsigned klass = d12_descriptor_range_class(range->type);
            unsigned this_range = root_range++;
            if (klass >= D12_RES_CLASS_COUNT)
               continue;

            /* An unbounded range cannot be enumerated; it is rejected at
             * bind time with a message about bindless.
             */
            if (range->num_descriptors == UINT32_MAX)
               continue;

            for (uint32_t d = 0; d < range->num_descriptors; d++) {
               d12_binding_map_add_range(out, klass, range->space,
                                         range->base_register + d,
                                         this_range);
            }
         }
         break;

      default:
         break;
      }
   }
}
