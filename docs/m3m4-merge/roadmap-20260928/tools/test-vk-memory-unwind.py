#!/usr/bin/env python3
"""Exercise production Vulkan memory allocation rollback with injected failures.

Offline mocks cover heap accounting, read-only alias failure, imported-fd
ownership and alignment overflow. Hardware external-memory tests stay separate.
"""
from pathlib import Path
import subprocess,tempfile
src=Path('mesa/m3m4-merged/src/asahi/vulkan/hk_device_memory.c').read_text()
start=src.index('VKAPI_ATTR VkResult VKAPI_CALL\nhk_AllocateMemory(')
end=src.index('VKAPI_ATTR VkResult VKAPI_CALL\nhk_MapMemory2KHR(',start)
methods=src[start:end]
harness=r'''
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define AGX_BO_SHARED 1
#define AGX_BO_SHAREABLE 2
#define AGX_BO_WRITEBACK 4
#define DRM_ASAHI_BIND_READ 2
enum agx_bo_flags {PLACEHOLDER};
struct agx_bo{unsigned flags;size_t size;struct {uint64_t addr;} *va;};
struct agx_device{int unused;};
struct vk_device_memory{uint32_t memory_type_index;};
struct hk_device_memory{struct vk_device_memory vk;struct agx_bo *bo;void *map;};
struct hk_memory_heap{uint64_t used,size;};
struct hk_physical_device{VkMemoryType mem_types[1];struct hk_memory_heap mem_heaps[1];};
struct hk_device{int vk;struct agx_device dev;};
#define VK_FROM_HANDLE(type,name,handle) struct type *name=(void*)(handle)
#define vk_error(dev,code) (code)
#define vk_errorf(dev,code,...) (code)
#define p_atomic_read(p) (*(p))
#define p_atomic_add_return(p,v) (*(p)+=(v))
#define p_atomic_add(p,v) (*(p)+=(v))
#define align64(v,a) (((v)+(a)-1)&~((uint64_t)(a)-1))
static int failure,mems,bos,exts,caller_fd_closed;
static struct hk_physical_device physical;
static struct agx_bo bo;
static struct hk_physical_device *hk_device_physical(struct hk_device*d){(void)d;return &physical;}
static const void *find_struct(const void *chain,VkStructureType t){for(const VkBaseInStructure*p=chain;p;p=p->pNext)if(p->sType==t)return p;return NULL;}
#define vk_find_struct_const(chain,type) find_struct(chain,VK_STRUCTURE_TYPE_##type)
static unsigned hk_memory_type_flags(const VkMemoryType*t,unsigned flags){(void)t;return flags?(AGX_BO_SHARED|AGX_BO_SHAREABLE):0;}
static struct hk_device_memory *vk_device_memory_create(int*d,const VkMemoryAllocateInfo*ai,const VkAllocationCallbacks*cb,size_t size){(void)d;(void)cb;if(failure==1)return NULL;struct hk_device_memory*m=calloc(1,size);assert(m);m->vk.memory_type_index=ai->memoryTypeIndex;++mems;return m;}
static void vk_device_memory_destroy(int*d,const VkAllocationCallbacks*cb,struct vk_device_memory*m){(void)d;(void)cb;assert(mems==1);--mems;free(m);}
static struct agx_bo *agx_bo_create(struct agx_device*d,uint64_t size,int align,enum agx_bo_flags flags,const char*label){(void)d;(void)align;(void)label;if(failure==2)return NULL;assert(!bos);bos=1;bo.size=size;bo.flags=flags;static typeof(*bo.va) va={.addr=0x1000};bo.va=&va;return &bo;}
static struct agx_bo *agx_bo_import(struct agx_device*d,int fd){assert(fd==70);struct agx_bo*b=agx_bo_create(d,16384,0,AGX_BO_SHARED|AGX_BO_SHAREABLE,"");if(failure==4)physical.mem_heaps[0].used=physical.mem_heaps[0].size;return b;}
static int agx_bo_bind(struct agx_device*d,struct agx_bo*b,uint64_t addr,size_t size,int off,unsigned flags){(void)d;(void)b;(void)addr;(void)size;(void)off;assert(flags==DRM_ASAHI_BIND_READ);return failure==3?-1:0;}
static uint64_t agx_rw_addr_to_ro(struct agx_device*d,uint64_t va){(void)d;return va+0x1000000;}
static void agx_bo_unreference(struct agx_device*d,struct agx_bo*b){(void)d;assert(b==&bo && bos==1);--bos;}
static void hk_add_ext_bo(struct hk_device*d,struct agx_bo*b){(void)d;(void)b;assert(!exts);++exts;}
static void hk_remove_ext_bo(struct hk_device*d,struct agx_bo*b){(void)d;(void)b;assert(exts==1);--exts;}
static int test_close(int fd){assert(fd==70 && !caller_fd_closed);caller_fd_closed=1;return 0;}
#define close test_close
static VkDeviceMemory hk_device_memory_to_handle(struct hk_device_memory*m){return (VkDeviceMemory)m;}
static void hk_UnmapMemory2KHR(VkDevice d,const VkMemoryUnmapInfo*i){(void)d;(void)i;abort();}
VKAPI_ATTR void VKAPI_CALL hk_FreeMemory(VkDevice,VkDeviceMemory,const VkAllocationCallbacks*);
/*METHODS*/
int main(void){
 struct hk_device d={0};physical.mem_types[0].heapIndex=0;
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=16384};
 VkImportMemoryFdInfoKHR fd={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,.fd=70};
 for(int imported=0;imported<2;++imported){
  ai.pNext=imported?&fd:NULL;
  for(failure=0;failure<=4;++failure){
   if(failure==4 && !imported)continue;
   physical.mem_heaps[0]=(struct hk_memory_heap){.used=0,.size=1048576};caller_fd_closed=0;
   VkDeviceMemory mem=VK_NULL_HANDLE;
   VkResult r=hk_AllocateMemory((VkDevice)&d,&ai,NULL,&mem);
   if(!failure){assert(r==VK_SUCCESS && mem);assert(caller_fd_closed==imported);hk_FreeMemory((VkDevice)&d,mem,NULL);}
   else {assert(r!=VK_SUCCESS);assert(!caller_fd_closed);}
   assert(!mems && !bos && !exts);
   assert(physical.mem_heaps[0].used==(failure==4?physical.mem_heaps[0].size:0));
  }
 }
 failure=0;ai.pNext=NULL;physical.mem_heaps[0].used=0;
 for(uint64_t n=1;n<16384;++n){ai.allocationSize=UINT64_MAX-(n-1);VkDeviceMemory mem=VK_NULL_HANDLE;assert(hk_AllocateMemory((VkDevice)&d,&ai,NULL,&mem)==VK_ERROR_OUT_OF_DEVICE_MEMORY);assert(!mems && !bos);}
 puts("PASS memory construction/unwind, alias failure, heap rollback, imported-fd ownership and alignment overflow");
}
'''.replace('/*METHODS*/',methods)
with tempfile.TemporaryDirectory(prefix='vk-memory-unwind-') as temp:
    p=Path(temp);(p/'test.c').write_text(harness)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-O2','-Imesa/m3m4-merged/include',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
