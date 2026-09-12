/* SPDX-License-Identifier: MIT
 * Reproduce Firefox/GFXReconstruct's captured BDA buffer at the end of a BO.
 * Run only through gpu-experiment.py with the watchdog/thermal guard.
 */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { VkResult r=(x); if(r!=VK_SUCCESS) {fprintf(stderr,"%s: %d\n",#x,r); exit(1);} } while(0)
int main(void) {
 VkInstance i; VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_2};
 VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
 CHECK(vkCreateInstance(&ici,0,&i));
 uint32_t n=1; VkPhysicalDevice p; CHECK(vkEnumeratePhysicalDevices(i,&n,&p));
 VkPhysicalDeviceVulkan12Features f={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.bufferDeviceAddress=1,.bufferDeviceAddressCaptureReplay=1};
 float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
 VkDevice d; CHECK(vkCreateDevice(p,&di,0,&d));
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.flags=VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT,.size=1048576,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
 VkBuffer b; CHECK(vkCreateBuffer(d,&bi,0,&b));
 VkMemoryRequirements req; vkGetBufferMemoryRequirements(d,b,&req);
 unsigned type=0; while(type<32 && !(req.memoryTypeBits&(1u<<type))) ++type;
 if(type==32 || req.size>1048576 || 1048576%req.alignment) return 2;
 VkMemoryAllocateFlagsInfo flags={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT|VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT};
 VkMemoryAllocateInfo mi={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&flags,.allocationSize=2097152,.memoryTypeIndex=type};
 VkDeviceMemory m; CHECK(vkAllocateMemory(d,&mi,0,&m));
 CHECK(vkBindBufferMemory(d,b,m,1048576));
 VkBufferDeviceAddressInfo addr={.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer=b};
 if(!vkGetBufferDeviceAddress(d,&addr)) return 3;
 vkDestroyBuffer(d,b,0); vkFreeMemory(d,m,0); vkDestroyDevice(d,0); vkDestroyInstance(i,0);
 puts("PASS: captured BDA buffer binds at allocation end");
}
