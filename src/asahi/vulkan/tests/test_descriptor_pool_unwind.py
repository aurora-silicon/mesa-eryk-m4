#!/usr/bin/env python3
"""Fault-inject the actual descriptor-pool constructor without GPU execution."""
from pathlib import Path
import subprocess,tempfile
source = Path(__file__).resolve().parents[1] / 'hk_descriptor_set.c'
mesa = Path(__file__).resolve().parents[4]
s=source.read_text()
body=s[s.index('VKAPI_ATTR VkResult VKAPI_CALL\nhk_CreateDescriptorPool('):s.index('\nstatic VkResult\nhk_descriptor_pool_alloc(')]
harness=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#define AGX_CHIP_G15S 0
#define AGX_CHIP_G16G 1
/* A sentinel for argument propagation, not a reproduction of the BO ABI. */
#define AGX_BO_WRITEBACK 0x1234
#include <vulkan/vulkan.h>
#define HK_MIN_UBO_ALIGNMENT 32
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define align64(v,a) (((uint64_t)(v)+(a)-1)&~((uint64_t)(a)-1))
#define VK_FROM_HANDLE(t,n,h) struct t *n=(void*)(h)
#define vk_error(d,r) (r)
#define MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT 0
struct heap {int initialized;};
struct hk_physical_device {int unused;};
struct agx_device {int chip;};
struct hk_device {int vk;struct agx_device dev;};
struct agx_bo {uint64_t size;struct {uint64_t addr;} *va;};
struct hk_descriptor_pool {int sets;struct heap heap;struct agx_bo *bo;void *mapped_ptr;};
static int fail,objects,bos,cache_mode,requested_flags;static uint64_t requested;
static bool debug_get_bool_option(const char *name, bool fallback){assert(!strcmp(name,"HK_M3_WRITEBACK_DESCRIPTOR_POOLS"));assert(!fallback);return cache_mode<0?fallback:cache_mode==1;}
static void list_inithead(int*p){*p=1;}
static void util_vma_heap_init(struct heap*h,uint64_t addr,uint64_t size){(void)addr;(void)size;assert(!h->initialized);h->initialized=1;}
static void util_vma_heap_free(struct heap*h,uint64_t addr,uint64_t size){assert(h->initialized && addr && size);}
static void util_vma_heap_finish(struct heap*h){assert(h->initialized);h->initialized=0;}
static struct hk_physical_device*hk_device_physical(struct hk_device*d){(void)d;static struct hk_physical_device p;return &p;}
static void*vk_object_zalloc(int*d,const VkAllocationCallbacks*a,size_t n,int type){(void)d;(void)a;(void)type;if(fail==1)return NULL;objects++;return calloc(1,n);}
static const void*vk_find_struct_const(const void*p,int type){(void)p;(void)type;return NULL;}
static void hk_descriptor_stride_align_for_type(struct hk_physical_device*p,VkDescriptorType t,const VkMutableDescriptorTypeListEXT*l,uint32_t*s,uint32_t*a){(void)p;(void)l;*s=t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC?0:16;*a=*s;}
static struct agx_bo*agx_bo_create(struct agx_device*d,uint64_t n,int a,int f,const char*l){(void)d;(void)a;(void)l;requested_flags=f;requested=n;if(fail==2)return NULL;bos++;static struct agx_bo b;static typeof(*b.va) va={4096};b.size=n;b.va=&va;return &b;}
static void*agx_bo_map(struct agx_bo*b){(void)b;return fail==3?NULL:(void*)0x10000;}
static void hk_destroy_descriptor_pool(struct hk_device*d,const VkAllocationCallbacks*a,struct hk_descriptor_pool*p){(void)d;(void)a;util_vma_heap_finish(&p->heap);if(p->bo){assert(bos==1);bos--;}assert(objects==1);objects--;free(p);}
static VkDescriptorPool hk_descriptor_pool_to_handle(struct hk_descriptor_pool*p){return (VkDescriptorPool)p;}
/*BODY*/
int main(void){
 struct hk_device dev={0};
 VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7};
 VkDescriptorPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=3,.poolSizeCount=1,.pPoolSizes=&size};
 for(dev.dev.chip=AGX_CHIP_G15S;dev.dev.chip<=AGX_CHIP_G16G;dev.dev.chip++)
 for(cache_mode=-1;cache_mode<=1;cache_mode++)
 for(fail=0;fail<=3;fail++){
  requested_flags=-1;
  VkDescriptorPool out=VK_NULL_HANDLE;
  VkResult r=hk_CreateDescriptorPool((VkDevice)&dev,&ci,NULL,&out);
  if(fail!=1)assert(requested_flags==((dev.dev.chip==AGX_CHIP_G15S && cache_mode==1)?AGX_BO_WRITEBACK:0));
  if(fail){assert(r==(fail==2?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_ERROR_OUT_OF_HOST_MEMORY));assert(!out && !objects && !bos);}
  else{assert(r==VK_SUCCESS && out && requested==208);hk_destroy_descriptor_pool(&dev,NULL,(void*)out);}
 }
 fail=0;ci.maxSets=UINT32_MAX;size.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
 VkDescriptorPool out=VK_NULL_HANDLE;
 assert(hk_CreateDescriptorPool((VkDevice)&dev,&ci,NULL,&out)==VK_SUCCESS);
 assert(requested==(uint64_t)UINT32_MAX*32);
 hk_destroy_descriptor_pool(&dev,NULL,(void*)out);
 assert(!objects && !bos);
 puts("PASS descriptor wrapper/backing/map rollback under default/opt-in cache policies, other-chip isolation and wide alignment accounting");
}
'''.replace('/*BODY*/',body)
with tempfile.TemporaryDirectory(prefix='descriptor-pool-unwind-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(harness)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-O1','-g','-fsanitize=address,undefined','-I'+str(mesa/'include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
