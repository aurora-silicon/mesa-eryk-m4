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
VK_FN(vkGetPhysicalDeviceProperties2);
VK_FN(vkGetPhysicalDeviceFeatures2);
VK_FN(vkGetPhysicalDeviceMemoryProperties);
VK_FN(vkGetPhysicalDeviceQueueFamilyProperties);
VK_FN(vkCreateDevice);
VK_FN(vkDestroyDevice);
VK_FN(vkGetDeviceQueue);
VK_FN(vkCreateBuffer);
VK_FN(vkDestroyBuffer);
VK_FN(vkCreateBufferView);
VK_FN(vkDestroyBufferView);
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
VK_FN(vkCmdFillBuffer);
VK_FN(vkCmdCopyBuffer);
VK_FN(vkResetFences);
VK_FN(vkCmdPipelineBarrier);
VK_FN(vkEndCommandBuffer);
VK_FN(vkCreateFence);
VK_FN(vkDestroyFence);
VK_FN(vkQueueSubmit);
VK_FN(vkQueueBindSparse);
VK_FN(vkQueueWaitIdle);
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
  const char *fill_option = getenv("M4_ORACLE_FILL32");
  const bool copy_option = getenv("M4_ORACLE_COPY") && !strcmp(getenv("M4_ORACLE_COPY"), "1");
  const bool transfer_option = fill_option || copy_option;
  const bool sparse_alias = getenv("M4_ORACLE_SPARSE_ALIAS") != NULL;
  const char *sparse_fill = getenv("M4_ORACLE_SPARSE_FILL");
  const char *sparse_shader = getenv("M4_ORACLE_SPARSE_SHADER");
  if (sparse_shader && (!sparse_alias || sparse_fill)) die("sparse shader mode conflict");
  if (sparse_alias && (transfer_option || getenv("M4_ORACLE_TEXEL_FORMAT")))
    die("sparse alias requires SSBO compute mode");
  const char *texel_format_option = getenv("M4_ORACLE_TEXEL_FORMAT");
  VkFormat texel_format = VK_FORMAT_UNDEFINED;
  VkDeviceSize texel_offset = 0, texel_range = 0;
  const bool texel_storage = getenv("M4_ORACLE_TEXEL_STORAGE") &&
                            !strcmp(getenv("M4_ORACLE_TEXEL_STORAGE"), "1");
  if (texel_format_option) {
    unsigned long f = strtoul(texel_format_option, &end, 0);
    if (*end || !f || f > INT32_MAX || transfer_option)
      die("invalid texel format or transfer/texel combination");
    texel_format = (VkFormat)f;
    const char *offset = getenv("M4_ORACLE_TEXEL_OFFSET");
    const char *range = getenv("M4_ORACLE_TEXEL_RANGE");
    if (!offset || !range) die("texel mode requires offset and range");
    texel_offset = strtoull(offset, &end, 0);
    if (*end) die("invalid texel offset");
    texel_range = strtoull(range, &end, 0);
    if (*end || !texel_range || texel_offset > input_size ||
        texel_range > input_size - texel_offset)
      die("invalid texel range");
  }
  if (fill_option && copy_option) die("choose fill or copy");
  if (copy_option && input_size % 8) die("copy requires two equal word-aligned halves");
  uint32_t fill_value = 0;
  if (fill_option) {
    unsigned long value = strtoul(fill_option, &end, 0);
    if (*end || value > UINT32_MAX) die("invalid fill32 value");
    fill_value = value;
  }
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
      .apiVersion = texel_format_option ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1,
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
  LOAD_INSTANCE(instance, vkGetPhysicalDeviceProperties2);
  LOAD_INSTANCE(instance, vkGetPhysicalDeviceFeatures2);
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
  if (texel_format_option) {
    VkPhysicalDeviceVulkan13Properties p13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES};
    VkPhysicalDeviceProperties2 p2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &p13};
    vkGetPhysicalDeviceProperties2(physical, &p2);
    if (!p13.uniformTexelBufferOffsetSingleTexelAlignment ||
        !p13.storageTexelBufferOffsetSingleTexelAlignment)
      die("single-texel alignment not advertised");
    printf("texel format=%u offset=%llu range=%llu storage=%u\n", texel_format,
           (unsigned long long)texel_offset, (unsigned long long)texel_range,
           texel_storage);
  }
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
  const bool want_half = getenv("M4_ORACLE_FLOAT16") && !strcmp(getenv("M4_ORACLE_FLOAT16"),"1");
  const bool want_byte = getenv("M4_ORACLE_INT8") && !strcmp(getenv("M4_ORACLE_INT8"),"1");
  VkPhysicalDevice8BitStorageFeatures byte_storage = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES};
  VkPhysicalDeviceShaderFloat16Int8Features half = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
      .pNext = want_byte ? &byte_storage : NULL};
  if (want_half || want_byte) {
    VkPhysicalDeviceFeatures2 query = {.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&half};
    vkGetPhysicalDeviceFeatures2(physical,&query);
    if(want_half && !half.shaderFloat16) die("shaderFloat16 unavailable");
    if(want_byte && (!half.shaderInt8 || !byte_storage.storageBuffer8BitAccess))
      die("shaderInt8 or storageBuffer8BitAccess unavailable");
    half.shaderFloat16 = want_half;
    half.shaderInt8 = want_byte;
    byte_storage.uniformAndStorageBuffer8BitAccess = VK_FALSE;
    byte_storage.storagePushConstant8 = VK_FALSE;
  }
  const bool want_subgroup_types = getenv("M4_ORACLE_SUBGROUP_TYPES") &&
      !strcmp(getenv("M4_ORACLE_SUBGROUP_TYPES"), "1");
  void *feature_chain = (want_half || want_byte) ? &half : NULL;
  VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures subgroup = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};
  if (want_subgroup_types) {
    VkPhysicalDeviceFeatures2 query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &subgroup};
    vkGetPhysicalDeviceFeatures2(physical, &query);
    if (!subgroup.shaderSubgroupExtendedTypes)
      die("shaderSubgroupExtendedTypes unavailable");
    subgroup.pNext = feature_chain;
    feature_chain = &subgroup;
  }
  const char *extensions[4];
  unsigned extension_count = 0;
  if (want_half || want_byte)
    extensions[extension_count++] = VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME;
  if (want_byte)
    extensions[extension_count++] = VK_KHR_8BIT_STORAGE_EXTENSION_NAME;
  if (want_subgroup_types)
    extensions[extension_count++] = VK_KHR_SHADER_SUBGROUP_EXTENDED_TYPES_EXTENSION_NAME;
  if (sparse_alias) {
    VkPhysicalDeviceFeatures2 query = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    vkGetPhysicalDeviceFeatures2(physical, &query);
    if (!query.features.sparseBinding || !query.features.sparseResidencyAliased ||
        !(queue_flags & VK_QUEUE_SPARSE_BINDING_BIT))
      die("sparse aliasing unavailable");
  }
  const VkPhysicalDeviceFeatures features = {
      .sparseBinding = sparse_alias, .sparseResidencyAliased = sparse_alias,
      .shaderInt64 = VK_TRUE,
      .shaderStorageImageExtendedFormats = texel_storage};
  const VkDeviceCreateInfo device_info = {
      .pEnabledFeatures = &features,
      .pNext = feature_chain,
      .enabledExtensionCount = extension_count,
      .ppEnabledExtensionNames = extensions,
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
  LOAD_DEVICE(device, vkCreateBufferView);
  LOAD_DEVICE(device, vkDestroyBufferView);
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
  LOAD_DEVICE(device, vkCmdFillBuffer);
  LOAD_DEVICE(device, vkCmdCopyBuffer);
  LOAD_DEVICE(device, vkResetFences);
  LOAD_DEVICE(device, vkCmdPipelineBarrier);
  LOAD_DEVICE(device, vkEndCommandBuffer);
  LOAD_DEVICE(device, vkCreateFence);
  LOAD_DEVICE(device, vkDestroyFence);
  LOAD_DEVICE(device, vkQueueSubmit);
  LOAD_DEVICE(device, vkQueueBindSparse);
  LOAD_DEVICE(device, vkQueueWaitIdle);
  LOAD_DEVICE(device, vkWaitForFences);

  VkQueue queue;
  vkGetDeviceQueue(device, queue_family, 0, &queue);

  const VkDeviceSize buffer_size = element_count * sizeof(uint32_t);
  const VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = buffer_size,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               (texel_format_option ? (texel_storage ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) : 0),
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

  VkBuffer alias_buffer = VK_NULL_HANDLE;
  if (sparse_alias) {
    VkBufferCreateInfo alias_info = buffer_info;
    alias_info.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                       VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
    check(vkCreateBuffer(device, &alias_info, NULL, &alias_buffer), "create alias");
    VkMemoryRequirements alias_requirements;
    vkGetBufferMemoryRequirements(device, alias_buffer, &alias_requirements);
    if (alias_requirements.size > requirements.size ||
        !(alias_requirements.memoryTypeBits & (1u << memory_type)))
      die("alias backing requirements differ");
    VkSparseMemoryBind bind = {.size = alias_requirements.size, .memory = memory};
    VkSparseBufferMemoryBindInfo buffer_bind = {
      .buffer = alias_buffer, .bindCount = 1, .pBinds = &bind};
    VkBindSparseInfo info = {.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
      .bufferBindCount = 1, .pBufferBinds = &buffer_bind};
    check(vkQueueBindSparse(queue, 1, &info, VK_NULL_HANDLE), "bind alias");
    check(vkQueueWaitIdle(queue), "wait alias bind");
  }

  unsigned binding_count = 1;
  const char *binding_option = getenv("M4_ORACLE_BINDING_COUNT");
  if (binding_option) {
    char *end;
    unsigned long parsed = strtoul(binding_option, &end, 10);
    if (!*binding_option || *end || parsed < 1 || parsed > 16)
      die("binding count must be 1..16");
    binding_count = parsed;
  }
  if (sparse_alias && binding_count != 2) die("sparse alias needs two bindings");
  if (buffer_size % binding_count)
    die("buffer size must divide evenly into bindings");
  VkDeviceSize binding_stride = buffer_size / binding_count;
  if (texel_format_option && (binding_count != 2 ||
      texel_offset > binding_stride || texel_range > binding_stride - texel_offset))
    die("texel mode requires two bindings and a view inside the first half");
  if (binding_count > 1 && binding_stride % properties.limits.minStorageBufferOffsetAlignment)
    die("binding ranges must satisfy minStorageBufferOffsetAlignment");
  VkDescriptorSetLayoutBinding bindings[16];
  for (unsigned i = 0; i < binding_count; ++i)
    bindings[i] = (VkDescriptorSetLayoutBinding) {
      .binding = i, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
  VkDescriptorType texel_type = texel_storage ?
    VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  VkBufferView texel_view = VK_NULL_HANDLE;
  if (texel_format_option) {
    bindings[0].descriptorType = texel_type;
    VkBufferViewCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, .buffer = buffer,
      .format = texel_format, .offset = texel_offset, .range = texel_range};
    check(vkCreateBufferView(device, &vi, NULL, &texel_view), "vkCreateBufferView");
  }
  const VkDescriptorSetLayoutCreateInfo set_layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = binding_count, .pBindings = bindings,
  };
  VkDescriptorSetLayout set_layout;
  check(
      vkCreateDescriptorSetLayout(device, &set_layout_info, NULL, &set_layout),
      "vkCreateDescriptorSetLayout");

  const VkDescriptorPoolSize pool_size[2] = {{
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = binding_count,
  }, {texel_type, 1}};
  const VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = texel_format_option ? 2 : 1,
      .pPoolSizes = pool_size,
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
  VkDescriptorBufferInfo descriptor_buffers[16];
  VkWriteDescriptorSet descriptor_writes[16];
  for (unsigned i = 0; i < binding_count; ++i) {
    descriptor_buffers[i] = (VkDescriptorBufferInfo) {
        .buffer = (sparse_alias && i == 0) ? alias_buffer : buffer,
        .offset = i * binding_stride, .range = binding_stride,
    };
    descriptor_writes[i] = (VkWriteDescriptorSet) {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptor_set,
        .dstBinding = i, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &descriptor_buffers[i],
    };
  }
  if (texel_format_option) {
    descriptor_writes[0].descriptorType = texel_type;
    descriptor_writes[0].pBufferInfo = NULL;
    descriptor_writes[0].pTexelBufferView = &texel_view;
  }
  if (sparse_shader) {
    descriptor_buffers[1].offset = 0;
    descriptor_buffers[1].range = buffer_size;
  }
  vkUpdateDescriptorSets(device, binding_count, descriptor_writes, 0, NULL);

  VkShaderModule producer_shader = VK_NULL_HANDLE;
  VkPipeline producer_pipeline = VK_NULL_HANDLE;
  VkShaderModule shader = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (!transfer_option) {
    const VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_size,
        .pCode = spv,
    };
    check(vkCreateShaderModule(device, &shader_info, NULL, &shader),
          "vkCreateShaderModule");
    const VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
    };
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
    check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                   NULL, &pipeline),
          "vkCreateComputePipelines");
    if (sparse_shader) {
      size_t producer_size;
      void *producer_code = read_file(sparse_shader, &producer_size);
      VkShaderModuleCreateInfo producer_info = shader_info;
      producer_info.codeSize = producer_size;
      producer_info.pCode = producer_code;
      check(vkCreateShaderModule(device, &producer_info, NULL, &producer_shader),
            "create producer shader");
      free(producer_code);
      VkComputePipelineCreateInfo producer = pipeline_info;
      producer.stage.module = producer_shader;
      check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &producer, NULL,
                                     &producer_pipeline), "create producer pipeline");
    }
  }

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
      .flags = transfer_option ? 0 : VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  check(vkBeginCommandBuffer(command_buffer, &begin_info),
        "vkBeginCommandBuffer");
  if (!transfer_option) {
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
  }
  if (sparse_shader) {
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, producer_pipeline);
    vkCmdDispatch(command_buffer, groups, 1, 1);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  }
  if (sparse_fill) {
    if (!sparse_alias) die("sparse fill requires sparse alias");
    unsigned long value = strtoul(sparse_fill, &end, 0);
    if (*end || value > UINT32_MAX) die("invalid sparse fill");
    vkCmdFillBuffer(command_buffer, buffer, 0, buffer_size / 2, value);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
  }
  if (fill_option)
    vkCmdFillBuffer(command_buffer, buffer, 0, buffer_size, fill_value);
  else if (copy_option) {
    VkBufferCopy region = {.srcOffset=0, .dstOffset=buffer_size/2, .size=buffer_size/2};
    vkCmdCopyBuffer(command_buffer, buffer, buffer, 1, &region);
  } else
    vkCmdDispatch(command_buffer, groups, 1, 1);
  const VkBufferMemoryBarrier host_barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = transfer_option ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = buffer,
      .offset = 0,
      .size = buffer_size,
  };
  vkCmdPipelineBarrier(command_buffer, transfer_option ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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

  if (transfer_option) {
    /* In transfer mode groups-x is the bounded submission count. Compare the
     * first read after every fence; retries may not hide stale contents. */
    for (unsigned long iteration = 0; iteration < groups; iteration++) {
      for (size_t i = 0; i < element_count; i++) {
        if (((volatile uint32_t *)mapped)[i] != expected[i]) {
          fprintf(stderr, "FAIL: transfer submission %lu word %zu\n", iteration, i);
          die("transfer readback mismatch");
        }
      }
      if (iteration + 1 == groups) break;
      check(vkResetFences(device, 1, &fence), "vkResetFences");
      check(vkQueueSubmit(queue, 1, &submit_info, fence), "vkQueueSubmit(transfer)");
      check(vkWaitForFences(device, 1, &fence, true, UINT64_C(20000000000)),
            "vkWaitForFences(transfer)");
    }
    printf("Transfer %s submissions=%lu bytes=%zu value=%#x\n",copy_option?"copy":"fill",groups,input_size,fill_value);
  }

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
  vkDestroyPipeline(device, producer_pipeline, NULL);
  vkDestroyShaderModule(device, producer_shader, NULL);
  vkDestroyPipeline(device, pipeline, NULL);
  vkDestroyPipelineLayout(device, pipeline_layout, NULL);
  vkDestroyShaderModule(device, shader, NULL);
  vkDestroyDescriptorPool(device, descriptor_pool, NULL);
  vkDestroyDescriptorSetLayout(device, set_layout, NULL);
  if (texel_view) vkDestroyBufferView(device, texel_view, NULL);
  vkUnmapMemory(device, memory);
  if (alias_buffer) vkDestroyBuffer(device, alias_buffer, NULL);
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
