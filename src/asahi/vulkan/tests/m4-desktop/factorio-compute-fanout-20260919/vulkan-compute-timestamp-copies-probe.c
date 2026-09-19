/* Mode18: one fill then consecutive timestamps; exact groups of nine and overflow.
 * Adjacent query-copy batching control: modes 16/17 use separate API calls.
 * SPDX-License-Identifier: MIT
 * Modes 16/17: host-reset timestamp copies across flags 0..7 (16), and
 * unavailable odd queries across flags 0,1,4,5 (17). Verify untouched bytes.
 * Modes 0/1: unavailable timestamp copies without WAIT.
 * Mode 2: empty fragment-statistics query followed by a WAIT copy.
 * Mode 3: 64 adjacent individual timestamp resets after prior writes; rewrite
 * even queries and verify odd queries become unavailable. Unavailable result
 * values are undefined and are deliberately not compared.
 * Mode 4: render-end timestamp followed by eleven writes after EndRendering;
 * verify monotonicity and CPU/GPU result copies, including overflow.
 * Mode 5: fill + memory barrier + timestamp, checking ordered CPU/GPU query
 * results and preserved fill data after the closed compute tail (WAIT copy).
 * Mode 6: same sequence without WAIT; safely diagnose unavailable results.
 * Mode 7: mode 6 with a host reset instead of a GPU reset.
 * Mode 8: host reset and timestamps BEFORE each fill, checking compute starts.
 * cc -O2 -Wall -Wextra vulkan-query-copy-probe.c -o vulkan-query-copy-probe
 * -lvulkan Run only inside gpu-experiment.py on hardware. No window system
 * required.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    VkResult r = (x);                                                          \
    if (r != VK_SUCCESS) {                                                     \
      fprintf(stderr, "%s: %d\n", #x, r);                                      \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static VkPhysicalDeviceMemoryProperties memory;
static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags flags) {
  for (unsigned i = 0; i < memory.memoryTypeCount; i++)
    if ((bits & (1u << i)) &&
        (memory.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  fprintf(stderr, "No memory type\n");
  exit(1);
}
int main(int argc, char **argv) {
  const unsigned queue_count = 1;
  int mode = argc > 1 ? atoi(argv[1]) : 0;
  if (mode < 0 || (mode > 8 && mode != 16 && mode != 17 && mode != 18))
    return 2;
  const unsigned copy_flags = argc > 2 ? strtoul(argv[2], NULL, 0) : 5;
  if ((mode == 16 || mode == 17 || mode == 18) &&
      (copy_flags > 7 || (mode == 17 && (copy_flags & VK_QUERY_RESULT_WAIT_BIT)))) return 2;
  unsigned queries = mode == 3 ? 64 : mode == 4 ? 12 : 4;
  if (argc > 3) {
    if (mode != 16 && mode != 17 && mode != 18) return 2;
    queries = strtoul(argv[3], NULL, 0);
    if (!queries || queries > 64) return 2;
  }
  VkInstance instance;
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_3};
  VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                             .pApplicationInfo = &app};
  CHECK(vkCreateInstance(&ic, NULL, &instance));
  uint32_t n = 1;
  VkPhysicalDevice physical;
  CHECK(vkEnumeratePhysicalDevices(instance, &n, &physical));
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical, &properties);
  printf("Device: %s\n", properties.deviceName);
  fflush(stdout);
  vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, NULL);
  VkQueueFamilyProperties *families = calloc(n, sizeof(*families));
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families);
  unsigned family = 0;
  while (family < n && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
    family++;
  if (family == n || families[family].queueCount < queue_count) {
    fprintf(stderr, "Need a graphics queue\n");
    return 1;
  }
  free(families);
  float priority[1] = {1};
  VkDeviceQueueCreateInfo qi = {.sType =
                                    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .queueFamilyIndex = family,
                                .queueCount = queue_count,
                                .pQueuePriorities = priority};
  VkPhysicalDeviceTimelineSemaphoreFeatures tf = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
      .timelineSemaphore = VK_TRUE};
  VkPhysicalDeviceDynamicRenderingFeatures df = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
      .dynamicRendering = mode == 4};
  VkPhysicalDeviceHostQueryResetFeatures hq = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
    .hostQueryReset = mode >= 7};
  df.pNext = &hq;
  tf.pNext = &df;
  VkPhysicalDeviceFeatures features = {.pipelineStatisticsQuery = mode == 2};
  VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                           .pNext = &tf,
                           .pEnabledFeatures = &features,
                           .queueCreateInfoCount = 1,
                           .pQueueCreateInfos = &qi};
  VkDevice dev;
  CHECK(vkCreateDevice(physical, &di, NULL, &dev));
  VkQueue queue[1];
  for (unsigned i = 0; i < queue_count; i++)
    vkGetDeviceQueue(dev, family, i, &queue[i]);

  VkBuffer buffer;
  VkDeviceMemory allocation;
  VkMemoryRequirements req;
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = 8192,
                           .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  CHECK(vkCreateBuffer(dev, &bi, NULL, &buffer));
  vkGetBufferMemoryRequirements(dev, buffer, &req);
  VkMemoryAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = memtype(req.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
  CHECK(vkAllocateMemory(dev, &ai, NULL, &allocation));
  CHECK(vkBindBufferMemory(dev, buffer, allocation, 0));
  uint32_t *data;
  CHECK(vkMapMemory(dev, allocation, 0, VK_WHOLE_SIZE, 0, (void **)&data));
  memset(data, 0, 8192);
  VkCommandPool pool;
  VkCommandPoolCreateInfo pi = {.sType =
                                    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                .queueFamilyIndex = family};
  CHECK(vkCreateCommandPool(dev, &pi, NULL, &pool));
  VkCommandBuffer cmds[1];
  VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  CHECK(vkAllocateCommandBuffers(dev, &ca, cmds));

  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory image_memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  if (mode == 4) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = {16,16,1}, .mipLevels = 1, .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    CHECK(vkCreateImage(dev, &ii, NULL, &image));
    vkGetImageMemoryRequirements(dev, image, &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memtype(req.memoryTypeBits, 0);
    CHECK(vkAllocateMemory(dev, &ai, NULL, &image_memory));
    CHECK(vkBindImageMemory(dev, image, image_memory, 0));
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = ii.format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    CHECK(vkCreateImageView(dev, &vi, NULL, &view));
  }
  VkQueryPool query;
  VkQueryPoolCreateInfo qc = {
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = mode == 2 ? VK_QUERY_TYPE_PIPELINE_STATISTICS
                             : VK_QUERY_TYPE_TIMESTAMP,
      .queryCount = queries,
      .pipelineStatistics =
          mode == 2
              ? VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT
              : 0};
  CHECK(vkCreateQueryPool(dev, &qc, NULL, &query));
  VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  CHECK(vkBeginCommandBuffer(cmds[0], &begin));
  if (mode >= 7) vkResetQueryPool(dev, query, 0, queries);
  else vkCmdResetQueryPool(cmds[0], query, 0, queries);
  if (mode == 3) {
    for (unsigned i = 0; i < queries; i++)
      vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, i);
    for (unsigned i = 0; i < queries; i++)
      vkCmdResetQueryPool(cmds[0], query, i, 1);
    for (unsigned i = 0; i < queries; i += 2)
      vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, i);
  }
  if (mode == 4) {
    VkImageMemoryBarrier ib = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(cmds[0], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&ib);
    VkRenderingAttachmentInfo ra = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.color = {.float32 = {1,0,0,1}}}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0,0},{16,16}}, .layerCount = 1,
      .colorAttachmentCount = 1, .pColorAttachments = &ra};
    vkCmdBeginRendering(cmds[0], &ri);
    vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, 0);
    vkCmdEndRendering(cmds[0]);
    for (unsigned i = 1; i < queries; i++)
      vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, i);
  }
  if (mode == 5 || mode == 6 || mode == 7 || mode == 8 || mode == 16 || mode == 17 || mode == 18) {
    for (unsigned i = 0; i < queries; i++) {
      if (mode == 8) {
        vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, i);
        vkCmdPipelineBarrier(cmds[0], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 0, NULL);
      }
      if (mode != 18 || i == 0) {
      vkCmdFillBuffer(cmds[0], buffer, 1024 + i * 64, 64, 0x12345670u + i);
      VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT};
      vkCmdPipelineBarrier(cmds[0], VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
      }
      if (mode != 8 && (mode != 17 || i % 2 == 0)) vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, i);
    }
  }
  unsigned write = mode == 1;
  if (mode == 2) {
    vkCmdBeginQuery(cmds[0], query, 0, 0);
    vkCmdEndQuery(cmds[0], query, 0);
  }
  if (write)
    vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query,
                        0);
  for (unsigned i = 0; i < queries * 4; i++)
    data[i] = 0xfeed713;
  if (mode == 3 || mode == 16 || mode == 17 || mode == 18) {
    for (unsigned i = 0; i < queries; i++)
      vkCmdCopyQueryPoolResults(cmds[0], query, i, 1, buffer, 16 * i, 16,
          (mode == 16 || mode == 17 || mode == 18) ? copy_flags :
          (VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT |
          (i % 2 == 0 ? VK_QUERY_RESULT_WAIT_BIT : 0)));
  } else vkCmdCopyQueryPoolResults(cmds[0], query, 0, mode == 2 ? 1 : queries, buffer, 0, 16,
                            (mode == 16 || mode == 17 || mode == 18) ? copy_flags :
                            (VK_QUERY_RESULT_64_BIT |
                                VK_QUERY_RESULT_WITH_AVAILABILITY_BIT |
                                (mode == 2 || mode == 4 || mode == 5 ? VK_QUERY_RESULT_WAIT_BIT : 0)));
  VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                             .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                             .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cmds[0], VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0,
                       NULL);
  CHECK(vkEndCommandBuffer(cmds[0]));
  VkFence f;
  VkFenceCreateInfo fc = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  CHECK(vkCreateFence(dev, &fc, NULL, &f));
  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .commandBufferCount = 1,
                     .pCommandBuffers = cmds};
  CHECK(vkQueueSubmit(queue[0], 1, &si, f));
  CHECK(vkWaitForFences(dev, 1, &f, VK_TRUE, 5000000000ull));
  if (mode == 16 || mode == 17 || mode == 18) {
    uint64_t stamps[64] = {0};
    const unsigned width = (copy_flags & VK_QUERY_RESULT_64_BIT) ? 8 : 4;
    const uint64_t mask = width == 8 ? UINT64_MAX : UINT32_MAX;
    for (unsigned i = 0; i < queries; i++) {
      uint64_t cpu[2] = {0,0}, gpu = 0, available = 0;
      VkResult status = vkGetQueryPoolResults(dev, query, i, 1, sizeof(cpu), cpu, 16,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      const uint64_t written = mode != 17 || i % 2 == 0;
      if (status != (written ? VK_SUCCESS : VK_NOT_READY) || cpu[1] != written) { fprintf(stderr, "query%u status=%d cpu=%llx available=%llu expected=%llu\n", i, status, (unsigned long long)cpu[0], (unsigned long long)cpu[1], (unsigned long long)written); return 1; }
      stamps[i] = cpu[0];
      memcpy(&gpu, (uint8_t *)data + 16*i, width);
      if (written ? gpu != (cpu[0] & mask) : gpu != (UINT64_C(0x0feed7130feed713) & mask)) { fprintf(stderr, "query%u CPU=%llx GPU=%llx mask=%llx\n", i, (unsigned long long)cpu[0], (unsigned long long)gpu, (unsigned long long)mask); return 1; }
      if (copy_flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
        memcpy(&available, (uint8_t *)data + 16*i + width, width);
        if (available != written) { fprintf(stderr, "query%u GPU available=%llu expected=%llu\n", i, (unsigned long long)available, (unsigned long long)written); return 1; }
      }
      const unsigned used = width * ((copy_flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? 2 : 1);
      for (unsigned off = used; off < 16; off++) {
        const uint32_t sentinel = 0x0feed713;
        if (((uint8_t *)data)[16*i+off] != ((const uint8_t *)&sentinel)[off%4]) return 1;
      }
      for (unsigned j = 0; (mode != 18 || i == 0) && j < 16; j++)
        if (data[256+i*16+j] != 0x12345670u+i) return 1;
    }
    if (mode == 18) {
      for (unsigned i = 1; i < queries; i++) {
        if (stamps[i] < stamps[i-1]) { fprintf(stderr, "timestamp%u went backwards\n", i); return 1; }
        if (i % 9 != 0 && stamps[i] != stamps[i-1]) { fprintf(stderr, "timestamp%u fanout differs\n", i); return 1; }
      }
      printf("PASS: compute timestamp fanout groups of nine, %u queries\n", queries);
    }
    printf("QUERY_VARIANT_PASS mode=%d flags=%u\n", mode, copy_flags);
    vkDestroyFence(dev, f, NULL); vkDestroyQueryPool(dev, query, NULL);
    vkDestroyCommandPool(dev, pool, NULL); vkUnmapMemory(dev, allocation);
    vkDestroyBuffer(dev, buffer, NULL); vkFreeMemory(dev, allocation, NULL);
    vkDestroyDevice(dev, NULL); vkDestroyInstance(instance, NULL);
    return 0;
  }
  uint64_t *v = (void *)data;
  if (mode == 6 || mode == 7 || mode == 8) {
    for (unsigned i = 0; i < queries; i++) {
      uint64_t cpu[2] = {0,0};
      VkResult result = vkGetQueryPoolResults(dev, query, i, 1, sizeof(cpu), cpu, 16,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      printf("closed_tail_query%u cpu_status=%d cpu_value=%llx cpu_available=%llu gpu_value=%llx gpu_available=%llu\n",
        i, result, (unsigned long long)cpu[0], (unsigned long long)cpu[1],
        (unsigned long long)v[2*i], (unsigned long long)v[2*i+1]);
    }
    fflush(stdout);
  }
  for (unsigned i = 0; i < (mode == 2 ? 1 : queries); i++) {
    printf("query%u value=%llx available=%llu\n", i,
           (unsigned long long)v[2 * i], (unsigned long long)v[2 * i + 1]);
    if (mode == 4 || mode == 5 || mode == 6 || mode == 7 || mode == 8 || mode == 16 || mode == 17 || mode == 18) {
      if (!v[2*i+1] || !v[2*i] || (i && v[2*i] < v[2*(i-1)])) return 1;
      uint64_t cpu;
      CHECK(vkGetQueryPoolResults(dev, query, i, 1, sizeof(cpu), &cpu, 8, VK_QUERY_RESULT_64_BIT));
      if (cpu != v[2*i]) return 1;
    } else if (mode == 3) {
      if (v[2*i+1] != (i % 2 == 0)) return 1;
      if (i % 2 == 0 && (!v[2*i] || (i >= 2 && v[2*i] < v[2*(i-2)]))) return 1;
    } else if (mode == 2) {
      if (v[0] != 0 || v[1] != 1)
        return 1;
    } else if (i >= write && v[2 * i + 1] != 0)
      return 1;
  }
  if (mode == 5 || mode == 6 || mode == 7 || mode == 8 || mode == 16 || mode == 17 || mode == 18) {
    for (unsigned i = 0; i < queries; i++)
      for (unsigned j = 0; (mode != 18 || i == 0) && j < 16; j++)
        if (data[256 + i * 16 + j] != 0x12345670u + i) return 1;
  }
  puts("QUERY_COPY_PASS");
  vkDestroyFence(dev, f, NULL);
  vkDestroyQueryPool(dev, query, NULL);
  vkDestroyCommandPool(dev, pool, NULL);
  vkUnmapMemory(dev, allocation);
  vkDestroyBuffer(dev, buffer, NULL);
  vkFreeMemory(dev, allocation, NULL);
  if (mode == 4) {
    vkDestroyImageView(dev, view, NULL);
    vkDestroyImage(dev, image, NULL);
    vkFreeMemory(dev, image_memory, NULL);
  }
  vkDestroyDevice(dev, NULL);
  vkDestroyInstance(instance, NULL);
  return 0;
}
