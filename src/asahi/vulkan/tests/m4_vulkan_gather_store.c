/* SPDX-License-Identifier: MIT
 * Native gather + two formatted image writes. Compare raw output against
 * lavapipe. Run M4 only through the watchdog/thermal experiment wrapper.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define CK(x)                                                                  \
   do {                                                                        \
      VkResult r = (x);                                                        \
      if (r) {                                                                 \
         fprintf(stderr, "%s: %d\n", #x, r);                                   \
         exit(1);                                                              \
      }                                                                        \
   } while (0)
static VkDevice dev;
static VkPhysicalDeviceMemoryProperties props;
static VkDeviceMemory
alloc(VkMemoryRequirements req, int host)
{
   unsigned i;
   for (i = 0; i < props.memoryTypeCount; i++)
      if ((req.memoryTypeBits & (1u << i)) &&
          (!host || ((props.memoryTypes[i].propertyFlags & 6) == 6)))
         break;
   if (i == props.memoryTypeCount)
      exit(1);
   VkMemoryAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                             .allocationSize = req.size,
                             .memoryTypeIndex = i};
   VkDeviceMemory m;
   CK(vkAllocateMemory(dev, &a, 0, &m));
   return m;
}
static void
barrier(VkCommandBuffer cb, VkImage im, VkImageLayout old, VkImageLayout next,
        VkAccessFlags src, VkAccessFlags dst)
{
   VkImageMemoryBarrier b = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = old,
      .newLayout = next,
      .srcAccessMask = src,
      .dstAccessMask = dst,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = im,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, 0, 0, 0, 1,
                        &b);
}
int
main(int argc, char **argv)
{
   if (argc != 3 && argc != 4)
      return 2;
   FILE *f = fopen(argv[1], "rb");
   if (!f)
      return 2;
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   rewind(f);
   if (n <= 0 || n > 1024 * 1024 || n % 4)
      return 2;
   uint32_t *spv = malloc(n);
   if (fread(spv, 1, n, f) != (size_t)n)
      return 2;
   fclose(f);
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .pApplicationName = "m4-gather-store",
                            .apiVersion = VK_API_VERSION_1_2};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
   VkInstance instance;
   CK(vkCreateInstance(&ici, 0, &instance));
   uint32_t count = 1;
   VkPhysicalDevice pd;
   CK(vkEnumeratePhysicalDevices(instance, &count, &pd));
   if (!count)
      return 2;
   vkGetPhysicalDeviceMemoryProperties(pd, &props);
   VkPhysicalDeviceProperties pp;
   vkGetPhysicalDeviceProperties(pd, &pp);
   printf("Device: %s\n", pp.deviceName);
   count = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, 0);
   VkQueueFamilyProperties *q = calloc(count, sizeof(*q));
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, q);
   unsigned family;
   for (family = 0; family < count; family++)
      if (q[family].queueFlags & VK_QUEUE_COMPUTE_BIT)
         break;
   if (family == count)
      return 2;
   free(q);
   float priority = 1;
   VkDeviceQueueCreateInfo qi = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = family,
      .queueCount = 1,
      .pQueuePriorities = &priority};
   VkPhysicalDeviceFeatures features = {.shaderStorageImageExtendedFormats =
                                           VK_TRUE};
   VkPhysicalDeviceShaderFloat16Int8Features half = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
      .shaderFloat16 = VK_TRUE};
   VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                            .pNext = getenv("M4_TEST_FLOAT16") ? &half : NULL,
                            .queueCreateInfoCount = 1,
                            .pQueueCreateInfos = &qi,
                            .pEnabledFeatures = &features};
   CK(vkCreateDevice(pd, &di, 0, &dev));
   VkQueue queue;
   vkGetDeviceQueue(dev, family, 0, &queue);
   VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = 65536,
                            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT};
   VkBuffer buf;
   CK(vkCreateBuffer(dev, &bi, 0, &buf));
   VkMemoryRequirements req;
   vkGetBufferMemoryRequirements(dev, buf, &req);
   VkDeviceMemory bm = alloc(req, 1);
   CK(vkBindBufferMemory(dev, buf, bm, 0));
   void *map;
   CK(vkMapMemory(dev, bm, 0, VK_WHOLE_SIZE, 0, &map));
   memset(map, 0xcd, 65536);
   for (unsigned y = 0; y < 32; y++)
      for (unsigned x = 0; x < 32; x++)
         for (unsigned c = 0; c < 4; c++)
            ((float *)map)[(y * 32 + x) * 4 + c] =
               ((x + 3 * y + 7 * c) % 128) / 128.0f;
   VkImage im[3];
   VkImageView view[3];
   VkDeviceMemory mem[3];
   VkFormat formats[] = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16_SFLOAT,
                         VK_FORMAT_R8_UNORM};
   for (unsigned j = 0; j < 3; j++) {
      VkImageCreateInfo ii = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = formats[j],
         .extent = {32, 32, 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage =
            j ? VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT
              : VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
      CK(vkCreateImage(dev, &ii, 0, &im[j]));
      vkGetImageMemoryRequirements(dev, im[j], &req);
      mem[j] = alloc(req, 0);
      CK(vkBindImageMemory(dev, im[j], mem[j], 0));
      VkImageViewCreateInfo vi = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = im[j],
         .viewType = VK_IMAGE_VIEW_TYPE_2D,
         .format = formats[j],
         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
      CK(vkCreateImageView(dev, &vi, 0, &view[j]));
   }
   VkSamplerCreateInfo si = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .maxLod = 0};
   VkSampler sampler;
   CK(vkCreateSampler(dev, &si, 0, &sampler));
   VkDescriptorSetLayoutBinding bind[3] = {0};
   for (unsigned j = 0; j < 3; j++)
      bind[j] = (VkDescriptorSetLayoutBinding){
         .binding = j,
         .descriptorType = j ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                             : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 3,
      .pBindings = bind};
   VkDescriptorSetLayout layout;
   CK(vkCreateDescriptorSetLayout(dev, &li, 0, &layout));
   VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
   VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = sizes};
   VkDescriptorPool dp;
   CK(vkCreateDescriptorPool(dev, &dpi, 0, &dp));
   VkDescriptorSetAllocateInfo dai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dp,
      .descriptorSetCount = 1,
      .pSetLayouts = &layout};
   VkDescriptorSet ds;
   CK(vkAllocateDescriptorSets(dev, &dai, &ds));
   VkDescriptorImageInfo image_info[3];
   VkWriteDescriptorSet writes[3];
   for (unsigned j = 0; j < 3; j++) {
      image_info[j] = (VkDescriptorImageInfo){
         .sampler = j ? VK_NULL_HANDLE : sampler,
         .imageView = view[j],
         .imageLayout = j ? VK_IMAGE_LAYOUT_GENERAL
                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      writes[j] =
         (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = ds,
                                .dstBinding = j,
                                .descriptorCount = 1,
                                .descriptorType = bind[j].descriptorType,
                                .pImageInfo = &image_info[j]};
   }
   vkUpdateDescriptorSets(dev, 3, writes, 0, 0);
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &layout};
   VkPipelineLayout pl;
   CK(vkCreatePipelineLayout(dev, &pli, 0, &pl));
   VkShaderModuleCreateInfo mi = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = n,
      .pCode = spv};
   VkShaderModule module;
   CK(vkCreateShaderModule(dev, &mi, 0, &module));
   free(spv);
   VkComputePipelineCreateInfo pi = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = module,
                .pName = "main"},
      .layout = pl};
   VkPipeline pipeline;
   CK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pi, 0, &pipeline));
   if (getenv("M4_TEST_COMPILE_ONLY")) {
      puts("INFO: pipeline compiled; no GPU commands recorded or submitted");
      return 0;
   }
   VkCommandPoolCreateInfo cpi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = family};
   VkCommandPool pool;
   CK(vkCreateCommandPool(dev, &cpi, 0, &pool));
   VkCommandBufferAllocateInfo cai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
   VkCommandBuffer cb;
   CK(vkAllocateCommandBuffers(dev, &cai, &cb));
   VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   CK(vkBeginCommandBuffer(cb, &begin));
   barrier(cb, im[0], VK_IMAGE_LAYOUT_UNDEFINED,
           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
           VK_ACCESS_TRANSFER_WRITE_BIT);
   VkBufferImageCopy region = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {32, 32, 1}};
   vkCmdCopyBufferToImage(cb, buf, im[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &region);
   barrier(cb, im[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
   for (unsigned j = 1; j < 3; j++)
      barrier(cb, im[j], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
              VK_ACCESS_SHADER_WRITE_BIT);
   if (getenv("M4_TEST_PREFILL")) {
      uint16_t *half_data = (uint16_t *)((char *)map + 32768);
      for (unsigned k = 0; k < 1024; ++k) {
         half_data[2*k] = 0x3800; /* 0.5 */
         half_data[2*k+1] = 0x3400; /* 0.25 */
      }
      barrier(cb, im[1], VK_IMAGE_LAYOUT_GENERAL,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
      VkBufferImageCopy prefill = region;
      prefill.bufferOffset = 32768;
      vkCmdCopyBufferToImage(cb, buf, im[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &prefill);
      barrier(cb, im[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
   }
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0,
                           0);
   vkCmdDispatch(cb, 8, 8, 1);
   for (unsigned j = 1; j < 3; j++) {
      barrier(cb, im[j], VK_IMAGE_LAYOUT_GENERAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
              VK_ACCESS_TRANSFER_READ_BIT);
      region.bufferOffset = 16384 + (j - 1) * 4096;
      vkCmdCopyImageToBuffer(cb, im[j], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             buf, 1, &region);
   }
   VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, 0, 0, 0);
   CK(vkEndCommandBuffer(cb));
   VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CK(vkCreateFence(dev, &fi, 0, &fence));
   VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                          .commandBufferCount = 1,
                          .pCommandBuffers = &cb};
   CK(vkQueueSubmit(queue, 1, &submit, fence));
   CK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull));
   f = fopen(argv[2], "wb");
   if (!f)
      return 1;
   if (fwrite((char *)map + 16384, 1, 5120, f) != 5120)
      return 1;
   fclose(f);
   unsigned different = 0;
   if (argc == 4) {
      unsigned char expected[5120];
      f = fopen(argv[3], "rb");
      if (!f || fread(expected, 1, sizeof(expected), f) != sizeof(expected) ||
          fgetc(f) != EOF)
         return 2;
      fclose(f);
      for (unsigned i = 0; i < sizeof(expected); ++i)
         different += expected[i] != ((unsigned char *)map)[16384 + i];
   }
   vkDestroyFence(dev, fence, 0);
   vkDestroyCommandPool(dev, pool, 0);
   vkDestroyPipeline(dev, pipeline, 0);
   vkDestroyShaderModule(dev, module, 0);
   vkDestroyPipelineLayout(dev, pl, 0);
   vkDestroyDescriptorPool(dev, dp, 0);
   vkDestroyDescriptorSetLayout(dev, layout, 0);
   vkDestroySampler(dev, sampler, 0);
   for (unsigned j = 0; j < 3; j++) {
      vkDestroyImageView(dev, view[j], 0);
      vkDestroyImage(dev, im[j], 0);
      vkFreeMemory(dev, mem[j], 0);
   }
   vkUnmapMemory(dev, bm);
   vkDestroyBuffer(dev, buf, 0);
   vkFreeMemory(dev, bm, 0);
   vkDestroyDevice(dev, 0);
   vkDestroyInstance(instance, 0);
   if (argc == 4) {
      printf("%s: gather/store reference comparison: %u differing bytes of 5120\n",
             different ? "FAIL" : "PASS", different);
   } else {
      puts("INFO: gather/store output saved; requires reference comparison");
   }
   return different ? 1 : 0;
}
