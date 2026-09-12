/* SPDX-License-Identifier: MIT
 * Exact BGRA upload -> RGBA blit -> host readback oracle. Hardware requires
 * guard. cc -O2 -Wall -Wextra vulkan-color-blit.c -o vulkan-color-blit -lvulkan
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
static int clear_source, render_clear;

static VkPhysicalDeviceMemoryProperties memprops;
static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  for (unsigned i = 0; i < memprops.memoryTypeCount; i++)
    if ((bits & (1u << i)) &&
        (memprops.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  fprintf(stderr, "No memory type\n");
  exit(1);
}
static void barrier(VkCommandBuffer cb, VkImage image, VkImageLayout old,
                    VkImageLayout new, VkAccessFlags src, VkAccessFlags dst) {
  VkImageMemoryBarrier b = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = old,
      .newLayout = new,
      .srcAccessMask = src,
      .dstAccessMask = dst,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL,
                       1, &b);
}
static unsigned run(VkDevice dev, VkQueue queue, unsigned family) {
  VkImage images[2];
  VkDeviceMemory im[2];
  VkFormat formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
  VkMemoryRequirements req;
  VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  for (unsigned i = 0; i < 2; i++) {
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = formats[i],
                            .extent = {64, 64, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    CHECK(vkCreateImage(dev, &ci, NULL, &images[i]));
    vkGetImageMemoryRequirements(dev, images[i], &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(req.memoryTypeBits, 0);
    CHECK(vkAllocateMemory(dev, &ai, NULL, &im[i]));
    CHECK(vkBindImageMemory(dev, images[i], im[i], 0));
  }
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = 3 * 64 * 64 * 4,
                           .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VkBuffer buffer;
  VkDeviceMemory bm;
  CHECK(vkCreateBuffer(dev, &bi, NULL, &buffer));
  vkGetBufferMemoryRequirements(dev, buffer, &req);
  ai.allocationSize = req.size;
  ai.memoryTypeIndex =
      memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  CHECK(vkAllocateMemory(dev, &ai, NULL, &bm));
  CHECK(vkBindBufferMemory(dev, buffer, bm, 0));
  uint8_t *data;
  CHECK(vkMapMemory(dev, bm, 0, VK_WHOLE_SIZE, 0, (void **)&data));
  for (unsigned i = 0; i < 64 * 64; i++) {
    data[i * 4] = 17 + i % 29;
    data[i * 4 + 1] = 63 + i % 17;
    data[i * 4 + 2] = 191 + i % 47;
    data[i * 4 + 3] = 255;
  }
  if (clear_source)
    for (unsigned i = 0; i < 64 * 64; i++) {
      data[i * 4] = 16;
      data[i * 4 + 1] = 64;
      data[i * 4 + 2] = 191;
      data[i * 4 + 3] = 255;
    }
  memset(data + 64 * 64 * 4, 0xa5, 2 * 64 * 64 * 4);
  VkCommandPoolCreateInfo pi = {.sType =
                                    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
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
  VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                          .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &host, 0, NULL, 0,
                       NULL);
  VkImageLayout src_layout = render_clear
                                 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                 : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  VkAccessFlags src_access = render_clear ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                          : VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier(cb, images[0], VK_IMAGE_LAYOUT_UNDEFINED, src_layout, 0, src_access);
  VkRenderPass rp = VK_NULL_HANDLE;
  VkFramebuffer fb = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkBufferImageCopy copy = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {64, 64, 1}};
  if (render_clear) {
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = images[0],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = formats[0],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    CHECK(vkCreateImageView(dev, &vi, NULL, &view));
    VkAttachmentDescription ad = {
        .format = formats[0],
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = src_layout,
        .finalLayout = src_layout};
    VkAttachmentReference ar = {0, src_layout};
    VkSubpassDescription sub = {.pipelineBindPoint =
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                .colorAttachmentCount = 1,
                                .pColorAttachments = &ar};
    VkRenderPassCreateInfo ri = {.sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                 .attachmentCount = 1,
                                 .pAttachments = &ad,
                                 .subpassCount = 1,
                                 .pSubpasses = &sub};
    CHECK(vkCreateRenderPass(dev, &ri, NULL, &rp));
    VkFramebufferCreateInfo fbi = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp,
        .attachmentCount = 1,
        .pAttachments = &view,
        .width = 64,
        .height = 64,
        .layers = 1};
    CHECK(vkCreateFramebuffer(dev, &fbi, NULL, &fb));
    VkClearValue cv = {
        .color.float32 = {191.0f / 255, 64.0f / 255, 16.0f / 255, 1}};
    VkRenderPassBeginInfo rbi = {.sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                 .renderPass = rp,
                                 .framebuffer = fb,
                                 .renderArea = {{0, 0}, {64, 64}},
                                 .clearValueCount = 1,
                                 .pClearValues = &cv};
    vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cb);
  } else if (clear_source) {
    VkClearColorValue color = {
        .float32 = {191.0f / 255, 64.0f / 255, 16.0f / 255, 1}};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, images[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &range);
  } else
    vkCmdCopyBufferToImage(cb, buffer, images[0],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  barrier(cb, images[0], src_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          src_access, VK_ACCESS_TRANSFER_READ_BIT);
  barrier(cb, images[1], VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
          VK_ACCESS_TRANSFER_WRITE_BIT);
  VkImageBlit blit = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .srcOffsets = {{0, 0, 0}, {64, 64, 1}},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .dstOffsets = {{0, 0, 0}, {64, 64, 1}}};
  vkCmdBlitImage(cb, images[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, images[1],
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                 VK_FILTER_NEAREST);
  barrier(cb, images[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_TRANSFER_READ_BIT);
  for (unsigned i = 0; i < 2; i++) {
    copy.bufferOffset = (i + 1) * 64 * 64 * 4;
    vkCmdCopyImageToBuffer(cb, images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer, 1, &copy);
  }
  host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0,
                       NULL);
  CHECK(vkEndCommandBuffer(cb));
  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .commandBufferCount = 1,
                     .pCommandBuffers = &cb};
  VkFence fence;
  VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  CHECK(vkCreateFence(dev, &fi, NULL, &fence));
  CHECK(vkQueueSubmit(queue, 1, &si, fence));
  CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull));
  unsigned bad[2] = {0};
  for (unsigned i = 0; i < 64 * 64; i++)
    for (unsigned c = 0; c < 4; c++) {
      bad[0] += data[64 * 64 * 4 + i * 4 + c] != data[i * 4 + c];
      unsigned sc = c == 0 ? 2 : c == 2 ? 0 : c;
      bad[1] += data[2 * 64 * 64 * 4 + i * 4 + c] != data[i * 4 + sc];
    }
  printf("BGRA upload bad_bytes=%u; BGRA-to-RGBA blit bad_bytes=%u "
         "first=%u,%u,%u,%u\n",
         bad[0], bad[1], data[32768], data[32769], data[32770], data[32771]);
  if (fb)
    vkDestroyFramebuffer(dev, fb, NULL);
  if (rp)
    vkDestroyRenderPass(dev, rp, NULL);
  if (view)
    vkDestroyImageView(dev, view, NULL);
  vkDestroyFence(dev, fence, NULL);
  vkDestroyCommandPool(dev, pool, NULL);
  vkUnmapMemory(dev, bm);
  vkDestroyBuffer(dev, buffer, NULL);
  vkFreeMemory(dev, bm, NULL);
  for (unsigned i = 0; i < 2; i++) {
    vkDestroyImage(dev, images[i], NULL);
    vkFreeMemory(dev, im[i], NULL);
  }
  if (!bad[0] && !bad[1])
    puts("PASS: Vulkan BGRA upload and RGBA blit readback");
  return bad[0] + bad[1];
}
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  int reference = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--reference"))
      reference = 1;
    else if (!strcmp(argv[i], "--clear"))
      clear_source = 1;
    else if (!strcmp(argv[i], "--render-clear"))
      render_clear = clear_source = 1;
    else
      return 2;
  }
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .pApplicationName = "M4 color blit oracle",
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
  unsigned bad = run(dev, queue, family);
  vkDestroyDevice(dev, NULL);
  vkDestroyInstance(instance, NULL);
  return bad ? 1 : 0;
}
