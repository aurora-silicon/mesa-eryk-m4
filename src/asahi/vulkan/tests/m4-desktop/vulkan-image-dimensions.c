/* SPDX-License-Identifier: MIT
 * Exact 1D/1D-array/3D partial upload, mip and padded readback oracle. Hardware
 * requires guard. cc -O2 -Wall -Wextra vulkan-image-dimensions.c -o
 * vulkan-image-dimensions -lvulkan
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    VkResult r_ = (x);                                                         \
    if (r_ != VK_SUCCESS) {                                                    \
      fprintf(stderr, "%s: %d\n", #x, r_);                                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static VkPhysicalDeviceMemoryProperties memprops;
static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  for (unsigned i = 0; i < memprops.memoryTypeCount; i++)
    if ((bits & (1u << i)) &&
        (memprops.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  fprintf(stderr, "No memory type\n");
  exit(1);
}

static unsigned run(VkDevice dev, VkQueue queue, unsigned family, unsigned kind,
                    unsigned bytes, int depth24) {
  const unsigned w = 16, h = kind == 2 || depth24 ? 8 : 1, slices = kind == 0 ? 1 : 4;
  const unsigned x = 3, y = kind == 2 || depth24 ? 2 : 0, z = kind == 0 ? 0 : 1;
  const unsigned pw = 9, ph = kind == 2 || depth24 ? 5 : 1, ps = kind == 0 ? 1 : 2;
  const unsigned row = pw + 3, rows = ph + 2;
  const unsigned full = w * h * slices * bytes, patch = row * rows * ps * bytes;
  const unsigned patch_off = full, out_off = full + patch,
                 padded_off = 2 * full + patch;
  const unsigned total = 2 * (full + patch);
  VkFormat fmt =
      bytes == 4 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R32G32B32A32_SFLOAT;
  if (depth24) fmt = VK_FORMAT_D24_UNORM_S8_UINT;
  const VkImageAspectFlags aspect = depth24 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = depth24 ? VK_IMAGE_TYPE_2D : kind == 2 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_1D,
      .format = fmt,
      .extent = {2 * w, kind == 2 || depth24 ? 2 * h : 1, kind == 2 ? 2 * slices : 1},
      .mipLevels = 2,
      .arrayLayers = kind == 1 ? slices : 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage =
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
  VkImage image;
  VkDeviceMemory im, bm;
  VkBuffer buffer;
  VkMemoryRequirements req;
  VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  CHECK(vkCreateImage(dev, &ci, NULL, &image));
  vkGetImageMemoryRequirements(dev, image, &req);
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, 0);
  CHECK(vkAllocateMemory(dev, &ai, NULL, &im));
  CHECK(vkBindImageMemory(dev, image, im, 0));
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = total,
                           .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  CHECK(vkCreateBuffer(dev, &bi, NULL, &buffer));
  vkGetBufferMemoryRequirements(dev, buffer, &req);
  ai.allocationSize = req.size;
  ai.memoryTypeIndex =
      memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  CHECK(vkAllocateMemory(dev, &ai, NULL, &bm));
  CHECK(vkBindBufferMemory(dev, buffer, bm, 0));
  uint8_t *data, *expected = malloc(total);
  CHECK(vkMapMemory(dev, bm, 0, VK_WHOLE_SIZE, 0, (void **)&data));
  memset(data, 0xa5, total);
  for (unsigned i = 0; i < full; i++)
    data[i] = (i * 113 + i / 251 + 17) & 255;
  for (unsigned i = 0; i < patch; i++)
    data[patch_off + i] = (i * 79 + i / 127 + 91) & 255;
  if (depth24)
    for (unsigned i = 3; i < full + patch; i += 4) data[i] = 0;
  memcpy(expected, data, total);
  memcpy(expected + out_off, data, full);
  for (unsigned s = 0; s < ps; s++)
    for (unsigned j = 0; j < ph; j++)
      for (unsigned i = 0; i < pw * bytes; i++) {
        unsigned src = patch_off + (s * rows + j) * row * bytes + i;
        expected[out_off + ((s + z) * h + j + y) * w * bytes + x * bytes + i] =
            data[src];
        expected[padded_off + (s * rows + j) * row * bytes + i] = data[src];
      }
  VkCommandPoolCreateInfo pi = {.sType =
                                    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                .queueFamilyIndex = family};
  VkCommandPool pool;
  CHECK(vkCreateCommandPool(dev, &pi, NULL, &pool));
  VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  VkCommandBuffer cb;
  CHECK(vkAllocateCommandBuffers(dev, &ca, &cb));
  VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  CHECK(vkBeginCommandBuffer(cb, &begin));
  VkImageMemoryBarrier ib = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {depth24 ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : aspect, 1, 1, 0, ci.arrayLayers}};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                       &ib);
  VkBufferImageCopy copy = {
      .imageSubresource = {aspect, 1, 0, ci.arrayLayers},
      .imageExtent = {w, h, kind == 2 ? slices : 1}};
  vkCmdCopyBufferToImage(cb, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
  VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                             .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                             .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT |
                                              VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL,
                       0, NULL);
  VkBufferImageCopy part = {
      .bufferOffset = patch_off,
      .bufferRowLength = row,
      .bufferImageHeight = rows,
      .imageSubresource = {aspect, 1, kind == 1 ? z : 0,
                           kind == 1 ? ps : 1},
      .imageOffset = {(int)x, (int)y, kind == 2 ? (int)z : 0},
      .imageExtent = {pw, ph, kind == 2 ? ps : 1}};
  vkCmdCopyBufferToImage(cb, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &part);
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL,
                       0, NULL);
  copy.bufferOffset = out_off;
  part.bufferOffset = padded_off;
  vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &copy);
  vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &part);
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0,
                       NULL);
  CHECK(vkEndCommandBuffer(cb));
  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .commandBufferCount = 1,
                     .pCommandBuffers = &cb};
  VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence;
  CHECK(vkCreateFence(dev, &fi, NULL, &fence));
  CHECK(vkQueueSubmit(queue, 1, &si, fence));
  CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
  unsigned bad = 0;
  for (unsigned i = 0; i < total; i++)
    if (!(depth24 && i % 4 == 3) && data[i] != expected[i]) {
      if (bad++ < 4)
        fprintf(stderr, "byte %u got %02x expected %02x\n", i, data[i],
                expected[i]);
    }
  printf("%s: image-dimensions kind=%u bytes=%u mip=1 bad=%u/%u\n",
         bad ? "FAIL" : "PASS", kind, bytes, bad, total);
  free(expected);
  vkDestroyFence(dev, fence, NULL);
  vkDestroyCommandPool(dev, pool, NULL);
  vkUnmapMemory(dev, bm);
  vkDestroyBuffer(dev, buffer, NULL);
  vkFreeMemory(dev, bm, NULL);
  vkDestroyImage(dev, image, NULL);
  vkFreeMemory(dev, im, NULL);
  return bad;
}
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  int reference = 0, depth24 = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--reference"))
      reference = 1;
    else if (!strcmp(argv[i], "--depth24"))
      depth24 = 1;
    else
      return 2;
  }
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .pApplicationName = "M4 image dimensionality oracle",
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
  if (!reference && props.deviceID != 0x8132) {
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
  VkDeviceQueueCreateInfo qi = {.sType =
                                    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
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
  unsigned bad = 0;
  for (unsigned type = 0; type < (depth24 ? 2u : 3u); type++)
    for (unsigned bytes = 4; bytes <= (depth24 ? 4u : 16u); bytes *= 4)
      bad += run(dev, queue, family, type, bytes, depth24);
  vkDestroyDevice(dev, NULL);
  vkDestroyInstance(instance, NULL);
  return bad ? 1 : 0;
}
