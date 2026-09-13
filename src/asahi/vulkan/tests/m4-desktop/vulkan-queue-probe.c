/* SPDX-License-Identifier: MIT
 * Reverse-order timeline semaphore dependencies across eight independent queues.
 * cc -O2 -Wall -Wextra vulkan-queue-probe.c -o vulkan-queue-probe -lvulkan
 * Run only inside gpu-experiment.py on hardware. No window system required.
 */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { VkResult r=(x); if(r!=VK_SUCCESS) { fprintf(stderr,"%s: %d\n",#x,r); exit(1); } } while(0)
static VkPhysicalDeviceMemoryProperties memory;
static uint32_t memtype(uint32_t bits,VkMemoryPropertyFlags flags) {
 for(unsigned i=0;i<memory.memoryTypeCount;i++)
  if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
 fprintf(stderr,"No memory type\n");exit(1);
}
int main(void) {
 const unsigned queue_count=8;
 VkInstance instance;
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_2};
 VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
 CHECK(vkCreateInstance(&ic,NULL,&instance));
 uint32_t n=1;VkPhysicalDevice physical;CHECK(vkEnumeratePhysicalDevices(instance,&n,&physical));
 VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
 printf("Device: %s\n",properties.deviceName);fflush(stdout);
 vkGetPhysicalDeviceMemoryProperties(physical,&memory);
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,NULL);
 VkQueueFamilyProperties *families=calloc(n,sizeof(*families));
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families);
 unsigned family=0;while(family<n && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;
 if(family==n || families[family].queueCount<queue_count){fprintf(stderr,"Need eight queues\n");return 1;}
 free(families);
 float priority[8]={1,1,1,1,1,1,1,1};
 VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=queue_count,.pQueuePriorities=priority};
 VkPhysicalDeviceTimelineSemaphoreFeatures tf={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,.timelineSemaphore=VK_TRUE};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&tf,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
 VkDevice dev;CHECK(vkCreateDevice(physical,&di,NULL,&dev));VkQueue queue[8];for(unsigned i=0;i<queue_count;i++)vkGetDeviceQueue(dev,family,i,&queue[i]);

 VkBuffer buffer;VkDeviceMemory allocation;VkMemoryRequirements req;
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=8*256,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
 CHECK(vkCreateBuffer(dev,&bi,NULL,&buffer));vkGetBufferMemoryRequirements(dev,buffer,&req);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
  .memoryTypeIndex=memtype(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
 CHECK(vkAllocateMemory(dev,&ai,NULL,&allocation));CHECK(vkBindBufferMemory(dev,buffer,allocation,0));
 uint32_t *data;CHECK(vkMapMemory(dev,allocation,0,VK_WHOLE_SIZE,0,(void**)&data));memset(data,0,8*256);
 VkCommandPool pool;VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};CHECK(vkCreateCommandPool(dev,&pi,NULL,&pool));
 VkCommandBuffer cmds[8];VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=8};CHECK(vkAllocateCommandBuffers(dev,&ca,cmds));
 VkFence fences[8];VkSemaphore sem[4];VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
 VkSemaphoreTypeCreateInfo st={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE};
 VkSemaphoreCreateInfo sc={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&st};
 for(unsigned i=0;i<8;i++)CHECK(vkCreateFence(dev,&fc,NULL,&fences[i]));
 for(unsigned i=0;i<4;i++)CHECK(vkCreateSemaphore(dev,&sc,NULL,&sem[i]));
 for(unsigned round=0;round<2;round++) {
  if(round){CHECK(vkResetCommandPool(dev,pool,0));CHECK(vkResetFences(dev,8,fences));}
  for(unsigned pair=0;pair<4;pair++) {
   unsigned consumer=2*pair+round,producer=2*pair+1-round;
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   CHECK(vkBeginCommandBuffer(cmds[producer],&begin));
   vkCmdFillBuffer(cmds[producer],buffer,256*pair,256,0x71300000+round*16+pair);
   CHECK(vkEndCommandBuffer(cmds[producer]));
   CHECK(vkBeginCommandBuffer(cmds[consumer],&begin));
   VkBufferCopy copy={256*pair,256*(pair+4),256};vkCmdCopyBuffer(cmds[consumer],buffer,buffer,1,&copy);
   VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cmds[consumer],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
   CHECK(vkEndCommandBuffer(cmds[consumer]));
   VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
   uint64_t value=round+1;
   VkTimelineSemaphoreSubmitInfo timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,.waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};
   VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,.waitSemaphoreCount=1,.pWaitSemaphores=&sem[pair],.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&cmds[consumer]};
   /* Submit ALL consumers before ANY producer. A single FIFO queue would
    * deadlock. Distinct DRM scheduler entities must select ready producers. */
   CHECK(vkQueueSubmit(queue[consumer],1,&si,fences[consumer]));
  }
  for(unsigned pair=0;pair<4;pair++) {
   unsigned producer=2*pair+1-round;
   uint64_t value=round+1;
   VkTimelineSemaphoreSubmitInfo timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,.signalSemaphoreValueCount=1,.pSignalSemaphoreValues=&value};
   VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,.commandBufferCount=1,.pCommandBuffers=&cmds[producer],.signalSemaphoreCount=1,.pSignalSemaphores=&sem[pair]};
   CHECK(vkQueueSubmit(queue[producer],1,&si,fences[producer]));
  }
  CHECK(vkWaitForFences(dev,8,fences,VK_TRUE,10000000000ull));
  unsigned errors=0;
  for(unsigned pair=0;pair<4;pair++)for(unsigned j=0;j<64;j++)
   if(data[64*(pair+4)+j]!=(0x71300000+round*16+pair))errors++;
  printf("QUEUE_DEPENDENCY_ROUND: %u errors=%u\n",round,errors);fflush(stdout);if(errors)return 1;
 }
 printf("QUEUE_DEPENDENCY_RESULT: PASS queues=8 rounds=2 consumer-first=1\n");
 for(unsigned i=0;i<8;i++)vkDestroyFence(dev,fences[i],NULL);
 for(unsigned i=0;i<4;i++)vkDestroySemaphore(dev,sem[i],NULL);
 vkDestroyCommandPool(dev,pool,NULL);vkUnmapMemory(dev,allocation);vkDestroyBuffer(dev,buffer,NULL);vkFreeMemory(dev,allocation,NULL);
 vkDestroyDevice(dev,NULL);vkDestroyInstance(instance,NULL);return 0;
}
