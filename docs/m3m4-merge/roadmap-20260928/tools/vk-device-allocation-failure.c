/* SPDX-License-Identifier: MIT
 * Fail every observed Vulkan device-allocation callback in turn. No GPU work.
 * Run hardware enumeration/device creation under the watchdog/thermal guard.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CHECK failed at line %d: %s\n",__LINE__,#x); abort(); } } while (0)
#define MAX_ALLOCS 4096
struct allocation { void *ptr; size_t size; };
struct state {
    pthread_mutex_t lock;
    struct allocation live[MAX_ALLOCS];
    unsigned calls, fail_at, count, peak;
    bool injected;
};
static bool should_fail(struct state *s) {
    pthread_mutex_lock(&s->lock);
    bool fail = ++s->calls == s->fail_at;
    s->injected |= fail;
    pthread_mutex_unlock(&s->lock);
    return fail;
}
static void *allocate_raw(struct state *s, size_t size, size_t alignment) {
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    CHECK((alignment & (alignment - 1)) == 0);
    void *p = NULL;
    if (posix_memalign(&p, alignment, size)) return NULL;
    pthread_mutex_lock(&s->lock);
    unsigned slot;
    for (slot=0; slot<MAX_ALLOCS && s->live[slot].ptr; ++slot) {}
    CHECK(slot < MAX_ALLOCS);
    s->live[slot]=(struct allocation){p,size};
    ++s->count; if (s->count > s->peak) s->peak=s->count;
    pthread_mutex_unlock(&s->lock);
    return p;
}
static void release(void *data, void *ptr) {
    struct state *s=data;
    if (!ptr) return;
    pthread_mutex_lock(&s->lock);
    unsigned slot;
    for (slot=0; slot<MAX_ALLOCS && s->live[slot].ptr!=ptr; ++slot) {}
    CHECK(slot < MAX_ALLOCS); CHECK(s->count > 0);
    s->live[slot]=(struct allocation){0}; --s->count;
    pthread_mutex_unlock(&s->lock);
    free(ptr);
}
static void *allocate(void *data, size_t size, size_t alignment, VkSystemAllocationScope scope) {
    (void)scope;
    struct state *s=data;
    return should_fail(s) ? NULL : allocate_raw(s,size,alignment);
}
static void *reallocate(void *data, void *ptr, size_t size, size_t alignment, VkSystemAllocationScope scope) {
    (void)scope;
    struct state *s=data;
    if (!size) { release(data,ptr); return NULL; }
    if (should_fail(s)) return NULL; /* Original remains owned on failure. */
    size_t previous=0;
    if (ptr) {
        pthread_mutex_lock(&s->lock);
        unsigned slot;
        for (slot=0; slot<MAX_ALLOCS && s->live[slot].ptr!=ptr; ++slot) {}
        CHECK(slot < MAX_ALLOCS); previous=s->live[slot].size;
        pthread_mutex_unlock(&s->lock);
    }
    void *p=allocate_raw(s,size,alignment);
    if (!p) return NULL;
    if (ptr) memcpy(p,ptr,size < previous ? size : previous);
    release(data,ptr); return p;
}

/* Allocation failures during object construction, command recording and submit.
 * Each successful execution fills and reads back every word of a 16 KiB buffer.
 * Objects use the same callback owner as the device; every failure unwinds in
 * reverse order. A failed submit must not leave a live GPU referencing frees.
 */
