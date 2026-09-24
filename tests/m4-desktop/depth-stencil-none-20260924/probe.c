/* SPDX-License-Identifier: MIT
 * Preserve unused NONE/NONE depth/stencil attachments across a color draw.
 * Run hardware only through gpu-experiment.py. Shader header generated below.
 */
#include "none-shaders.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define W 32
#define H 32
#define CHECK(x)                                                               \
  do {                                                                         \
    VkResult r_ = (x);                                                         \
    if (r_ != VK_SUCCESS) {                                                    \
      fprintf(stderr, "%s: %d\n", #x, r_);                                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static VkPhysicalDeviceMemoryProperties memory;
static VkDevice dev;
static uint32_t mt(uint32_t mask, VkMemoryPropertyFlags flags) {
  for (unsigned i = 0; i < memory.memoryTypeCount; i++)
    if ((mask & (1u << i)) &&
        (memory.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  abort();
}
struct img {
  VkImage image;
  VkDeviceMemory mem;
  VkImageView view;
  VkImageAspectFlags aspects;
};
static struct img image(VkFormat format, VkImageAspectFlags aspects) {
  struct img x = {.aspects = aspects};
  VkImageCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {W, H, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               (aspects == VK_IMAGE_ASPECT_COLOR_BIT
                    ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                    : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)};
  CHECK(vkCreateImage(dev, &ci, 0, &x.image));
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(dev, x.image, &mr);
  VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                             .allocationSize = mr.size,
                             .memoryTypeIndex = mt(mr.memoryTypeBits, 0)};
  CHECK(vkAllocateMemory(dev, &ai, 0, &x.mem));
  CHECK(vkBindImageMemory(dev, x.image, x.mem, 0));
  VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                              .image = x.image,
                              .viewType = VK_IMAGE_VIEW_TYPE_2D,
                              .format = format,
                              .subresourceRange = {aspects, 0, 1, 0, 1}};
  CHECK(vkCreateImageView(dev, &vi, 0, &x.view));
  return x;
}
static void barrier(VkCommandBuffer cmd, struct img x, VkImageLayout old) {
  VkImageMemoryBarrier b = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask =
          old == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      .oldLayout = old,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = x.image,
      .subresourceRange = {x.aspects, 0, 1, 0, 1}};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, 0, 0, 0, 1,
                       &b);
}
int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "d24";
  VkFormat format;
  unsigned db;
  if (!strcmp(mode, "d24")) {
    format = VK_FORMAT_D24_UNORM_S8_UINT;
    db = 4;
  } else if (!strcmp(mode, "d32")) {
    format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    db = 4;
  } else if (!strcmp(mode, "d16")) {
    format = VK_FORMAT_D16_UNORM;
    db = 2;
  } else if (!strcmp(mode, "s8")) {
    format = VK_FORMAT_S8_UINT;
    db = 0;
  } else
    return 2;
  const int stencil = strcmp(mode, "d16") != 0,
            readonly = argc > 2 && !strcmp(argv[2], "readonly");
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_4};
  VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                              .pApplicationInfo = &app};
  VkInstance inst;
  CHECK(vkCreateInstance(&ici, 0, &inst));
  uint32_t n = 1;
  VkPhysicalDevice phy;
  CHECK(vkEnumeratePhysicalDevices(inst, &n, &phy));
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(phy, &props);
  printf("Device: %s; format=%s initial-pass=%s\n", props.deviceName, mode,
         readonly ? "readonly" : "unused-none");
  fflush(stdout);
  if (!strstr(props.deviceName, "M4") && !strstr(props.deviceName, "llvmpipe"))
    return 2;
  vkGetPhysicalDeviceMemoryProperties(phy, &memory);
  vkGetPhysicalDeviceQueueFamilyProperties(phy, &n, 0);
  VkQueueFamilyProperties *q = calloc(n, sizeof(*q));
  vkGetPhysicalDeviceQueueFamilyProperties(phy, &n, q);
  uint32_t family = 0;
  while (family < n && !(q[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
    family++;
  if (family == n)
    return 2;
  free(q);
  float priority = 1;
  VkDeviceQueueCreateInfo qi = {.sType =
                                    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .queueFamilyIndex = family,
                                .queueCount = 1,
                                .pQueuePriorities = &priority};
  VkPhysicalDeviceVulkan13Features f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .dynamicRendering = VK_TRUE};
  VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                           .pNext = &f13,
                           .queueCreateInfoCount = 1,
                           .pQueueCreateInfos = &qi};
  CHECK(vkCreateDevice(phy, &di, 0, &dev));
  VkQueue queue;
  vkGetDeviceQueue(dev, family, 0, &queue);
  struct img color = image(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT),
             ds =
                 image(format, (db ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                                   (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0));
  const unsigned cb = W * H * 4, depthsize = W * H * db,
                 total = cb + depthsize + (stencil ? W * H : 0);
  VkBuffer buffer;
  VkDeviceMemory bm;
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = total,
                           .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  CHECK(vkCreateBuffer(dev, &bi, 0, &buffer));
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(dev, buffer, &mr);
  VkMemoryAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex =
          mt(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
  CHECK(vkAllocateMemory(dev, &ai, 0, &bm));
  CHECK(vkBindBufferMemory(dev, buffer, bm, 0));
  VkShaderModule vs, fs;
  VkShaderModuleCreateInfo sm = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(vert),
      .pCode = vert};
  CHECK(vkCreateShaderModule(dev, &sm, 0, &vs));
  sm.codeSize = sizeof(frag);
  sm.pCode = frag;
  CHECK(vkCreateShaderModule(dev, &sm, 0, &fs));
  VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vs,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fs,
       .pName = "main"}};
  VkPipelineLayout layout;
  VkPipelineLayoutCreateInfo li = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  CHECK(vkCreatePipelineLayout(dev, &li, 0, &layout));
  VkPipelineVertexInputStateCreateInfo vertex = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  VkViewport vp = {0, 0, W, H, 0, 1};
  VkRect2D sc = {{0, 0}, {W, H}};
  VkPipelineViewportStateCreateInfo viewport = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &vp,
      .scissorCount = 1,
      .pScissors = &sc};
  VkPipelineRasterizationStateCreateInfo rs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .lineWidth = 1};
  VkPipelineMultisampleStateCreateInfo ms = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
  VkPipelineColorBlendAttachmentState ba = {.colorWriteMask = 15};
  VkPipelineColorBlendStateCreateInfo blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &ba};
  VkStencilOpState sop = {.failOp = VK_STENCIL_OP_KEEP,
                          .passOp = VK_STENCIL_OP_KEEP,
                          .depthFailOp = VK_STENCIL_OP_KEEP,
                          .compareOp = VK_COMPARE_OP_EQUAL,
                          .compareMask = 255,
                          .writeMask = 0,
                          .reference = 0x5a};
  VkPipelineDepthStencilStateCreateInfo depth = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .front = sop,
      .back = sop};
  VkFormat cf = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo pri = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &cf,
      .depthAttachmentFormat = db ? format : VK_FORMAT_UNDEFINED,
      .stencilAttachmentFormat = stencil ? format : VK_FORMAT_UNDEFINED};
  VkGraphicsPipelineCreateInfo pi = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &pri,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &vertex,
      .pInputAssemblyState = &ia,
      .pViewportState = &viewport,
      .pRasterizationState = &rs,
      .pMultisampleState = &ms,
      .pDepthStencilState = &depth,
      .pColorBlendState = &blend,
      .layout = layout};
  VkPipeline pipelines[2];
  for (unsigned i = 0; i < 2; i++) {
    depth.depthTestEnable = (i || readonly) && db;
    depth.stencilTestEnable = (i || readonly) && stencil;
    CHECK(vkCreateGraphicsPipelines(dev, 0, 1, &pi, 0, &pipelines[i]));
  }
  VkCommandPool pool;
  VkCommandPoolCreateInfo pci = {.sType =
                                     VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                 .queueFamilyIndex = family};
  CHECK(vkCreateCommandPool(dev, &pci, 0, &pool));
  VkCommandBuffer cmd;
  VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  CHECK(vkAllocateCommandBuffers(dev, &ca, &cmd));
  VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  CHECK(vkBeginCommandBuffer(cmd, &begin));
  barrier(cmd, color, VK_IMAGE_LAYOUT_UNDEFINED);
  barrier(cmd, ds, VK_IMAGE_LAYOUT_UNDEFINED);
  VkClearDepthStencilValue clear = {1.0, 0x5a};
  VkImageSubresourceRange range = {ds.aspects, 0, 1, 0, 1};
  vkCmdClearDepthStencilImage(cmd, ds.image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1,
                              &range);
  barrier(cmd, ds, VK_IMAGE_LAYOUT_GENERAL);
  for (unsigned i = 0; i < 2; i++) {
    VkRenderingAttachmentInfo c = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = color.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.color.float32 = {0, 0, 1, 1}};
    VkRenderingAttachmentInfo z = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = ds.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = i || readonly ? VK_ATTACHMENT_LOAD_OP_LOAD
                                : VK_ATTACHMENT_LOAD_OP_NONE,
        .storeOp =
            i ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_NONE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, {W, H}},
                          .layerCount = 1,
                          .colorAttachmentCount = 1,
                          .pColorAttachments = &c,
                          .pDepthAttachment = db ? &z : 0,
                          .pStencilAttachment = stencil ? &z : 0};
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[i]);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    barrier(cmd, color, VK_IMAGE_LAYOUT_GENERAL);
    barrier(cmd, ds, VK_IMAGE_LAYOUT_GENERAL);
  }
  VkBufferImageCopy copy = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {W, H, 1}};
  vkCmdCopyImageToBuffer(cmd, color.image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1,
                         &copy);
  if (db) {
    copy.bufferOffset = cb;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyImageToBuffer(cmd, ds.image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1,
                           &copy);
  }
  if (stencil) {
    copy.bufferOffset = cb + depthsize;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    vkCmdCopyImageToBuffer(cmd, ds.image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1,
                           &copy);
  }
  VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, 0, 0, 0);
  CHECK(vkEndCommandBuffer(cmd));
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                         .commandBufferCount = 1,
                         .pCommandBuffers = &cmd};
  CHECK(vkQueueSubmit(queue, 1, &submit, 0));
  CHECK(vkQueueWaitIdle(queue));
  uint8_t *map;
  CHECK(vkMapMemory(dev, bm, 0, total, 0, (void **)&map));
  unsigned bad = 0;
  for (unsigned i = 0; i < W * H; i++) {
    uint32_t z = 0;
    uint32_t expected = !strcmp(mode, "d16")   ? 65535
                        : !strcmp(mode, "d24") ? 16777215
                                               : 0x3f800000;
    if (db)
      memcpy(&z, map + cb + i * db, db);
    if (map[4 * i] != 255 || map[4 * i + 1] || map[4 * i + 2] ||
        map[4 * i + 3] != 255 || (db && z != expected) ||
        (stencil && map[cb + depthsize + i] != 0x5a))
      bad++;
  }
  printf("%s: %s %s, %u pixels, %u failures\n", bad ? "FAIL" : "PASS", mode,
         readonly ? "readonly" : "none", W * H, bad);
  vkUnmapMemory(dev, bm);
  vkDeviceWaitIdle(dev);
  vkDestroyCommandPool(dev, pool, 0);
  for (unsigned i = 0; i < 2; i++)
    vkDestroyPipeline(dev, pipelines[i], 0);
  vkDestroyPipelineLayout(dev, layout, 0);
  vkDestroyShaderModule(dev, vs, 0);
  vkDestroyShaderModule(dev, fs, 0);
  vkDestroyBuffer(dev, buffer, 0);
  vkFreeMemory(dev, bm, 0);
  struct img images[] = {color, ds};
  for (unsigned i = 0; i < 2; i++) {
    vkDestroyImageView(dev, images[i].view, 0);
    vkDestroyImage(dev, images[i].image, 0);
    vkFreeMemory(dev, images[i].mem, 0);
  }
  vkDestroyDevice(dev, 0);
  vkDestroyInstance(inst, 0);
  return bad ? 1 : 0;
}
