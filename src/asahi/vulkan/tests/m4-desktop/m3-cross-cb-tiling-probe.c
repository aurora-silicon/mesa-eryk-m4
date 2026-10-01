/* SPDX-License-Identifier: MIT
 * Independent render-CB overlap and barrier/timestamp/fence pixel oracle.
 * Run only under the watchdog/thermal guard; cc FILE -lvulkan -o PROBE.
 */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { VkResult r=(x); if(r!=VK_SUCCESS) {fprintf(stderr,"FAIL %d result=%d\n",__LINE__,r);exit(1);} } while(0)
static VkPhysicalDevice physical;
static VkDevice dev;
static unsigned memory_type(unsigned bits, unsigned flags) {
 VkPhysicalDeviceMemoryProperties p; vkGetPhysicalDeviceMemoryProperties(physical,&p);
 for(unsigned i=0;i<p.memoryTypeCount;i++) if((bits&(1u<<i)) && (p.memoryTypes[i].propertyFlags&flags)==flags) return i;
 abort();
}
static void barrier(VkCommandBuffer cb, VkPipelineStageFlags dest) {
 VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT};
 vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,dest,0,1,&b,0,NULL,0,NULL);
}
int main(void) {
 VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; VkInstance instance; CHECK(vkCreateInstance(&ii,NULL,&instance));
 unsigned n=1; CHECK(vkEnumeratePhysicalDevices(instance,&n,&physical));
 VkQueueFamilyProperties qp[16]; n=16; vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,qp); unsigned family=0;
 while(family<n && !(qp[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) family++; if(family==n) abort();
 float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi}; CHECK(vkCreateDevice(physical,&di,NULL,&dev));
 VkQueue queue; vkGetDeviceQueue(dev,family,0,&queue);
 enum{W=64,H=64,BYTES=W*H*4};
 VkImage images[2]; VkImageView views[2]; VkDeviceMemory imem[2]; VkFramebuffer fbs[2];
 VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};
 VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_GENERAL}; VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};
 VkSubpassDependency dep={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
 VkRenderPassCreateInfo rpinfo={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&sub,.dependencyCount=1,.pDependencies=&dep}; VkRenderPass rp; CHECK(vkCreateRenderPass(dev,&rpinfo,NULL,&rp));
 for(unsigned j=0;j<2;j++) {
  VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=attachment.format,.extent={W,H,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
  CHECK(vkCreateImage(dev,&ci,NULL,&images[j])); VkMemoryRequirements req; vkGetImageMemoryRequirements(dev,images[j],&req);
  VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,0)}; CHECK(vkAllocateMemory(dev,&ai,NULL,&imem[j])); CHECK(vkBindImageMemory(dev,images[j],imem[j],0));
  VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[j],.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=attachment.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}; CHECK(vkCreateImageView(dev,&vi,NULL,&views[j]));
  VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=rp,.attachmentCount=1,.pAttachments=&views[j],.width=W,.height=H,.layers=1}; CHECK(vkCreateFramebuffer(dev,&fi,NULL,&fbs[j]));
 }
 VkBuffer buffer; VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=2*BYTES,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT}; CHECK(vkCreateBuffer(dev,&bi,NULL,&buffer)); VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev,buffer,&req);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)}; VkDeviceMemory mem; CHECK(vkAllocateMemory(dev,&ai,NULL,&mem)); CHECK(vkBindBufferMemory(dev,buffer,mem,0)); unsigned char *pixels; CHECK(vkMapMemory(dev,mem,0,VK_WHOLE_SIZE,0,(void**)&pixels));
 VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family}; VkCommandPool pool; CHECK(vkCreateCommandPool(dev,&pi,NULL,&pool));
 VkCommandBuffer cbs[4]; VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=4}; CHECK(vkAllocateCommandBuffers(dev,&ca,cbs));
 VkFenceCreateInfo fence_info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; CHECK(vkCreateFence(dev,&fence_info,NULL,&fence));
 VkQueryPoolCreateInfo query_info={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=2}; VkQueryPool queries; CHECK(vkCreateQueryPool(dev,&query_info,NULL,&queries));
 VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
 CHECK(vkBeginCommandBuffer(cbs[3],&begin));
 for(unsigned j=0;j<2;j++) {
  VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[j],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  vkCmdPipelineBarrier(cbs[3],VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&ib);
 } CHECK(vkEndCommandBuffer(cbs[3])); VkSubmitInfo setup={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cbs[3]}; CHECK(vkQueueSubmit(queue,1,&setup,fence)); CHECK(vkWaitForFences(dev,1,&fence,VK_TRUE,1000000000)); CHECK(vkResetFences(dev,1,&fence));
 for(unsigned mode=0;mode<7;mode++) {
  fprintf(stderr,"PROBE_BEGIN mode=%u\n",mode);
  for(unsigned k=0;k<3;k++) {CHECK(vkResetCommandBuffer(cbs[k],0)); CHECK(vkBeginCommandBuffer(cbs[k],&begin));}
  vkCmdResetQueryPool(cbs[0],queries,0,2);
  for(unsigned j=0;j<2;j++) {
   if(j==1 && mode==1) barrier(cbs[1],VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
   if(j==1 && mode==5) vkCmdWriteTimestamp(cbs[1],VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0);
   VkRenderPassBeginInfo rbi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=rp,.framebuffer=fbs[j],.renderArea={{0,0},{W,H}}}; vkCmdBeginRenderPass(cbs[j],&rbi,VK_SUBPASS_CONTENTS_INLINE);
   VkClearAttachment clear={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=0,.clearValue.color.float32={j?0:1,j?1:0,0,1}}; VkClearRect rect={{{0,0},{W,H}},0,1}; vkCmdClearAttachments(cbs[j],1,&clear,1,&rect); vkCmdEndRenderPass(cbs[j]);
   if(j==0 && (mode==2 || mode==3)) barrier(cbs[0],mode==2?VK_PIPELINE_STAGE_VERTEX_SHADER_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  }
  if(mode==4) barrier(cbs[2],VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
  barrier(cbs[1],VK_PIPELINE_STAGE_TRANSFER_BIT);
  for(unsigned j=0;j<2;j++) {VkBufferImageCopy copy={.bufferOffset=j*BYTES,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={W,H,1}};vkCmdCopyImageToBuffer(cbs[1],images[j],VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);}
  VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(cbs[1],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
  for(unsigned k=0;k<3;k++) CHECK(vkEndCommandBuffer(cbs[k]));
  VkCommandBuffer ordered[]={cbs[0],cbs[2],cbs[1]}; VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=mode==4?3:2,.pCommandBuffers=mode==4?ordered:cbs};
  if(mode==6) {VkSubmitInfo first={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cbs[0]};CHECK(vkQueueSubmit(queue,1,&first,fence));CHECK(vkWaitForFences(dev,1,&fence,VK_TRUE,1000000000));CHECK(vkResetFences(dev,1,&fence));submit.commandBufferCount=1;submit.pCommandBuffers=&cbs[1];}
  CHECK(vkQueueSubmit(queue,1,&submit,fence)); CHECK(vkWaitForFences(dev,1,&fence,VK_TRUE,1000000000)); CHECK(vkResetFences(dev,1,&fence));
  for(unsigned j=0;j<2;j++) for(unsigned p=0;p<W*H;p++) for(unsigned c=0;c<4;c++) {unsigned expected=c==3?255:((j==0 && c==0)||(j==1 && c==1))?255:0; if(pixels[j*BYTES+p*4+c]!=expected) {fprintf(stderr,"FAIL mode=%u image=%u pixel=%u channel=%u got=%u expected=%u\n",mode,j,p,c,pixels[j*BYTES+p*4+c],expected);return 1;}}
  fprintf(stderr,"PROBE_PASS mode=%u pixels=%u\n",mode,2*W*H);
 }
 CHECK(vkDeviceWaitIdle(dev));vkDestroyQueryPool(dev,queries,NULL);vkDestroyFence(dev,fence,NULL);vkDestroyCommandPool(dev,pool,NULL);vkUnmapMemory(dev,mem);vkDestroyBuffer(dev,buffer,NULL);vkFreeMemory(dev,mem,NULL);
 for(unsigned j=0;j<2;j++){vkDestroyFramebuffer(dev,fbs[j],NULL);vkDestroyImageView(dev,views[j],NULL);vkDestroyImage(dev,images[j],NULL);vkFreeMemory(dev,imem[j],NULL);}vkDestroyRenderPass(dev,rp,NULL);vkDestroyDevice(dev,NULL);vkDestroyInstance(instance,NULL);
 puts("PASS: 7 cross-CB pixel/barrier/timestamp/fence cases");return 0;
}