static VkResult resource_work(PFN_vkGetInstanceProcAddr gipa, VkInstance instance,
    VkPhysicalDevice physical, VkDevice device, uint32_t family,
    const VkAllocationCallbacks *cb, bool fail_map) {
#define RLOAD(name) PFN_##name name=(PFN_##name)gipa(instance,#name);CHECK(name)
    RLOAD(vkCreateBuffer);RLOAD(vkDestroyBuffer);RLOAD(vkGetBufferMemoryRequirements);
    RLOAD(vkGetPhysicalDeviceMemoryProperties);RLOAD(vkAllocateMemory);RLOAD(vkFreeMemory);
    RLOAD(vkBindBufferMemory);RLOAD(vkMapMemory);RLOAD(vkUnmapMemory);
    RLOAD(vkCreateCommandPool);RLOAD(vkDestroyCommandPool);RLOAD(vkAllocateCommandBuffers);
    RLOAD(vkBeginCommandBuffer);RLOAD(vkEndCommandBuffer);RLOAD(vkCmdFillBuffer);
    RLOAD(vkCmdPipelineBarrier);RLOAD(vkCreateFence);RLOAD(vkDestroyFence);
    RLOAD(vkGetDeviceQueue);RLOAD(vkQueueSubmit);RLOAD(vkWaitForFences);RLOAD(vkDeviceWaitIdle);
#undef RLOAD
    VkResult result=VK_SUCCESS;
    VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE; VkFence fence=VK_NULL_HANDLE;
    void *mapped=NULL;
#define TRY(call) do { result=(call); if(result!=VK_SUCCESS) goto cleanup; } while(0)
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=16384,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    TRY(vkCreateBuffer(device,&bi,cb,&buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);
    VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(physical,&mp);
    uint32_t type=0;const VkMemoryPropertyFlags flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while(type<mp.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || (mp.memoryTypes[type].propertyFlags&flags)!=flags))++type;
    CHECK(type<mp.memoryTypeCount);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
    TRY(vkAllocateMemory(device,&ai,cb,&memory));
    TRY(vkBindBufferMemory(device,buffer,memory,0));
    TRY(vkMapMemory(device,memory,0,req.size,0,&mapped));
    memset(mapped,0,16384);
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    TRY(vkCreateCommandPool(device,&pi,cb,&pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command;TRY(vkAllocateCommandBuffers(device,&ca,&command));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if(fail_map){
        void (*arm_map)(void)=dlsym(RTLD_DEFAULT,"asahi_test_arm_map_failure");CHECK(arm_map);arm_map();
    }
    TRY(vkBeginCommandBuffer(command,&begin));
    vkCmdFillBuffer(command,buffer,0,16384,0xa5c39e17);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    TRY(vkEndCommandBuffer(command));
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    TRY(vkCreateFence(device,&fi,cb,&fence));
    VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
    TRY(vkQueueSubmit(queue,1,&si,fence));
    /* A submit accepted by the implementation owns these resources until its
     * fence completes. Never free timed-out work as an allocation-test result. */
    result=vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_C(10000000000));
    CHECK(result==VK_SUCCESS);
    for(unsigned i=0;i<16384/4;++i)CHECK(((uint32_t*)mapped)[i]==0xa5c39e17);
cleanup:
    /* Includes any partial work accepted before a submit allocation error. */
    VkResult idle=vkDeviceWaitIdle(device);
    if(idle==VK_ERROR_OUT_OF_HOST_MEMORY || idle==VK_ERROR_OUT_OF_DEVICE_MEMORY){
        /* WaitIdle itself can allocate a synchronization object. A one-shot
         * OOM must be retryable; it is not evidence of a GPU failure. */
        fprintf(stderr,"RESOURCE_PROBE cleanup_wait_result=%d\n",idle);
        if(result==VK_SUCCESS)result=idle;
        idle=vkDeviceWaitIdle(device);
    }
    CHECK(idle==VK_SUCCESS);
    if(fence)vkDestroyFence(device,fence,cb);
    if(pool)vkDestroyCommandPool(device,pool,cb);
    if(mapped)vkUnmapMemory(device,memory);
    if(buffer)vkDestroyBuffer(device,buffer,cb);
    if(memory)vkFreeMemory(device,memory,cb);
#undef TRY
    return result;
}
int main(int argc,char **argv) {
    CHECK(argc==3 || (argc==4 && (!strcmp(argv[3],"resources") || !strcmp(argv[3],"bind-failure") || !strcmp(argv[3],"map-failure"))));
    bool resources=argc==4;
    bool bind_failure=resources && !strcmp(argv[3],"bind-failure");
    bool map_failure=resources && !strcmp(argv[3],"map-failure");
    bool boundary_failure=bind_failure || map_failure;
    void (*arm_bind)(void)=NULL;
    int (*bind_injected)(void)=NULL;
    if(boundary_failure){
        arm_bind=dlsym(RTLD_DEFAULT,"asahi_test_arm_bind_failure");
        bind_injected=dlsym(RTLD_DEFAULT,map_failure?"asahi_test_map_failure_injected":"asahi_test_bind_failure_injected");
        CHECK((!bind_failure || arm_bind) && bind_injected);
    }
    char *end; unsigned long chip=strtoul(argv[1],&end,0);
    CHECK(!*end && (chip==0x6030 || chip==0x8132));
    FILE *report=fopen(argv[2],"wx"); CHECK(report);
    void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL); CHECK(loader);
    PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(loader,"vkGetInstanceProcAddr");CHECK(gipa);
    PFN_vkCreateInstance create_instance=(PFN_vkCreateInstance)gipa(VK_NULL_HANDLE,"vkCreateInstance");CHECK(create_instance);
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="allocation failure probe",.apiVersion=VK_API_VERSION_1_4};
    VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance;CHECK(create_instance(&ci,NULL,&instance)==VK_SUCCESS);
