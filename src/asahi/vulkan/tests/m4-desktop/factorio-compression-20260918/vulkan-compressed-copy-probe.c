/* SPDX-License-Identifier: MIT
 * Exact-byte RGBA8/BGRA8 copies, including partial rectangles and preserved
 * surroundings. Enable HK_M4_COMPRESSED_2D to exercise lossless compression.
 * cc -O2 -Wall vulkan-compressed-copy-probe.c -lvulkan -o probe
 * Hardware execution must use gpu-experiment.py's watchdog/thermal guard.
 */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { VkResult r=(x); if(r) {fprintf(stderr,"%s: %d\n",#x,r);exit(1);} } while(0)
#define W 256
#define H 256
#define BYTES (W*H*4)
static VkPhysicalDeviceMemoryProperties props;
static unsigned type(unsigned bits, VkMemoryPropertyFlags flags) {
 for(unsigned i=0;i<props.memoryTypeCount;i++)
  if((bits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&flags)==flags)return i;
 abort();
}
static void barrier(VkCommandBuffer cb) {
 VkMemoryBarrier m={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
 .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT};
 vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&m,0,NULL,0,NULL);
}
int main(void) {
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};
 VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};VkInstance inst;CHECK(vkCreateInstance(&ic,NULL,&inst));
 unsigned n=1;VkPhysicalDevice pd;CHECK(vkEnumeratePhysicalDevices(inst,&n,&pd));vkGetPhysicalDeviceMemoryProperties(pd,&props);
 vkGetPhysicalDeviceQueueFamilyProperties(pd,&n,NULL);VkQueueFamilyProperties *families=calloc(n,sizeof(*families));vkGetPhysicalDeviceQueueFamilyProperties(pd,&n,families);
 unsigned family=0;while(family<n && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;if(family==n)abort();free(families);
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice dev;CHECK(vkCreateDevice(pd,&di,NULL,&dev));VkQueue q;vkGetDeviceQueue(dev,family,0,&q);
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=BYTES*3,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};VkBuffer buf;CHECK(vkCreateBuffer(dev,&bi,NULL,&buf));VkMemoryRequirements req;vkGetBufferMemoryRequirements(dev,buf,&req);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};VkDeviceMemory mem;CHECK(vkAllocateMemory(dev,&ai,NULL,&mem));CHECK(vkBindBufferMemory(dev,buf,mem,0));unsigned char *data;CHECK(vkMapMemory(dev,mem,0,VK_WHOLE_SIZE,0,(void**)&data));
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)for(unsigned c=0;c<4;c++)data[(y*W+x)*4+c]=(x*13+y*29+c*67)&255;
 memset(data+BYTES,0x93,BYTES);
 VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool pool;CHECK(vkCreateCommandPool(dev,&pi,NULL,&pool));VkCommandBufferAllocateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cb;CHECK(vkAllocateCommandBuffers(dev,&ci,&cb));
 unsigned bad=0;
 for(unsigned round=0;round<16;round++) {
  VkImage images[2];VkDeviceMemory imem[2];
  for(unsigned j=0;j<2;j++) {
   VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
    .format=((round+j)&1)?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM,.extent={W,H,1},.mipLevels=1,.arrayLayers=1,.samples=1,.tiling=VK_IMAGE_TILING_OPTIMAL,
    .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
   /* Storage usage deliberately makes just this image uncompressed. */
   if ((round / 4) & (1u << j)) ii.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
   CHECK(vkCreateImage(dev,&ii,NULL,&images[j]));vkGetImageMemoryRequirements(dev,images[j],&req);ai.allocationSize=req.size;ai.memoryTypeIndex=type(req.memoryTypeBits,0);CHECK(vkAllocateMemory(dev,&ai,NULL,&imem[j]));CHECK(vkBindImageMemory(dev,images[j],imem[j],0));
  }
  CHECK(vkResetCommandBuffer(cb,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cb,&begin));
  for(unsigned j=0;j<2;j++) {
   VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[j],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
   vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
   VkBufferImageCopy upload={.bufferOffset=BYTES*j,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={W,H,1}};
   vkCmdCopyBufferToImage(cb,buf,images[j],VK_IMAGE_LAYOUT_GENERAL,1,&upload);
  }
  barrier(cb);
  unsigned partial=(round&2)!=0;
  unsigned sx=partial?17:0, sy=partial?23:0, dx=partial?31:0,dy=partial?37:0,cw=partial?129:W,ch=partial?117:H;
  VkImageCopy copy={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.srcOffset={(int)sx,(int)sy,0},.dstOffset={(int)dx,(int)dy,0},.extent={cw,ch,1}};
  vkCmdCopyImage(cb,images[0],VK_IMAGE_LAYOUT_GENERAL,images[1],VK_IMAGE_LAYOUT_GENERAL,1,&copy);barrier(cb);
  VkBufferImageCopy read={.bufferOffset=BYTES*2,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={W,H,1}};vkCmdCopyImageToBuffer(cb,images[1],VK_IMAGE_LAYOUT_GENERAL,buf,1,&read);barrier(cb);
  CHECK(vkEndCommandBuffer(cb));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};CHECK(vkQueueSubmit(q,1,&submit,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(q));
  for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)for(unsigned c=0;c<4;c++) {
   unsigned expected=x>=dx&&x<dx+cw&&y>=dy&&y<dy+ch?data[((y-dy+sy)*W+x-dx+sx)*4+c]:0x93;
   if(data[BYTES*2+(y*W+x)*4+c]!=expected)bad++;
  }
  printf("round=%u partial=%u uncompressed_mask=%u bad=%u\n",round,partial,round/4,bad);fflush(stdout);
  for(unsigned j=0;j<2;j++){vkDestroyImage(dev,images[j],NULL);vkFreeMemory(dev,imem[j],NULL);}
 }
 vkDestroyCommandPool(dev,pool,NULL);vkUnmapMemory(dev,mem);vkDestroyBuffer(dev,buf,NULL);vkFreeMemory(dev,mem,NULL);vkDestroyDevice(dev,NULL);vkDestroyInstance(inst,NULL);
 if(bad)return 1;puts("PASS: exact compressed RGBA/BGRA image copies");return 0;
}
