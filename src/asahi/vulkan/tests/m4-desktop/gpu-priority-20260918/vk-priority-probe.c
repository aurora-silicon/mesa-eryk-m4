/* Bounded Vulkan scheduling probe. Run only under gpu-experiment.py.
 * Usage: probe shader.spv priority iterations batch rounds ready go
 * output.jsonl Priority: 0 low, 1 medium, 2 high, 3 realtime. Each batch is one
 * submission containing separate command buffers (independent native command
 * boundaries).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#define CHECK(call)                                                            \
  do {                                                                         \
    VkResult r = (call);                                                       \
    if (r != VK_SUCCESS) {                                                     \
      fprintf(stderr, "%s: %d\n", #call, r);                                   \
      exit(2);                                                                 \
    }                                                                          \
  } while (0)
static uint64_t now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void nap(void) {
  struct timespec t = {0, 1000000};
  nanosleep(&t, NULL);
}
int main(int argc, char **argv) {
  if (argc != 9)
    return 2;
  unsigned priority = strtoul(argv[2], 0, 0),
           iterations = strtoul(argv[3], 0, 0), batch = strtoul(argv[4], 0, 0),
           rounds = strtoul(argv[5], 0, 0);
  if (priority > 3 || !iterations || iterations > 1000000 || !batch ||
      batch > 128 || !rounds || rounds > 256)
    return 2;
  VkInstance instance;
  VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  CHECK(vkCreateInstance(&ici, 0, &instance));
  uint32_t n = 1;
  VkPhysicalDevice physical;
  CHECK(vkEnumeratePhysicalDevices(instance, &n, &physical));
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physical, &props);
  fprintf(stderr, "Device: %s\n", props.deviceName);
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, 0);
  VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
  unsigned family = 0;
  while (family < count &&
         !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT))
    family++;
  if (family == count)
    return 2;
  free(families);
  VkQueueGlobalPriorityKHR priorities[] = {
      VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR, VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
      VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR, VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR};
  VkDeviceQueueGlobalPriorityCreateInfoKHR gp = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR,
      .globalPriority = priorities[priority]};
  float qpriority = 1;
  VkDeviceQueueCreateInfo qi = {.sType =
                                    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .pNext = &gp,
                                .queueFamilyIndex = family,
                                .queueCount = 1,
                                .pQueuePriorities = &qpriority};
  const char *ext = VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME;
  VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                           .queueCreateInfoCount = 1,
                           .pQueueCreateInfos = &qi,
                           .enabledExtensionCount = 1,
                           .ppEnabledExtensionNames = &ext};
  VkDevice device;
  CHECK(vkCreateDevice(physical, &di, 0, &device));
  VkQueue queue;
  vkGetDeviceQueue(device, family, 0, &queue);
  VkBuffer buffer;
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = (VkDeviceSize)batch * 256 * 4,
                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
  CHECK(vkCreateBuffer(device, &bi, 0, &buffer));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(device, buffer, &req);
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(physical, &mp);
  unsigned type = 0;
  while (type < mp.memoryTypeCount &&
         (!(req.memoryTypeBits & (1u << type)) ||
          (mp.memoryTypes[type].propertyFlags &
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) !=
              (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
    type++;
  if (type == mp.memoryTypeCount)
    return 2;
  VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                             .allocationSize = req.size,
                             .memoryTypeIndex = type};
  VkDeviceMemory memory;
  CHECK(vkAllocateMemory(device, &ai, 0, &memory));
  CHECK(vkBindBufferMemory(device, buffer, memory, 0));
  uint32_t *mapped;
  CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&mapped));
  VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
  VkDescriptorSetLayoutCreateInfo li = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &binding};
  VkDescriptorSetLayout layout;
  CHECK(vkCreateDescriptorSetLayout(device, &li, 0, &layout));
  VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
  VkDescriptorPoolCreateInfo pi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &size};
  VkDescriptorPool pool;
  CHECK(vkCreateDescriptorPool(device, &pi, 0, &pool));
  VkDescriptorSetAllocateInfo si = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &layout};
  VkDescriptorSet set;
  CHECK(vkAllocateDescriptorSets(device, &si, &set));
  VkDescriptorBufferInfo db = {buffer, 0, VK_WHOLE_SIZE};
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = set,
                                .descriptorCount = 1,
                                .descriptorType =
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                .pBufferInfo = &db};
  vkUpdateDescriptorSets(device, 1, &write, 0, 0);
  VkPushConstantRange pc = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
  VkPipelineLayoutCreateInfo pl = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pc};
  VkPipelineLayout pipeline_layout;
  CHECK(vkCreatePipelineLayout(device, &pl, 0, &pipeline_layout));
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  fseek(f, 0, SEEK_END);
  long bytes = ftell(f);
  rewind(f);
  uint32_t *code = malloc(bytes);
  if (fread(code, 1, bytes, f) != (size_t)bytes)
    return 2;
  fclose(f);
  VkShaderModuleCreateInfo smi = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = bytes,
      .pCode = code};
  VkShaderModule shader;
  CHECK(vkCreateShaderModule(device, &smi, 0, &shader));
  free(code);
  VkComputePipelineCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shader,
                .pName = "main"},
      .layout = pipeline_layout};
  VkPipeline pipeline;
  CHECK(vkCreateComputePipelines(device, 0, 1, &ci, 0, &pipeline));
  VkCommandPoolCreateInfo cpi = {.sType =
                                     VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                 .queueFamilyIndex = family};
  VkCommandPool cp;
  CHECK(vkCreateCommandPool(device, &cpi, 0, &cp));
  VkCommandBuffer *cb = calloc(batch, sizeof(*cb));
  VkCommandBufferAllocateInfo cai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cp,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = batch};
  CHECK(vkAllocateCommandBuffers(device, &cai, cb));
  for (unsigned i = 0; i < batch; i++) {
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(cb[i], &begin));
    vkCmdBindPipeline(cb[i], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cb[i], VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout, 0, 1, &set, 0, 0);
    uint32_t constants[2] = {iterations, i * 256};
    vkCmdPushConstants(cb[i], pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       8, constants);
    vkCmdDispatch(cb[i], 4, 1, 1);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT |
                                                VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, 0, 0, 0);
    CHECK(vkEndCommandBuffer(cb[i]));
  }
  VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence;
  CHECK(vkCreateFence(device, &fi, 0, &fence));
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                         .commandBufferCount = batch,
                         .pCommandBuffers = cb};
  uint32_t *expected = malloc(batch * 1024);
  for (unsigned i = 0; i < batch * 256; i++) {
    uint32_t v = (i % 256) + 1;
    for (unsigned j = 0; j < iterations; j++)
      v = (v * 1664525u + 1013904223u) ^ (v >> 13);
    expected[i] = v;
  }
  f = fopen(argv[8], "w");
  if (!f)
    return 2;
  FILE *ready = fopen(argv[6], "w");
  if (!ready)
    return 2;
  fprintf(ready, "ready\n");
  fclose(ready);
  uint64_t deadline = now() + 20000000000ull;
  while (access(argv[7], F_OK)) {
    if (now() > deadline)
      return 2;
    nap();
  }
  for (unsigned i = 0; i < rounds; i++) {
    memset(mapped, 0, batch * 1024);
    uint64_t start = now();
    CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    uint64_t submitted = now();
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    uint64_t end = now();
    if (memcmp(mapped, expected, batch * 1024)) {
      fprintf(stderr, "FAIL: readback round %u\n", i);
      return 3;
    }
    fprintf(f,
            "{\"round\":%u,\"priority\":%u,\"batch\":%u,\"start_ns\":%llu,"
            "\"submit_ns\":%llu,\"end_ns\":%llu,\"latency_ms\":%.6f}\n",
            i, priority, batch, (unsigned long long)start,
            (unsigned long long)submitted, (unsigned long long)end,
            (end - start) / 1e6);
    fflush(f);
    CHECK(vkResetFences(device, 1, &fence));
  }
  fclose(f);
  free(expected);
  CHECK(vkDeviceWaitIdle(device));
  vkDestroyFence(device, fence, 0);
  vkDestroyCommandPool(device, cp, 0);
  free(cb);
  vkDestroyPipeline(device, pipeline, 0);
  vkDestroyShaderModule(device, shader, 0);
  vkDestroyPipelineLayout(device, pipeline_layout, 0);
  vkDestroyDescriptorPool(device, pool, 0);
  vkDestroyDescriptorSetLayout(device, layout, 0);
  vkDestroyBuffer(device, buffer, 0);
  vkUnmapMemory(device, memory);
  vkFreeMemory(device, memory, 0);
  vkDestroyDevice(device, 0);
  vkDestroyInstance(instance, 0);
  puts("PASS: priority probe readbacks");
  return 0;
}
