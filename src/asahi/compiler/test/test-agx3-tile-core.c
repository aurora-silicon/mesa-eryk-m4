#include "agx3_tile_core.h"
#include "agx3_tile_core_vectors.c"
#include <stdio.h>
#include <string.h>
int main(void){
   unsigned pass=0,fail=0;
   for(size_t i=0;i<tile_vec_count;i++){
      const struct tile_vec *v=&tile_vecs[i];
      enum agx_isa_encoding e=v->enc==0?AGX_ISA_ENCODING_AGX3B:AGX_ISA_ENCODING_AGX3C;
      uint8_t out[AGX3_TILE_MAX_LENGTH]; uint8_t len=0; bool ok;
      if(!strcmp(v->op,"store")) ok=agx3_tile_store(e,v->data,v->w,v->mask,v->off,v->cov,v->wait,out,&len);
      else ok=agx3_tile_load(e,v->data,v->w,(uint8_t)v->slot,v->mask,v->off,v->cov,v->wait,v->first,v->last,out,&len);
      if(!ok||len!=v->nbytes||memcmp(out,v->bytes,v->nbytes)){ if(fail<8) fprintf(stderr,"FAIL %s enc=%d i=%zu\n",v->op,v->enc,i); fail++; } else pass++;
   }
   {
      static const uint8_t scalar_w16[] = {
         0x67,0x0e,0x54,0x00,0x00,0x00,0x01,0x82,0x02,0x00,0x00,0x00,
      };
      static const uint8_t scalar_w8[] = {
         0x67,0x0e,0x54,0x00,0x00,0x00,0x01,0xc2,0x02,0x00,0x00,0x00,
      };
      const struct { enum agx3_tile_width width; const uint8_t *bytes; } v[] = {
         {AGX3_TILE_W16, scalar_w16}, {AGX3_TILE_W8, scalar_w8},
      };
      for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
         uint8_t out[AGX3_TILE_MAX_LENGTH], len = 0;
         if (!agx3_tile_load(AGX_ISA_ENCODING_AGX3B, 0, v[i].width, 6,
                             1, 0, AGX3_TILE_COV_1X, 0, true, true,
                             out, &len) || len != sizeof(scalar_w8) ||
             memcmp(out, v[i].bytes, len))
            fail++;
         else
            pass++;
      }
   }
   {
      uint8_t out[AGX3_TILE_MAX_LENGTH]; uint8_t len=0;
      if(agx3_tile_store(AGX_ISA_ENCODING_AGX3B,0,AGX3_TILE_W32,15,0,
                         AGX3_TILE_COV_1X,0x40,out,&len)) fail++; else pass++;
      if(agx3_tile_load(AGX_ISA_ENCODING_AGX3C,1,AGX3_TILE_W32,1,15,0,
                        AGX3_TILE_COV_1X,0,true,true,out,&len)) fail++; else pass++;
      if(agx3_tile_store(AGX_ISA_ENCODING_AGX3B,0,AGX3_TILE_W32,15,0,
                         AGX3_TILE_COV_1X,3,out,&len)) fail++; else pass++;
      if(agx3_tile_load(AGX_ISA_ENCODING_AGX3C,0,AGX3_TILE_W32,1,7,0,
                        AGX3_TILE_COV_1X,0,true,true,out,&len)) fail++; else pass++;
      if(agx3_tile_load(AGX_ISA_ENCODING_AGX3C,0,AGX3_TILE_W32,1,15,0,
                        AGX3_TILE_COV_PER_SAMPLE,0,true,true,out,&len)) fail++; else pass++;
   }
   /* Native G16G four-sample framebuffer fetch, entry offset 64 in the
    * retained sample-fragment4 object. Each load selects one sample. */
   {
      const struct { uint8_t reg, slot, mask; bool first; uint8_t bytes[12]; } v[] = {
         {12, 1, 1, true, {0x67,0x0e,0x54,0x0c,0,0,1,0x4e,0,0,0,0}},
         {10, 2, 2, false,{0x67,0x06,0x54,0x0a,0,0,2,0xce,0,0,0,0}},
         {8, 3, 4, false, {0x67,0x06,0x54,8,0,0,4,0x4e,1,0,0,0}},
         {14, 6, 8, false,{0x67,0x06,0x54,0x0e,0,0,8,0xce,2,0,0,0}},
      };
      for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
         uint8_t out[AGX3_TILE_MAX_LENGTH], len = 0;
         if (!agx3_tile_load_mask(AGX_ISA_ENCODING_AGX3B, v[i].reg,
                                  AGX3_TILE_W8, v[i].slot, 15, 0, v[i].mask,
                                  0, v[i].first, false, out, &len) ||
             len != 12 || memcmp(out, v[i].bytes, 12)) {
            fprintf(stderr, "native MSAA load %u differs\n", i);
            fail++;
         } else pass++;
      }
   }
   /* Fresh G16G RG16Unorm framebuffer fetch: two raw W16 lanes. */
   {
      const uint8_t native[] = {0x67,0x0e,0x54,0,0,0,1,0x92,2,0,0,0};
      uint8_t out[AGX3_TILE_MAX_LENGTH], len;
      if (!agx3_tile_load_mask(AGX_ISA_ENCODING_AGX3B, 0, AGX3_TILE_W16,
                               6, 3, 0, 1, 0, true, true, out, &len) ||
          len != sizeof(native) || memcmp(out, native, len)) fail++; else pass++;
   }
   /* G16G RG8Unorm framebuffer fetch, native fragment text offset76. */
   {
      const uint8_t native[] = {0x67,0x0e,0x54,0,0,0,1,0xd2,2,0,0,0};
      uint8_t out[AGX3_TILE_MAX_LENGTH], len;
      if (!agx3_tile_load_mask(AGX_ISA_ENCODING_AGX3B, 0, AGX3_TILE_W8,
                               6, 3, 0, 1, 0, true, true, out, &len) ||
          len != sizeof(native) || memcmp(out, native, len)) fail++; else pass++;
   }
   /* G16G native write-mask4: LSTCOL at182 selects SR39 in half2. */
   {
      const uint8_t native[] = {0xe7,0x26,0x54,0,0,0,2,0x4e,8,0,0,8};
      uint8_t out[AGX3_TILE_MAX_LENGTH], len;
      if (!agx3_tile_store_register_mask(AGX_ISA_ENCODING_AGX3B, 0,
              AGX3_TILE_W8, 15, 0, 2, true, 2, out, &len) ||
          len != sizeof(native) || memcmp(out, native, len)) {
         fprintf(stderr, "native register sample mask mismatch: ");
         for (unsigned i = 0; i < len; i++) fprintf(stderr, "%02x", out[i]);
         fprintf(stderr, " len=%u\n", len);
         fail++;
      } else pass++;
      if (agx3_tile_store_register_mask(AGX_ISA_ENCODING_AGX3B, 0,
              AGX3_TILE_W8, 15, 0, 128, true, 2, out, &len) || len) fail++; else pass++;
      if (agx3_tile_store_register_mask(AGX_ISA_ENCODING_AGX3C, 0,
              AGX3_TILE_W8, 15, 0, 2, true, 2, out, &len) || len) fail++; else pass++;
   }
   if(fail){ fprintf(stderr,"agx3_tile_core: %u of %zu failed\n",fail,tile_vec_count); return 1; }
   printf("agx3_tile_core: all %u tile ops reproduced\n",pass); return 0;
}
