/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * M4 raw-buffer compute oracle, adapted from the Aurora compute stride probe.
 * Accepts SPIR-V, initial bytes and independently computed expected bytes.
 * Hardware execution MUST use the workspace watchdog/thermal guard.
 */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VK_FN(name) static PFN_##name name

VK_FN(vkGetInstanceProcAddr);
VK_FN(vkGetDeviceProcAddr);
VK_FN(vkCreateInstance);
VK_FN(vkDestroyInstance);
VK_FN(vkEnumeratePhysicalDevices);
VK_FN(vkGetPhysicalDeviceProperties);
VK_FN(vkGetPhysicalDeviceMemoryProperties);
VK_FN(vkGetPhysicalDeviceQueueFamilyProperties);
VK_FN(vkCreateDevice);
VK_FN(vkDestroyDevice);
VK_FN(vkGetDeviceQueue);
VK_FN(vkCreateBuffer);
VK_FN(vkDestroyBuffer);
VK_FN(vkGetBufferMemoryRequirements);
VK_FN(vkAllocateMemory);
VK_FN(vkFreeMemory);
VK_FN(vkBindBufferMemory);
VK_FN(vkMapMemory);
VK_FN(vkUnmapMemory);
VK_FN(vkCreateDescriptorSetLayout);
VK_FN(vkDestroyDescriptorSetLayout);
VK_FN(vkCreateDescriptorPool);
VK_FN(vkDestroyDescriptorPool);
VK_FN(vkAllocateDescriptorSets);
VK_FN(vkUpdateDescriptorSets);
VK_FN(vkCreateShaderModule);
VK_FN(vkDestroyShaderModule);
VK_FN(vkCreatePipelineLayout);
VK_FN(vkDestroyPipelineLayout);
VK_FN(vkCreateComputePipelines);
VK_FN(vkDestroyPipeline);
VK_FN(vkCreateCommandPool);
VK_FN(vkDestroyCommandPool);
VK_FN(vkAllocateCommandBuffers);
VK_FN(vkBeginCommandBuffer);
VK_FN(vkCmdBindPipeline);
VK_FN(vkCmdBindDescriptorSets);
VK_FN(vkCmdDispatch);
VK_FN(vkCmdPipelineBarrier);
VK_FN(vkEndCommandBuffer);
VK_FN(vkCreateFence);
VK_FN(vkDestroyFence);
VK_FN(vkQueueSubmit);
VK_FN(vkWaitForFences);

static void die(const char *what) {
  fprintf(stderr, "vk-buffer-oracle: %s\n", what);
  exit(EXIT_FAILURE);
}

static void check(VkResult result, const char *what) {
  if (result != VK_SUCCESS) {
    fprintf(stderr, "vk-buffer-oracle: %s returned %d\n", what, result);
    exit(EXIT_FAILURE);
  }
}

static uint64_t monotonic_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts))
    die("cannot read monotonic clock");
  return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

