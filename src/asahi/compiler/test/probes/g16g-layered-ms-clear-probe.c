/* SPDX-License-Identifier: MIT
 * Layered multisample clear oracle mirroring dEQP-VK.api.image_clearing
 * clear_color_attachment.multiple_layers.*_sample_count_4: a layered 4x
 * attachment view (base/count) is load-op cleared to INIT, a layer range is
 * vkCmdClearAttachments-cleared to CLEAR, and the pass resolves into a 1x
 * image of the same layering. Every view layer is read back.
 * cc -O2 -Wall layered-ms-clear-probe.c -o layered-ms-clear-probe -lvulkan
 * usage: probe [layers=16] [viewbase=3] [viewcount=12] [clearbase=2]
 *              [clearcount=5] [w=64] [h=11] [fmt: 0 rgba32f, 1 rg32f,
 *              2 rgba16f] [samples=4] [resolve: 0 in-pass, 1 vkCmdResolveImage
 *              after a STORE pass, 2 copy the MS image sample 0 via a
 *              1x resolve of layer counts] 
 * Run only inside gpu-experiment.py on hardware.
 */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { VkResult r=(x); if(r!=VK_SUCCESS) { fprintf(stderr,"%s: %d\n",#x,r); exit(1); } } while(0)
static VkPhysicalDeviceMemoryProperties memory;
static uint32_t memtype(uint32_t bits,VkMemoryPropertyFlags flags) {
 for(unsigned i=0;i<memory.memoryTypeCount;i++)
  if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
 fprintf(stderr,"No memory type\n");exit(1);
}
static VkDevice dev; static int host_ms; static VkDeviceSize ms_size;
static VkImage make_image(VkFormat format,unsigned w,unsigned h,unsigned layers,VkSampleCountFlagBits samples,VkImageUsageFlags usage,VkDeviceMemory *mem) {
 VkImage image;VkMemoryRequirements req;
 VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,
  .extent={w,h,1},.mipLevels=1,.arrayLayers=layers,.samples=samples,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
 CHECK(vkCreateImage(dev,&ci,NULL,&image));vkGetImageMemoryRequirements(dev,image,&req);
 const int host = host_ms && samples > 1; if (host) ms_size = req.size;
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memtype(req.memoryTypeBits,host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:0)};
 CHECK(vkAllocateMemory(dev,&ai,NULL,mem));CHECK(vkBindImageMemory(dev,image,*mem,0));
 return image;
}
int main(int argc, char **argv) {
 const unsigned layers = argc>1?atoi(argv[1]):16, vbase = argc>2?atoi(argv[2]):3, vcount = argc>3?atoi(argv[3]):12;
 const unsigned cbase = argc>4?atoi(argv[4]):2, ccount = argc>5?atoi(argv[5]):5;
 const unsigned w = argc>6?atoi(argv[6]):64, h = argc>7?atoi(argv[7]):11; const int fmtsel = argc>8?atoi(argv[8]):0;
 const unsigned nsamples = argc>9?atoi(argv[9]):4; const int post_resolve = argc>10?atoi(argv[10]):0;
 host_ms = argc>11?atoi(argv[11]):0; /* 1: place the MS image in host-visible memory and dump its raw 16-byte texel classes */
 const int preinit = argc>12?atoi(argv[12]):0; /* 1: vkCmdClearColorImage the MS image to INIT before the pass and use LOAD_OP_LOAD */
 const VkFormat format = fmtsel==1?VK_FORMAT_R32G32_SFLOAT:fmtsel==2?VK_FORMAT_R16G16B16A16_SFLOAT:VK_FORMAT_R32G32B32A32_SFLOAT;
 const unsigned comps = fmtsel==1?2:4, pixel_size = fmtsel==2?8:4*comps;
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
 CHECK(vkCreateDevice(physical,&di,NULL,&dev));VkQueue queue;vkGetDeviceQueue(dev,family,0,&queue);
 VkDeviceMemory msmem, ssmem, bm; VkBuffer buffer; VkMemoryRequirements req;
 const int ms = nsamples > 1;
 VkImage msimage = ms ? make_image(format,w,h,layers,(VkSampleCountFlagBits)nsamples,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,&msmem) : VK_NULL_HANDLE;
 VkImage ssimage = make_image(format,w,h,layers,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,&ssmem);
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=(VkDeviceSize)w*h*pixel_size*vcount,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
 CHECK(vkCreateBuffer(dev,&bi,NULL,&buffer));vkGetBufferMemoryRequirements(dev,buffer,&req);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memtype(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
 CHECK(vkAllocateMemory(dev,&ai,NULL,&bm));CHECK(vkBindBufferMemory(dev,buffer,bm,0));
 VkImageView views[2]; unsigned nviews = 0;
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY,
  .format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,vbase,vcount}};
 if (ms) { vi.image=msimage; CHECK(vkCreateImageView(dev,&vi,NULL,&views[nviews++])); }
 vi.image=ssimage; CHECK(vkCreateImageView(dev,&vi,NULL,&views[nviews++]));
 VkAttachmentDescription ad[2]={{.format=format,.samples=(VkSampleCountFlagBits)nsamples,.loadOp=preinit?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
  .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=preinit?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
  {.format=format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
  .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
 VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, rref={1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref,.pResolveAttachments=(ms&&!post_resolve)?&rref:NULL};
 VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=post_resolve?1:nviews,.pAttachments=ad,.subpassCount=1,.pSubpasses=&sub};
 VkRenderPass pass;CHECK(vkCreateRenderPass(dev,&ri,NULL,&pass));
 VkFramebuffer framebuffer;VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=post_resolve?1:nviews,.pAttachments=views,.width=w,.height=h,.layers=vcount};
 CHECK(vkCreateFramebuffer(dev,&fi,NULL,&framebuffer));
 VkCommandPool pool;VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};CHECK(vkCreateCommandPool(dev,&pi,NULL,&pool));
 VkCommandBuffer cmd;VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};CHECK(vkAllocateCommandBuffers(dev,&ca,&cmd));
 VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
 const float init[4]={0.2f,0.1f,0.7f,0.8f}, clr[4]={0.1f,0.5f,0.3f,0.9f};
 VkClearValue clears[2]={{.color.float32={init[0],init[1],init[2],init[3]}},{.color.float32={0,0,0,0}}};
 VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea={{0,0},{w,h}},.clearValueCount=nviews,.pClearValues=clears};
 VkClearAttachment ac={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=0,.clearValue.color.float32={clr[0],clr[1],clr[2],clr[3]}};
 VkClearRect rect={.rect={{0,0},{w,h}},.baseArrayLayer=cbase,.layerCount=ccount};
 if (preinit && ms) {
  VkImageMemoryBarrier tb={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
   .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=msimage,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers}};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&tb);
  VkClearColorValue cc={.float32={init[0],init[1],init[2],init[3]}}; VkImageSubresourceRange all={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers};
  vkCmdClearColorImage(cmd,msimage,VK_IMAGE_LAYOUT_GENERAL,&cc,1,&all);
  VkImageMemoryBarrier tb2={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
   .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=msimage,.subresourceRange=all};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&tb2);
 }
 vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);
 if (ccount) vkCmdClearAttachments(cmd,1,&ac,1,&rect);
 vkCmdEndRenderPass(cmd);
 if (post_resolve) {
  VkImageMemoryBarrier pre[2]={{.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
   .oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=msimage,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers}},
   {.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
   .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=ssimage,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers}}};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2,pre);
  VkImageResolve res={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,vbase,vcount},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,vbase,vcount},.extent={w,h,1}};
  vkCmdResolveImage(cmd,msimage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,ssimage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&res);
  VkImageMemoryBarrier post={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
   .oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=ssimage,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers}};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&post);
 } else {
 VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  .newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=ssimage,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
 }
 VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,vbase,vcount},.imageExtent={w,h,1}};
 vkCmdCopyImageToBuffer(cmd,ssimage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
 VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
 CHECK(vkEndCommandBuffer(cmd));VkFence fence;VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};CHECK(vkCreateFence(dev,&fc,NULL,&fence));
 VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&si,fence));
 CHECK(vkWaitForFences(dev,1,&fence,VK_TRUE,10000000000ull));
 if (host_ms && ms) {
  uint8_t *raw; CHECK(vkMapMemory(dev,msmem,0,VK_WHOLE_SIZE,0,(void**)&raw));
  /* classes: 0 zero, 1 full init colour, 2 rg-only (ba zero), 3 other */
  int prev=-1; VkDeviceSize start=0; unsigned counts[4]={0,0,0,0};
  for (VkDeviceSize off=0; off+16<=ms_size; off+=16) {
   float v[4]; memcpy(v,raw+off,16); int cls;
   if (v[0]==0&&v[1]==0&&v[2]==0&&v[3]==0) cls=0;
   else if (fabsf(v[0]-init[0])<1e-6f&&fabsf(v[1]-init[1])<1e-6f&&fabsf(v[2]-init[2])<1e-6f&&fabsf(v[3]-init[3])<1e-6f) cls=1;
   else if (fabsf(v[0]-init[0])<1e-6f&&fabsf(v[1]-init[1])<1e-6f&&v[2]==0&&v[3]==0) cls=2;
   else if (fabsf(v[0]-clr[0])<1e-6f&&fabsf(v[1]-clr[1])<1e-6f&&fabsf(v[2]-clr[2])<1e-6f&&fabsf(v[3]-clr[3])<1e-6f) cls=1;
   else cls=3;
   counts[cls]++;
   if (cls!=prev) { if (prev>=0) printf("raw run class %d: [0x%llx, 0x%llx)\n",prev,(unsigned long long)start,(unsigned long long)off); prev=cls; start=off; }
  }
  if (prev>=0) printf("raw run class %d: [0x%llx, 0x%llx)\n",prev,(unsigned long long)start,(unsigned long long)ms_size);
  /* Compact 4 KiB block map: '.' zero, 'F' full, 'r' rg-only, '?' other, 'm' mixed */
  printf("raw 4K map:");
  for (VkDeviceSize blk=0; blk<ms_size; blk+=4096) {
   unsigned c[4]={0,0,0,0};
   for (VkDeviceSize off=blk; off<blk+4096 && off+16<=ms_size; off+=16) {
    float v[4]; memcpy(v,raw+off,16); int cls;
    if (v[0]==0&&v[1]==0&&v[2]==0&&v[3]==0) cls=0;
    else if (fabsf(v[0]-init[0])<1e-6f&&fabsf(v[1]-init[1])<1e-6f&&fabsf(v[2]-init[2])<1e-6f&&fabsf(v[3]-init[3])<1e-6f) cls=1;
    else if (fabsf(v[0]-init[0])<1e-6f&&fabsf(v[1]-init[1])<1e-6f&&v[2]==0&&v[3]==0) cls=2;
    else cls=3;
    c[cls]++;
   }
   if ((blk/4096)%64==0) printf("\n%08llx ",(unsigned long long)blk);
   char ch = (c[1]&&!c[2]&&!c[3]) ? 'F' : (c[2]&&!c[1]&&!c[3]) ? 'r' : (!c[1]&&!c[2]&&!c[3]) ? '.' : (c[3]) ? '?' : 'm';
   putchar(ch);
  }
  printf("\n");
  printf("raw texel classes: zero=%u full=%u rg-only=%u other=%u size=0x%llx\n",counts[0],counts[1],counts[2],counts[3],(unsigned long long)ms_size);
  vkUnmapMemory(dev,msmem);
 }
 uint8_t *data;CHECK(vkMapMemory(dev,bm,0,VK_WHOLE_SIZE,0,(void**)&data));unsigned errors=0;
 for(unsigned l=0;l<vcount;l++) {
  const float *exp = (l>=cbase && l<cbase+ccount) ? clr : init; unsigned lerr=0; float first[4]={0,0,0,0}; unsigned tilebad[64]={0};
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
   const uint8_t *px = data + ((size_t)l*w*h + y*w + x)*pixel_size; float v[4]={0,0,0,1};
   if (fmtsel==2) { for(unsigned c=0;c<4;c++){ uint16_t hv; memcpy(&hv,px+2*c,2); unsigned e=(hv>>10)&31, m=hv&1023; v[c]=(hv&0x8000?-1.f:1.f)*(e?ldexpf(1.f+m/1024.f,(int)e-15):ldexpf(m/1024.f,-14)); } }
   else memcpy(v,px,4*comps);
   int bad=0; for(unsigned c=0;c<comps;c++) if(fabsf(v[c]-exp[c])>0.002f) bad=1;
   if(bad){ if(!lerr) memcpy(first,v,sizeof(first)); lerr++; tilebad[(y/16)*8+(x/16)]++; }
  }
  if(lerr){ errors+=lerr; printf("layer %u (image %u, %s): %u bad pixels, first %g %g %g %g expected %g %g %g %g tiles:",l,l+vbase,(exp==clr)?"cleared":"init",lerr,first[0],first[1],first[2],first[3],exp[0],exp[1],exp[2],exp[3]);
   for(unsigned t=0;t<64;t++) if(tilebad[t]) printf(" [%u,%u]=%u",t%8,t/8,tilebad[t]); printf("\n"); }
 }
 printf("LAYERED_MS_CLEAR_RESULT: %s errors=%u layers=%u view=%u+%u clear=%u+%u %ux%u fmt=%d samples=%u\n",errors?"FAIL":"PASS",errors,layers,vbase,vcount,cbase,ccount,w,h,fmtsel,nsamples);
 vkUnmapMemory(dev,bm);vkDestroyFence(dev,fence,NULL);vkDestroyCommandPool(dev,pool,NULL);vkDestroyFramebuffer(dev,framebuffer,NULL);
 vkDestroyRenderPass(dev,pass,NULL);for(unsigned i=0;i<nviews;i++)vkDestroyImageView(dev,views[i],NULL);
 if(ms){vkDestroyImage(dev,msimage,NULL);vkFreeMemory(dev,msmem,NULL);} vkDestroyImage(dev,ssimage,NULL);vkFreeMemory(dev,ssmem,NULL);
 vkDestroyBuffer(dev,buffer,NULL);vkFreeMemory(dev,bm,NULL);vkDestroyDevice(dev,NULL);vkDestroyInstance(instance,NULL);
 return errors?1:0;
}
