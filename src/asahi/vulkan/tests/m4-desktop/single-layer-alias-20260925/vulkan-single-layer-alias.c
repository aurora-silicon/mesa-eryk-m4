/* SPDX-License-Identifier: MIT
 * Run only under the hardware watchdog/thermal guard.
 * Tests standard 2D/array views, plus the D3D-style non-array shader/one-layer
 * array descriptor compatibility used by VKD3D. The latter intentionally goes
 * beyond Vulkan's requirements without maintenance11; this is not a CTS test.
 * Build: cc -O2 -Wall -Wextra ... -lvulkan
 * Usage: probe plain.spv array.spv [--compat]
 */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK(x) do { VkResult r=(x); if(r) {fprintf(stderr,"%s: %d\n",#x,r);exit(2);} } while(0)
static VkDevice dev;
static VkPhysicalDeviceMemoryProperties props;
static VkDeviceMemory alloc(VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
    unsigned t; for(t=0;t<props.memoryTypeCount;t++)
        if((req.memoryTypeBits&(1u<<t)) && (props.memoryTypes[t].propertyFlags&flags)==flags) break;
    if(t==props.memoryTypeCount) exit(2);
    VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=t};
    VkDeviceMemory m; VK(vkAllocateMemory(dev,&a,0,&m)); return m;
}
static VkShaderModule shader(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)exit(2);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    uint32_t *p=malloc(n);if(n<=0 || n%4 || fread(p,1,n,f)!=(size_t)n)exit(2);fclose(f);
    VkShaderModuleCreateInfo c={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=n,.pCode=p};
    VkShaderModule s;VK(vkCreateShaderModule(dev,&c,0,&s));free(p);return s;
}
int main(int argc,char **argv) {
    if(argc<3 || argc>4 || (argc==4 && strcmp(argv[3],"--compat")))return 2;
    VkInstance instance;VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VK(vkCreateInstance(&ic,0,&instance));
    uint32_t count=1;VkPhysicalDevice gpu;VK(vkEnumeratePhysicalDevices(instance,&count,&gpu));
    VkPhysicalDeviceProperties gp;vkGetPhysicalDeviceProperties(gpu,&gp);printf("Device: %s\n",gp.deviceName);
    vkGetPhysicalDeviceMemoryProperties(gpu,&props);
    uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,0);
    VkQueueFamilyProperties qp[nq];vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,qp);
    unsigned family=0;while(family<nq && !(qp[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;if(family==nq)return 2;
    float priority=1;VkDeviceQueueCreateInfo qc={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc};VK(vkCreateDevice(gpu,&dc,0,&dev));
    VkQueue queue;vkGetDeviceQueue(dev,family,0,&queue);
    VkImageCreateInfo imageci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.extent={8,8,1},.mipLevels=4,.arrayLayers=4,.samples=1,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
    VkImage image;VK(vkCreateImage(dev,&imageci,0,&image));VkMemoryRequirements req;vkGetImageMemoryRequirements(dev,image,&req);VkDeviceMemory im=alloc(req,0);VK(vkBindImageMemory(dev,image,im,0));
    VkBufferCreateInfo bc={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=16384,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    VkBuffer buffer;VK(vkCreateBuffer(dev,&bc,0,&buffer));vkGetBufferMemoryRequirements(dev,buffer,&req);
    VkDeviceMemory bm=alloc(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VK(vkBindBufferMemory(dev,buffer,bm,0));float *data;VK(vkMapMemory(dev,bm,0,VK_WHOLE_SIZE,0,(void**)&data));
    VkBufferImageCopy copies[4];unsigned offset=0;
    for(unsigned m=0;m<4;m++) {
        unsigned size=8>>m;copies[m]=(VkBufferImageCopy){.bufferOffset=offset*4,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,m,0,4},.imageExtent={size,size,1}};
        for(unsigned l=0;l<4;l++)for(unsigned y=0;y<size;y++)for(unsigned x=0;x<size;x++)for(unsigned c=0;c<4;c++)data[offset++]=m*512+l*128+y*8+x+c*.125f;
    }
    VkDescriptorSetLayoutBinding bindings[]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo lc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};VkDescriptorSetLayout layout;VK(vkCreateDescriptorSetLayout(dev,&lc,0,&layout));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};VkDescriptorPool pool;VK(vkCreateDescriptorPool(dev,&pc,0,&pool));
    VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&layout};VkDescriptorSet set;VK(vkAllocateDescriptorSets(dev,&sa,&set));
    VkPipelineLayoutCreateInfo plc={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&layout};VkPipelineLayout pl;VK(vkCreatePipelineLayout(dev,&plc,0,&pl));
    VkShaderModule modules[2]={shader(argv[1]),shader(argv[2])};VkPipeline pipelines[2];
    for(unsigned i=0;i<2;i++){VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=modules[i],.pName="main"},.layout=pl};VK(vkCreateComputePipelines(dev,0,1,&ci,0,&pipelines[i]));}
    VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.maxLod=3};VkSampler sampler;VK(vkCreateSampler(dev,&sci,0,&sampler));
    VkCommandPoolCreateInfo cc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool cp;VK(vkCreateCommandPool(dev,&cc,0,&cp));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.commandBufferCount=1,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY};VkCommandBuffer cb;VK(vkAllocateCommandBuffers(dev,&ca,&cb));
    VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;VK(vkCreateFence(dev,&fc,0,&fence));unsigned bad=0;
    for(unsigned layer=0;layer<4;layer++)for(unsigned mode=0;mode<(argc==4?3u:2u);mode++) {
        /* 0=valid plain, 1=valid array, 2=VKD3D compatibility alias. */
        VkImageViewCreateInfo vc={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=mode?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D,.format=imageci.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,4,layer,1}};VkImageView view;VK(vkCreateImageView(dev,&vc,0,&view));
        VkDescriptorImageInfo di={.sampler=sampler,.imageView=view,.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkDescriptorBufferInfo db={buffer,8192,1024};
        VkWriteDescriptorSet writes[]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=bindings[0].descriptorType,.pImageInfo=&di},{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=bindings[1].descriptorType,.pBufferInfo=&db}};vkUpdateDescriptorSets(dev,2,writes,0,0);
        memset(data+2048,0xa5,1024);VK(vkResetCommandBuffer(cb,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(cb,&begin));
        if(layer==0 && mode==0){
            VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,4,0,4}};
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,0,0,0,1,&ib);
            vkCmdCopyBufferToImage(cb,buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,4,copies);
            ib.oldLayout=ib.newLayout;ib.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,0,0,0,1,&ib);
        }
        vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[mode==1]);vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&set,0,0);vkCmdDispatch(cb,32,1,1);
        VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,0,0,0);VK(vkEndCommandBuffer(cb));
        VK(vkResetFences(dev,1,&fence));VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};VK(vkQueueSubmit(queue,1,&si,fence));VK(vkWaitForFences(dev,1,&fence,VK_TRUE,10000000000ull));
        unsigned mismatches=0;for(unsigned i=0;i<64;i++)for(unsigned c=0;c<4;c++){unsigned m=i%4,u=(i%32)/4,size=8>>m,x=((2*u+1)*size)/16,y=((15-2*u)*size)/16;float expect=m*512+layer*128+y*8+x+c*.125f;float actual=data[2048+4*i+c];if(actual!=expect){if(mismatches<2)fprintf(stderr,"layer%u mode%u i%u c%u: %g expected %g\n",layer,mode,i,c,actual,expect);mismatches++;}}
        printf("%s: base layer %u %s sample+fetch, %u mismatches\n",mismatches?"FAIL":"PASS",layer,mode==0?"plain":mode==1?"array":"D3D alias",mismatches);bad+=mismatches;vkDestroyImageView(dev,view,0);
    }
    vkDestroyFence(dev,fence,0);vkDestroyCommandPool(dev,cp,0);vkDestroySampler(dev,sampler,0);
    for(unsigned i=0;i<2;i++){vkDestroyPipeline(dev,pipelines[i],0);vkDestroyShaderModule(dev,modules[i],0);}
    vkDestroyPipelineLayout(dev,pl,0);vkDestroyDescriptorPool(dev,pool,0);vkDestroyDescriptorSetLayout(dev,layout,0);
    vkUnmapMemory(dev,bm);vkDestroyBuffer(dev,buffer,0);vkFreeMemory(dev,bm,0);vkDestroyImage(dev,image,0);vkFreeMemory(dev,im,0);vkDestroyDevice(dev,0);vkDestroyInstance(instance,0);
    printf("%s: single-layer descriptor matrix (%u mismatches)\n",bad?"FAIL":"PASS",bad);return bad?1:0;
}