#define LOAD_GLOBAL(name)                                                      \
  do {                                                                         \
    name = (PFN_##name)vkGetInstanceProcAddr(VK_NULL_HANDLE, #name);           \
    if (!name)                                                                 \
      die("loader is missing " #name);                                         \
  } while (0)

#define LOAD_INSTANCE(instance, name)                                          \
  do {                                                                         \
    name = (PFN_##name)vkGetInstanceProcAddr(instance, #name);                 \
    if (!name)                                                                 \
      die("instance is missing " #name);                                       \
  } while (0)

#define LOAD_DEVICE(device, name)                                              \
  do {                                                                         \
    name = (PFN_##name)vkGetDeviceProcAddr(device, #name);                     \
    if (!name)                                                                 \
      die("device is missing " #name);                                         \
  } while (0)

static uint32_t
find_memory_type(const VkPhysicalDeviceMemoryProperties *properties,
                 uint32_t allowed, VkMemoryPropertyFlags required) {
  for (uint32_t i = 0; i < properties->memoryTypeCount; ++i) {
    if ((allowed & (1u << i)) &&
        (properties->memoryTypes[i].propertyFlags & required) == required)
      return i;
  }

  die("host-visible coherent memory is unavailable");
  return UINT32_MAX;
}

static void *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f || fseek(f, 0, SEEK_END))
    die("cannot open input file");
  long length = ftell(f);
  if (length <= 0 || (length % 4))
    die("input must contain whole 32-bit words");
  *size = length;
  rewind(f);
  void *data = malloc(*size);
  if (!data || fread(data, 1, *size, f) != *size)
    die("cannot read input file");
  fclose(f);
  return data;
}

int main(int argc, char **argv) {
  if (argc != 6)
    die("usage: vk-buffer-oracle shader.spv input.bin expected.bin groups-x "
        "actual.bin");
  size_t shader_size, input_size, expected_size;
  uint32_t *spv = read_file(argv[1], &shader_size);
  uint32_t *input = read_file(argv[2], &input_size);
  uint32_t *expected = read_file(argv[3], &expected_size);
  if (input_size != expected_size)
    die("input and expected sizes differ");
  char *end;
  unsigned long groups = strtoul(argv[4], &end, 10);
  if (!groups || *end || groups > UINT32_MAX)
    die("invalid group count");
  const size_t element_count = input_size / 4;
  uint32_t *snapshot = malloc(input_size);
  if (!snapshot)
    die("cannot allocate readback snapshot");
  memset(snapshot, 0, input_size);
  size_t watched = 0;
  const char *watch = getenv("M4_ORACLE_WATCH_WORD");
  if (watch) {
    watched = strtoul(watch, &end, 0);
    if (*end || watched >= element_count)
      die("invalid watched word index");
  }
  unsigned long trace_us = 0;
  const char *trace = getenv("M4_ORACLE_WATCH_TRACE_US");
  if (trace) {
    trace_us = strtoul(trace, &end, 10);
    if (!watch || *end || !trace_us || trace_us > 100000)
      die("watch trace requires a watched word and 1..100000 microseconds");
  }
  struct watch_sample { uint64_t ns; uint32_t value; };
  const unsigned sample_capacity = 16384;
  struct watch_sample *samples = trace ? calloc(sample_capacity, sizeof(*samples)) : NULL;
  if (trace && !samples)
    die("cannot allocate watch trace");
  void *loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!loader)
    die(dlerror());

  vkGetInstanceProcAddr =
      (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
  if (!vkGetInstanceProcAddr)
    die("libvulkan.so.1 has no vkGetInstanceProcAddr");
  LOAD_GLOBAL(vkCreateInstance);

  const VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "vk-buffer-oracle",
      .applicationVersion = 1,
      .pEngineName = "none",
      .engineVersion = 1,
      .apiVersion = VK_API_VERSION_1_1,
  };
  const VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application_info,
  };
  VkInstance instance;
  check(vkCreateInstance(&instance_info, NULL, &instance), "vkCreateInstance");

  LOAD_INSTANCE(instance, vkGetDeviceProcAddr);
  LOAD_INSTANCE(instance, vkDestroyInstance);
  LOAD_INSTANCE(instance, vkEnumeratePhysicalDevices);
  LOAD_INSTANCE(instance, vkGetPhysicalDeviceProperties);
  LOAD_INSTANCE(instance, vkGetPhysicalDeviceMemoryProperties);
  LOAD_INSTANCE(instance, vkGetPhysicalDeviceQueueFamilyProperties);
  LOAD_INSTANCE(instance, vkCreateDevice);

  uint32_t physical_count = 0;
  check(vkEnumeratePhysicalDevices(instance, &physical_count, NULL),
        "vkEnumeratePhysicalDevices(count)");
  if (!physical_count)
    die("Vulkan loader found no physical device");

  VkPhysicalDevice *physical_devices =
      calloc(physical_count, sizeof(*physical_devices));
  if (!physical_devices)
    die("out of host memory");
  check(vkEnumeratePhysicalDevices(instance, &physical_count, physical_devices),
        "vkEnumeratePhysicalDevices(list)");

  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties properties;
  const bool software = getenv("M4_ORACLE_SOFTWARE") &&
                        !strcmp(getenv("M4_ORACLE_SOFTWARE"), "1");
  for (uint32_t i = 0; i < physical_count; ++i) {
    vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
    if (software ? (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU &&
                    strstr(properties.deviceName, "llvmpipe")) :
                   (properties.deviceID == 0x8132 &&
                    strstr(properties.deviceName, "M4"))) {
      physical = physical_devices[i];
      break;
    }
  }
  free(physical_devices);
  if (physical == VK_NULL_HANDLE)
    die(software ? "lavapipe CPU device was not found" :
                   "M4 Vulkan device was not found");

  printf("device: %s (%#x)\n", properties.deviceName, properties.deviceID);
  if (groups > properties.limits.maxComputeWorkGroupCount[0])
    die("group count exceeds device limit");
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_family_count, NULL);
  VkQueueFamilyProperties *queue_families =
      calloc(queue_family_count, sizeof(*queue_families));
  if (!queue_families)
    die("out of host memory");
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_family_count,
                                           queue_families);

  uint32_t queue_family = UINT32_MAX;
  for (uint32_t i = 0; i < queue_family_count; ++i) {
    if (queue_families[i].queueCount != 0 &&
        (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
      queue_family = i;
      break;
    }
  }
  if (queue_family == UINT32_MAX)
    die("device has no usable queue family");
  const VkQueueFlags queue_flags = queue_families[queue_family].queueFlags;
  free(queue_families);

  const float priority = 1.0f;
  const VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = queue_family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
  };
  const VkPhysicalDeviceFeatures features = {.shaderInt64 = VK_TRUE};
  const VkDeviceCreateInfo device_info = {
      .pEnabledFeatures = &features,
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
  };
  VkDevice device;
  check(vkCreateDevice(physical, &device_info, NULL, &device),
        "vkCreateDevice");

  LOAD_DEVICE(device, vkDestroyDevice);
  LOAD_DEVICE(device, vkGetDeviceQueue);
  LOAD_DEVICE(device, vkCreateBuffer);
  LOAD_DEVICE(device, vkDestroyBuffer);
  LOAD_DEVICE(device, vkGetBufferMemoryRequirements);
  LOAD_DEVICE(device, vkAllocateMemory);
  LOAD_DEVICE(device, vkFreeMemory);
  LOAD_DEVICE(device, vkBindBufferMemory);
  LOAD_DEVICE(device, vkMapMemory);
  LOAD_DEVICE(device, vkUnmapMemory);
  LOAD_DEVICE(device, vkCreateDescriptorSetLayout);
  LOAD_DEVICE(device, vkDestroyDescriptorSetLayout);
  LOAD_DEVICE(device, vkCreateDescriptorPool);
  LOAD_DEVICE(device, vkDestroyDescriptorPool);
  LOAD_DEVICE(device, vkAllocateDescriptorSets);
  LOAD_DEVICE(device, vkUpdateDescriptorSets);
  LOAD_DEVICE(device, vkCreateShaderModule);
  LOAD_DEVICE(device, vkDestroyShaderModule);
  LOAD_DEVICE(device, vkCreatePipelineLayout);
  LOAD_DEVICE(device, vkDestroyPipelineLayout);
  LOAD_DEVICE(device, vkCreateComputePipelines);
  LOAD_DEVICE(device, vkDestroyPipeline);
  LOAD_DEVICE(device, vkCreateCommandPool);
  LOAD_DEVICE(device, vkDestroyCommandPool);
  LOAD_DEVICE(device, vkAllocateCommandBuffers);
  LOAD_DEVICE(device, vkBeginCommandBuffer);
  LOAD_DEVICE(device, vkCmdBindPipeline);
  LOAD_DEVICE(device, vkCmdBindDescriptorSets);
  LOAD_DEVICE(device, vkCmdDispatch);
  LOAD_DEVICE(device, vkCmdPipelineBarrier);
  LOAD_DEVICE(device, vkEndCommandBuffer);
  LOAD_DEVICE(device, vkCreateFence);
  LOAD_DEVICE(device, vkDestroyFence);
  LOAD_DEVICE(device, vkQueueSubmit);
  LOAD_DEVICE(device, vkWaitForFences);

  VkQueue queue;
  vkGetDeviceQueue(device, queue_family, 0, &queue);

  const VkDeviceSize buffer_size = element_count * sizeof(uint32_t);
  const VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = buffer_size,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  VkBuffer buffer;
  check(vkCreateBuffer(device, &buffer_info, NULL, &buffer), "vkCreateBuffer");

  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
  const uint32_t memory_type =
      find_memory_type(&memory_properties, requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  const VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = requirements.size,
      .memoryTypeIndex = memory_type,
  };
  VkDeviceMemory memory;
  check(vkAllocateMemory(device, &memory_info, NULL, &memory),
        "vkAllocateMemory");
  check(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");

  uint32_t *mapped;
  check(vkMapMemory(device, memory, 0, buffer_size, 0, (void **)&mapped),
        "vkMapMemory");
  memcpy(mapped, input, input_size);

  const VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutCreateInfo set_layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &binding,
  };
  VkDescriptorSetLayout set_layout;
  check(
      vkCreateDescriptorSetLayout(device, &set_layout_info, NULL, &set_layout),
      "vkCreateDescriptorSetLayout");

  const VkDescriptorPoolSize pool_size = {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
  };
  const VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_size,
  };
  VkDescriptorPool descriptor_pool;
  check(vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool),
        "vkCreateDescriptorPool");

  const VkDescriptorSetAllocateInfo set_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &set_layout,
  };
  VkDescriptorSet descriptor_set;
  check(vkAllocateDescriptorSets(device, &set_info, &descriptor_set),
        "vkAllocateDescriptorSets");
  const VkDescriptorBufferInfo descriptor_buffer = {
      .buffer = buffer,
      .offset = 0,
      .range = buffer_size,
  };
  const VkWriteDescriptorSet descriptor_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptor_set,
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &descriptor_buffer,
  };
  vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, NULL);

  const VkShaderModuleCreateInfo shader_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = shader_size,
      .pCode = spv,
  };
  VkShaderModule shader;
  check(vkCreateShaderModule(device, &shader_info, NULL, &shader),
        "vkCreateShaderModule");
  const VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &set_layout,
  };
  VkPipelineLayout pipeline_layout;
  check(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL,
                               &pipeline_layout),
        "vkCreatePipelineLayout");
  const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
          {
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = shader,
              .pName = "main",
          },
      .layout = pipeline_layout,
  };
  VkPipeline pipeline;
  check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                 NULL, &pipeline),
        "vkCreateComputePipelines");

  const VkCommandPoolCreateInfo command_pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = queue_family,
  };
  VkCommandPool command_pool;
  check(vkCreateCommandPool(device, &command_pool_info, NULL, &command_pool),
        "vkCreateCommandPool");
  const VkCommandBufferAllocateInfo command_buffer_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  VkCommandBuffer command_buffer;
  check(vkAllocateCommandBuffers(device, &command_buffer_info, &command_buffer),
        "vkAllocateCommandBuffers");
  const VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  check(vkBeginCommandBuffer(command_buffer, &begin_info),
        "vkBeginCommandBuffer");
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
  vkCmdDispatch(command_buffer, groups, 1, 1);
  const VkBufferMemoryBarrier host_barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = buffer,
      .offset = 0,
      .size = buffer_size,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &host_barrier,
                       0, NULL);
  check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

  const VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  };
  VkFence fence;
  check(vkCreateFence(device, &fence_info, NULL, &fence), "vkCreateFence");
  const VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
  };
  check(vkQueueSubmit(queue, 1, &submit_info, fence), "vkQueueSubmit");
  check(vkWaitForFences(device, 1, &fence, true, UINT64_C(20000000000)),
        "vkWaitForFences");

  /* Preserve the first observations before any file I/O can conceal delayed
   * GPU visibility. All comparison and saved evidence use this same snapshot. */
  uint32_t first_watched = watch ? ((volatile uint32_t *)mapped)[watched] : 0;
  const uint64_t fence_ns = monotonic_ns();
  unsigned sample_count = 0, dropped_samples = 0;
  if (trace) {
    uint32_t previous = first_watched;
    samples[sample_count++] = (struct watch_sample){fence_ns, previous};
    uint64_t now = fence_ns;
    while (previous != expected[watched] && now - fence_ns < trace_us * 1000) {
      uint32_t value = ((volatile uint32_t *)mapped)[watched];
      now = monotonic_ns();
      if (value != previous) {
        if (sample_count < sample_capacity)
          samples[sample_count++] = (struct watch_sample){now, value};
        else
          dropped_samples++;
        previous = value;
      }
    }
  }
  /* In watch-trace mode the full snapshot intentionally follows the bounded
   * observation interval. The immediate watched mismatch still fails the test.
   * This diagnostic is never a driver wait or a synchronization workaround. */
  for (size_t i = 0; i < element_count; i++)
    snapshot[i] = ((volatile uint32_t *)mapped)[i];
  unsigned bad = 0;
  for (size_t i = 0; i < element_count; i++) {
    if (snapshot[i] != expected[i]) {
      if (bad < 8)
        fprintf(stderr, "word %zu got %#x expected %#x input %#x\n", i,
                snapshot[i], expected[i], input[i]);
      bad++;
    }
  }
  if (watch) {
    printf("First watched word %zu got %#x expected %#x snapshot %#x\n",
           watched, first_watched, expected[watched], snapshot[watched]);
  }
  const bool watched_bad = watch && first_watched != expected[watched];
  if (trace) {
    printf("Watch trace: fence_observed_ns=%llu dropped=%u snapshot_after_trace=1\n",
           (unsigned long long)fence_ns, dropped_samples);
    for (unsigned i = 0; i < sample_count; i++)
      printf("Watch change: after_ns=%llu value=%#x\n",
             (unsigned long long)(samples[i].ns - fence_ns), samples[i].value);
  }
  FILE *actual = fopen(argv[5], "wb");
  if (!actual || fwrite(snapshot, 1, input_size, actual) != input_size ||
      fflush(actual) || fclose(actual))
    die("cannot save actual output");
  printf("%s: buffer oracle bad=%u/%zu watched_bad=%u on queue %u flags=%#x\n",
         (bad || watched_bad) ? "FAIL" : "PASS", bad, element_count,
         watched_bad, queue_family, queue_flags);

  vkDestroyFence(device, fence, NULL);
  vkDestroyCommandPool(device, command_pool, NULL);
  vkDestroyPipeline(device, pipeline, NULL);
  vkDestroyPipelineLayout(device, pipeline_layout, NULL);
  vkDestroyShaderModule(device, shader, NULL);
  vkDestroyDescriptorPool(device, descriptor_pool, NULL);
  vkDestroyDescriptorSetLayout(device, set_layout, NULL);
  vkUnmapMemory(device, memory);
  vkDestroyBuffer(device, buffer, NULL);
  vkFreeMemory(device, memory, NULL);
  vkDestroyDevice(device, NULL);
  vkDestroyInstance(instance, NULL);
  dlclose(loader);
  free(spv);
  free(input);
  free(expected);
  free(snapshot);
  free(samples);
  return (bad || watched_bad) ? EXIT_FAILURE : EXIT_SUCCESS;
}
