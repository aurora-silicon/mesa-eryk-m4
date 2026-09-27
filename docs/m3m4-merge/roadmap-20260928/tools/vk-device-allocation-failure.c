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
int main(int argc,char **argv) {
    CHECK(argc==3);
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
        struct state *s=calloc(1,sizeof(*s));CHECK(s);CHECK(!pthread_mutex_init(&s->lock,NULL));s->fail_at=failure;
        VkAllocationCallbacks cb={.pUserData=s,.pfnAllocation=allocate,.pfnReallocation=reallocate,.pfnFree=release};
        VkDevice device=VK_NULL_HANDLE;VkResult result=vkCreateDevice(physical,&di,&cb,&device);
        CHECK(result==VK_SUCCESS || result==VK_ERROR_OUT_OF_HOST_MEMORY);
        if(result==VK_SUCCESS){CHECK(device);vkDestroyDevice(device,&cb);}else{CHECK(s->injected);}
        fprintf(report,"{\"fail_at\":%u,\"calls\":%u,\"result\":%d,\"injected\":%s,\"live\":%u,\"peak\":%u}\n",failure,s->calls,result,s->injected?"true":"false",s->count,s->peak);fflush(report);
        CHECK(!s->count);
        if(!failure){CHECK(result==VK_SUCCESS);total=s->calls;CHECK(total>0 && total<=256);}
        else CHECK(s->injected);
        CHECK(!pthread_mutex_destroy(&s->lock));free(s);
    }
    vkDestroyInstance(instance,NULL);fclose(report);dlclose(loader);
    printf("DEVICE_ALLOCATION_FAILURE_PASS tested=%u\n",total);return 0;
}
