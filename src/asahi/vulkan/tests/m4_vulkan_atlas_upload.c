/* SPDX-License-Identifier: MIT
 * Verify batched atlas uploads, untouched gaps, strides, layers and mip
 * fallback. Run hardware only inside the watchdog/thermal guard. No shaders
 * required.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define CHECK(x)                                                               \
   do {                                                                        \
      VkResult r_ = (x);                                                       \
      if (r_ != VK_SUCCESS) {                                                  \
         fprintf(stderr, "%s: %d\n", #x, r_);                                  \
         exit(1);                                                              \
      }                                                                        \
   } while (0)
#define W   193
#define H   129
#define CAP (1024 * 1024)
static VkPhysicalDeviceMemoryProperties memprops;
static uint32_t
memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
{
   for (unsigned i = 0; i < memprops.memoryTypeCount; i++)
      if ((bits & (1u << i)) &&
          (memprops.memoryTypes[i].propertyFlags & flags) == flags)
         return i;
   fprintf(stderr, "No memory type\n");
   exit(1);
}
static void
barrier(VkCommandBuffer cb, VkImage image, VkImageLayout old, VkImageLayout new,
        VkAccessFlags src, VkAccessFlags dst)
{
   VkImageMemoryBarrier b = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = old,
      .newLayout = new,
      .srcAccessMask = src,
      .dstAccessMask = dst,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 2, 0, 2}};
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL,
                        1, &b);
}
static size_t offsets[2][2], used;
static uint8_t expected[CAP];
static VkBufferImageCopy
patch(uint8_t *src, unsigned bpp, unsigned mip, unsigned layer, unsigned x,
      unsigned y, unsigned w, unsigned h, unsigned salt, unsigned layers)
{
   unsigned row = w + 6, rows = h + 2;
   used = (used + 3) & ~3u;
   VkBufferImageCopy r = {
      .bufferOffset = used,
      .bufferRowLength = row,
      .bufferImageHeight = rows,
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, layers},
      .imageOffset = {(int)x, (int)y, 0},
      .imageExtent = {w, h, 1}};
   if (used + (size_t)row * rows * bpp * layers > CAP)
      abort();
   memset(src + used, 0xda, (size_t)row * rows * bpp * layers);
   for (unsigned l = 0; l < layers; l++)
      for (unsigned yy = 0; yy < h; yy++)
         for (unsigned xx = 0; xx < w; xx++)
            for (unsigned c = 0; c < bpp; c++) {
               uint8_t value =
                  17 + ((salt * 19 + xx * 7 + yy * 13 + c * 37 + l * 53) % 217);
               src[used + ((size_t)l * rows * row + yy * row + xx) * bpp + c] =
                  value;
               expected[offsets[mip][layer + l] +
                        ((size_t)(y + yy) * (W >> mip) + x + xx) * bpp + c] =
                  value;
            }
   used += (size_t)row * rows * bpp * layers;
   return r;
}
static unsigned
run(VkDevice dev, VkQueue queue, unsigned family, unsigned bpp)
{
   VkFormat format = bpp == 1 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
   VkImageCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = format,
                            .extent = {W, H, 1},
                            .mipLevels = 2,
                            .arrayLayers = 2,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
   VkImage image;
   CHECK(vkCreateImage(dev, &ici, NULL, &image));
   VkMemoryRequirements req;
   vkGetImageMemoryRequirements(dev, image, &req);
   VkMemoryAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = memory_type(req.memoryTypeBits, 0)};
   VkDeviceMemory im;
   CHECK(vkAllocateMemory(dev, &ai, NULL, &im));
   CHECK(vkBindImageMemory(dev, image, im, 0));
   VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = CAP * 2,
                            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
   VkBuffer buffer;
   CHECK(vkCreateBuffer(dev, &bi, NULL, &buffer));
   vkGetBufferMemoryRequirements(dev, buffer, &req);
   ai.allocationSize = req.size;
   ai.memoryTypeIndex =
      memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   VkDeviceMemory bm;
   CHECK(vkAllocateMemory(dev, &ai, NULL, &bm));
   CHECK(vkBindBufferMemory(dev, buffer, bm, 0));
   uint8_t *data;
   CHECK(vkMapMemory(dev, bm, 0, CAP * 2, 0, (void **)&data));
   memset(data, 0xee, CAP * 2);
   VkCommandPoolCreateInfo pi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = family};
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(dev, &pi, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
   VkCommandBuffer cb;
   CHECK(vkAllocateCommandBuffers(dev, &cai, &cb));
   VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   CHECK(vkBeginCommandBuffer(cb, &begin));
   barrier(cb, image, VK_IMAGE_LAYOUT_UNDEFINED,
           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
           VK_ACCESS_TRANSFER_WRITE_BIT);
   used = 0;
   VkBufferImageCopy full[4];
   unsigned n = 0;
   for (unsigned mip = 0; mip < 2; mip++)
      for (unsigned layer = 0; layer < 2; layer++) {
         used = (used + 3) & ~3u;
         offsets[mip][layer] = used;
         size_t bytes = (size_t)(W >> mip) * (H >> mip) * bpp;
         for (size_t i = 0; i < bytes; i++)
            data[used + i] = expected[used + i] =
               (uint8_t)(3 + (i * 11 + layer * 17 + mip * 31) % 251);
         full[n++] = (VkBufferImageCopy){
            .bufferOffset = used,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1},
            .imageExtent = {W >> mip, H >> mip, 1}};
         used += bytes;
      }
   vkCmdCopyBufferToImage(cb, buffer, image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, n, full);
   barrier(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
           VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
           VK_ACCESS_TRANSFER_WRITE_BIT);
   for (unsigned layer = 0; layer < 2; layer++) {
      VkBufferImageCopy regions[64];
      for (unsigned i = 0; i < 64; i++)
         regions[i] =
            patch(data, bpp, 0, layer, 3 + (i % 8) * 23, 2 + (i / 8) * 15,
                  7 + i % 5, 5 + i % 3, i + layer, 1);
      vkCmdCopyBufferToImage(cb, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 64,
                             regions);
   }
   VkBufferImageCopy mixed[2] = {patch(data, bpp, 1, 0, 3, 2, 13, 11, 33, 1),
                                 patch(data, bpp, 1, 1, 21, 5, 15, 9, 71, 1)};
   vkCmdCopyBufferToImage(cb, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 2, mixed);
   VkBufferImageCopy layered = patch(data, bpp, 1, 0, 55, 40, 11, 7, 91, 2);
   vkCmdCopyBufferToImage(cb, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1,
                          &layered);
   barrier(cb, image, VK_IMAGE_LAYOUT_GENERAL,
           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
           VK_ACCESS_TRANSFER_READ_BIT);
   for (unsigned i = 0; i < n; i++)
      full[i].bufferOffset += CAP;
   vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          buffer, n, full);
   VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                           .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                           .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0,
                        NULL);
   CHECK(vkEndCommandBuffer(cb));
   VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CHECK(vkCreateFence(dev, &fi, NULL, &fence));
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1,
                      .pCommandBuffers = &cb};
   CHECK(vkQueueSubmit(queue, 1, &si, fence));
   CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 20000000000ull));
   unsigned bad = 0, checked = 0;
   for (unsigned mip = 0; mip < 2; mip++)
      for (unsigned layer = 0; layer < 2; layer++) {
         size_t bytes = (size_t)(W >> mip) * (H >> mip) * bpp,
                start = offsets[mip][layer];
         for (size_t i = 0; i < bytes; i++) {
            checked++;
            if (data[CAP + start + i] != expected[start + i]) {
               if (bad < 8)
                  fprintf(
                     stderr,
                     "bpp=%u mip=%u layer=%u byte=%zu got=%u expected=%u\n",
                     bpp, mip, layer, i, data[CAP + start + i],
                     expected[start + i]);
               bad++;
            }
         }
      }
   printf(
      "atlas format=%u bytes=%u bad=%u extent=%ux%u layers=2 mips=2 regions=128+2+2\n",
      format, checked, bad, W, H);
   vkDestroyFence(dev, fence, NULL);
   vkDestroyCommandPool(dev, pool, NULL);
   vkUnmapMemory(dev, bm);
   vkDestroyBuffer(dev, buffer, NULL);
   vkFreeMemory(dev, bm, NULL);
   vkDestroyImage(dev, image, NULL);
   vkFreeMemory(dev, im, NULL);
   return bad;
}
int
main(int argc, char **argv)
{
   setvbuf(stdout, NULL, _IOLBF, 0);
   if (argc > 2 || (argc == 2 && strcmp(argv[1], "--reference")))
      return 2;
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .pApplicationName = "M4 atlas upload oracle",
                            .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                              .pApplicationInfo = &app};
   VkInstance instance;
   CHECK(vkCreateInstance(&ii, NULL, &instance));
   uint32_t n = 0;
   CHECK(vkEnumeratePhysicalDevices(instance, &n, NULL));
   if (!n)
      return 1;
   VkPhysicalDevice *ps = calloc(n, sizeof(*ps));
   CHECK(vkEnumeratePhysicalDevices(instance, &n, ps));
   VkPhysicalDevice physical = ps[0];
   VkPhysicalDeviceProperties props;
   vkGetPhysicalDeviceProperties(physical, &props);
   free(ps);
   printf("device=%s device_id=%#x\n", props.deviceName, props.deviceID);
   if (argc == 1 && props.deviceID != 0x8132) {
      fprintf(stderr, "M4 required; use --reference for another renderer\n");
      return 1;
   }
   vkGetPhysicalDeviceMemoryProperties(physical, &memprops);
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, NULL);
   VkQueueFamilyProperties *qs = calloc(n, sizeof(*qs));
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, qs);
   unsigned family = 0;
   while (family < n && !(qs[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
      family++;
   free(qs);
   if (family == n)
      return 1;
   float priority = 1;
   VkDeviceQueueCreateInfo qi = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = family,
      .queueCount = 1,
      .pQueuePriorities = &priority};
   VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                            .queueCreateInfoCount = 1,
                            .pQueueCreateInfos = &qi};
   VkDevice dev;
   CHECK(vkCreateDevice(physical, &di, NULL, &dev));
   VkQueue queue;
   vkGetDeviceQueue(dev, family, 0, &queue);
   unsigned bad = run(dev, queue, family, 1) + run(dev, queue, family, 4);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(instance, NULL);
   return bad ? 1 : 0;
}