#define LOAD(name) PFN_##name name=(PFN_##name)gipa(instance,#name);CHECK(name)
    LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);
    LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);
    LOAD(vkDestroyDevice);LOAD(vkDestroyInstance);
    uint32_t count=0;CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL)==VK_SUCCESS);
    CHECK(count>0 && count<=16);VkPhysicalDevice physicals[16];
    CHECK(vkEnumeratePhysicalDevices(instance,&count,physicals)==VK_SUCCESS);
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    for (uint32_t i=0;i<count;++i){VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physicals[i],&props);if(props.deviceID==chip){CHECK(!physical);physical=physicals[i];}}
    CHECK(physical);
    uint32_t queues=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&queues,NULL);CHECK(queues && queues<=64);
    VkQueueFamilyProperties qp[64];vkGetPhysicalDeviceQueueFamilyProperties(physical,&queues,qp);
    uint32_t family=0;while(family<queues && !(qp[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;CHECK(family<queues);
    float priority=1.0f;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    unsigned total=0;
    for(unsigned failure=0;failure<=total;++failure){
        struct state *s=calloc(1,sizeof(*s));CHECK(s);CHECK(!pthread_mutex_init(&s->lock,NULL));s->fail_at=resources?0:failure;
        VkAllocationCallbacks cb={.pUserData=s,.pfnAllocation=allocate,.pfnReallocation=reallocate,.pfnFree=release};
        VkDevice device=VK_NULL_HANDLE;VkResult result=vkCreateDevice(physical,&di,&cb,&device);
        if(resources){
            CHECK(result==VK_SUCCESS);
            pthread_mutex_lock(&s->lock);
            s->calls=0;s->fail_at=failure;
            pthread_mutex_unlock(&s->lock);
            if(bind_failure)arm_bind();
            result=resource_work(gipa,instance,physical,device,family,&cb,map_failure);
            if(boundary_failure){
                fprintf(stderr,"BOUNDARY_FAILURE observed result=%d injected=%d\n",result,bind_injected());
                /* A failed disk-cache deserialize can legitimately compile a fresh
                 * shader. The successful path already checked every GPU word. */
                bool recovered_cache = map_failure && result==VK_SUCCESS &&
                    getenv("ASAHI_TEST_MAP_ALLOW_RETRY");
                CHECK(bind_injected() && (recovered_cache ||
                    result==(map_failure?VK_ERROR_OUT_OF_HOST_MEMORY:VK_ERROR_OUT_OF_DEVICE_MEMORY)));
                if(recovered_cache)fprintf(stderr,"MAP_FAILURE recovered_by_cache_fallback=1\n");
                CHECK(resource_work(gipa,instance,physical,device,family,&cb,false)==VK_SUCCESS);
            }
            vkDestroyDevice(device,&cb);device=VK_NULL_HANDLE;
        }
        CHECK(result==VK_SUCCESS || result==VK_ERROR_OUT_OF_HOST_MEMORY || result==VK_ERROR_OUT_OF_DEVICE_MEMORY);
        if(!resources && result==VK_SUCCESS){CHECK(device);vkDestroyDevice(device,&cb);}else if(result!=VK_SUCCESS){CHECK(s->injected || boundary_failure);}
        fprintf(report,"{\"fail_at\":%u,\"calls\":%u,\"result\":%d,\"injected\":%s,\"live\":%u,\"peak\":%u}\n",failure,s->calls,result,s->injected?"true":"false",s->count,s->peak);fflush(report);
        CHECK(!s->count);
        if(!failure){CHECK(result==VK_SUCCESS || boundary_failure);total=boundary_failure?0:s->calls;CHECK(total<=256 && (total>0 || boundary_failure));}
        else CHECK(s->injected);
        CHECK(!pthread_mutex_destroy(&s->lock));free(s);
    }
    vkDestroyInstance(instance,NULL);fclose(report);dlclose(loader);
    printf("DEVICE_ALLOCATION_FAILURE_PASS mode=%s tested=%u\n",map_failure?"map-failure":bind_failure?"bind-failure":resources?"resources":"device",boundary_failure?1:total);return 0;
}
