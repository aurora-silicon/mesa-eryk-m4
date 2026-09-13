/* SPDX-License-Identifier: MIT
 * Exact packed BGR8 / RGB9E5 attachment clear/reload oracle, including mip/layer views.
 * cc -O2 -Wall -Wextra vulkan-attachment-probe.c -o vulkan-attachment-probe -lvulkan
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
int main(int argc, char **argv) {
 const int rgb9 = argc > 1 && !strcmp(argv[1], "--rgb9e5");
 const unsigned pixel_size = rgb9 ? 4 : 3;
 const VkFormat format = rgb9 ? VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 : VK_FORMAT_B8G8R8_UINT;
 const unsigned w=16,h=9;
 VkInstance instance;
 VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
 CHECK(vkCreateInstance(&ic,NULL,&instance));
 uint32_t n=1;VkPhysicalDevice physical;CHECK(vkEnumeratePhysicalDevices(instance,&n,&physical));
 VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
 printf("Device: %s\n",properties.deviceName);fflush(stdout);
 vkGetPhysicalDeviceMemoryProperties(physical,&memory);
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,NULL);
 VkQueueFamilyProperties *families=calloc(n,sizeof(*families));
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families);
 unsigned family=0;while(family<n && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;
 if(family==n)return 1;
 free(families);
 float priority=1;
 VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
 VkDevice dev;CHECK(vkCreateDevice(physical,&di,NULL,&dev));VkQueue queue;vkGetDeviceQueue(dev,family,0,&queue);
 VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(physical,format,&fp);
 if(!(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)){fprintf(stderr,"No requested attachment support\n");return 1;}
 VkImage image;VkDeviceMemory im,bm;VkBuffer buffer;VkMemoryRequirements req;
 VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,
  .extent={2*w,2*h,1},.mipLevels=2,.arrayLayers=2,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
  .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
 CHECK(vkCreateImage(dev,&ci,NULL,&image));vkGetImageMemoryRequirements(dev,image,&req);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memtype(req.memoryTypeBits,0)};
 CHECK(vkAllocateMemory(dev,&ai,NULL,&im));CHECK(vkBindImageMemory(dev,image,im,0));
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=w*h*pixel_size,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
 CHECK(vkCreateBuffer(dev,&bi,NULL,&buffer));vkGetBufferMemoryRequirements(dev,buffer,&req);
 ai.allocationSize=req.size;ai.memoryTypeIndex=memtype(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
 CHECK(vkAllocateMemory(dev,&ai,NULL,&bm));CHECK(vkBindBufferMemory(dev,buffer,bm,0));
 VkImageView view;VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
  .format=ci.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,1,1,1,1}};
 CHECK(vkCreateImageView(dev,&vi,NULL,&view));
 VkAttachmentDescription ad={.format=ci.format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
  .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};
 VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&ad,.subpassCount=1,.pSubpasses=&sub};
 VkRenderPass pass[2];CHECK(vkCreateRenderPass(dev,&ri,NULL,&pass[0]));
 ad.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;ad.initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 CHECK(vkCreateRenderPass(dev,&ri,NULL,&pass[1]));
 VkFramebuffer framebuffer;VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass[0],.attachmentCount=1,.pAttachments=&view,.width=w,.height=h,.layers=1};
 CHECK(vkCreateFramebuffer(dev,&fi,NULL,&framebuffer));
 VkCommandPool pool;VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};CHECK(vkCreateCommandPool(dev,&pi,NULL,&pool));
 VkCommandBuffer cmd;VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};CHECK(vkAllocateCommandBuffers(dev,&ca,&cmd));
 VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
 VkClearValue clear={.color.uint32={17,93,201,1}};
 if(rgb9) clear=(VkClearValue){.color.float32={0.25f,0.5f,0.75f,1}};
 VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass[0],.framebuffer=framebuffer,.renderArea={{0,0},{w,h}},.clearValueCount=1,.pClearValues=&clear};
 VkClearAttachment ac={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=0,.clearValue.color.uint32={233,71,29,1}};
 if(rgb9) ac.clearValue=(VkClearValue){.color.float32={0.75f,0.25f,0.5f,1}};
 VkClearRect rect={.rect={{3,2},{7,4}},.baseArrayLayer=0,.layerCount=1};
 vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdClearAttachments(cmd,1,&ac,1,&rect);vkCmdEndRenderPass(cmd);
 VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  .newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange=vi.subresourceRange};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&barrier);
 rb.renderPass=pass[1];ac.clearValue.color.uint32[0]=41;ac.clearValue.color.uint32[1]=199;ac.clearValue.color.uint32[2]=127;
 if(rgb9) ac.clearValue=(VkClearValue){.color.float32={0.5f,0.75f,0.25f,1}};
 rect.rect.offset=(VkOffset2D){8,5};rect.rect.extent=(VkExtent2D){5,3};
 vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdClearAttachments(cmd,1,&ac,1,&rect);vkCmdEndRenderPass(cmd);
 barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
 VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,1,1,1},.imageExtent={w,h,1}};
 vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
 VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
 CHECK(vkEndCommandBuffer(cmd));VkFence fence;VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};CHECK(vkCreateFence(dev,&fc,NULL,&fence));
 VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&si,fence));
 CHECK(vkWaitForFences(dev,1,&fence,VK_TRUE,10000000000ull));
 uint8_t *data;CHECK(vkMapMemory(dev,bm,0,VK_WHOLE_SIZE,0,(void**)&data));unsigned errors=0;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
  if(rgb9) {
   unsigned r=128,g=256,b=384;
   if(x>=3&&x<10&&y>=2&&y<6){r=384;g=128;b=256;}
   if(x>=8&&x<13&&y>=5&&y<8){r=256;g=384;b=128;}
   const uint32_t expected=r|(g<<9)|(b<<18)|(15u<<27);
   uint32_t actual; memcpy(&actual,data+4*(y*w+x),4);
   if(actual!=expected){if(errors<8)printf("Mismatch %u,%u: %08x expected %08x\n",x,y,actual,expected);errors++;}
   continue;
  }
  uint8_t expected[3]={201,93,17};
  if(x>=3&&x<10&&y>=2&&y<6){expected[0]=29;expected[1]=71;expected[2]=233;}
  if(x>=8&&x<13&&y>=5&&y<8){expected[0]=127;expected[1]=199;expected[2]=41;}
  if(memcmp(data+3*(y*w+x),expected,3)) {if(errors<8)printf("Mismatch %u,%u: %u %u %u expected %u %u %u\n",x,y,data[3*(y*w+x)],data[3*(y*w+x)+1],data[3*(y*w+x)+2],expected[0],expected[1],expected[2]);errors++;}
 }
 printf("%s: %s errors=%u pixels=%u mip=1 layer=1\n",rgb9?"RGB9E5_ATTACHMENT_RESULT":"BGR_ATTACHMENT_RESULT",errors?"FAIL":"PASS",errors,w*h);
 vkUnmapMemory(dev,bm);vkDestroyFence(dev,fence,NULL);vkDestroyCommandPool(dev,pool,NULL);vkDestroyFramebuffer(dev,framebuffer,NULL);
 vkDestroyRenderPass(dev,pass[0],NULL);vkDestroyRenderPass(dev,pass[1],NULL);vkDestroyImageView(dev,view,NULL);vkDestroyImage(dev,image,NULL);
 vkDestroyBuffer(dev,buffer,NULL);vkFreeMemory(dev,im,NULL);vkFreeMemory(dev,bm,NULL);vkDestroyDevice(dev,NULL);vkDestroyInstance(instance,NULL);
 return errors?1:0;
}
