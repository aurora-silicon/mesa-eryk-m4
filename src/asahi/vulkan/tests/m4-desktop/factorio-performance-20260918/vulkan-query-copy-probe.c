/* SPDX-License-Identifier: MIT
 * Modes 0/1: unavailable timestamp copies without WAIT.
 * Mode 2: empty fragment-statistics query followed by a WAIT copy.
 * Mode 3: 64 adjacent individual timestamp resets after prior writes; rewrite
 * even queries and verify odd queries become unavailable. Unavailable result
 * values are undefined and are deliberately not compared.
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
  if (mode < 0 || mode > 3)
    return 2;
  unsigned queries = mode == 3 ? 64 : 4;
  VkInstance instance;
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_2};
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
                           .size = 8 * 256,
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
  memset(data, 0, 8 * 256);
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
  vkCmdResetQueryPool(cmds[0], query, 0, queries);
  if (mode == 3) {
    for (unsigned i = 0; i < queries; i++)
      vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, i);
    for (unsigned i = 0; i < queries; i++)
      vkCmdResetQueryPool(cmds[0], query, i, 1);
    for (unsigned i = 0; i < queries; i += 2)
      vkCmdWriteTimestamp(cmds[0], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, i);
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
  if (mode == 3) {
    for (unsigned i = 0; i < queries; i++)
      vkCmdCopyQueryPoolResults(cmds[0], query, i, 1, buffer, 16 * i, 16,
          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT |
          (i % 2 == 0 ? VK_QUERY_RESULT_WAIT_BIT : 0));
  } else vkCmdCopyQueryPoolResults(cmds[0], query, 0, mode == 2 ? 1 : queries, buffer, 0, 16,
                            VK_QUERY_RESULT_64_BIT |
                                VK_QUERY_RESULT_WITH_AVAILABILITY_BIT |
                                (mode == 2 ? VK_QUERY_RESULT_WAIT_BIT : 0));
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
  uint64_t *v = (void *)data;
  for (unsigned i = 0; i < (mode == 2 ? 1 : queries); i++) {
    printf("query%u value=%llx available=%llu\n", i,
           (unsigned long long)v[2 * i], (unsigned long long)v[2 * i + 1]);
    if (mode == 3) {
      if (v[2*i+1] != (i % 2 == 0)) return 1;
      if (i % 2 == 0 && (!v[2*i] || (i >= 2 && v[2*i] < v[2*(i-2)]))) return 1;
    } else if (mode == 2) {
      if (v[0] != 0 || v[1] != 1)
        return 1;
    } else if (i >= write && v[2 * i + 1] != 0)
      return 1;
  }
  puts("QUERY_COPY_PASS");
  vkDestroyFence(dev, f, NULL);
  vkDestroyQueryPool(dev, query, NULL);
  vkDestroyCommandPool(dev, pool, NULL);
  vkUnmapMemory(dev, allocation);
  vkDestroyBuffer(dev, buffer, NULL);
  vkFreeMemory(dev, allocation, NULL);
  vkDestroyDevice(dev, NULL);
  vkDestroyInstance(instance, NULL);
  return 0;
}
