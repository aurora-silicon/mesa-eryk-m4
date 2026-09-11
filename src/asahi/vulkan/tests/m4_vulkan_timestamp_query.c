/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Execute standalone M4 timestamp queries with no application draw/dispatch.
 * Run only through the mandatory J713 watchdog and thermal guard.
 */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define T8132_DEVICE_ID 0x8132
#define VK_FN(name) static PFN_##name name

VK_FN(vkGetInstanceProcAddr);
VK_FN(vkGetDeviceProcAddr);
VK_FN(vkCreateInstance);
VK_FN(vkDestroyInstance);
VK_FN(vkEnumeratePhysicalDevices);
VK_FN(vkGetPhysicalDeviceProperties);
VK_FN(vkGetPhysicalDeviceQueueFamilyProperties);
VK_FN(vkCreateDevice);
VK_FN(vkDestroyDevice);
VK_FN(vkCreateQueryPool);
VK_FN(vkDestroyQueryPool);
VK_FN(vkCreateCommandPool);
VK_FN(vkDestroyCommandPool);
VK_FN(vkAllocateCommandBuffers);
VK_FN(vkBeginCommandBuffer);
VK_FN(vkEndCommandBuffer);
VK_FN(vkCmdResetQueryPool);
VK_FN(vkResetQueryPool);
VK_FN(vkCmdWriteTimestamp);
VK_FN(vkGetDeviceQueue);
VK_FN(vkCreateFence);
VK_FN(vkDestroyFence);
VK_FN(vkQueueSubmit);
VK_FN(vkWaitForFences);
VK_FN(vkGetQueryPoolResults);

static void
die(const char *message)
{
   fprintf(stderr, "m4-timestamp-query: %s\n", message);
   exit(EXIT_FAILURE);
}

static void
check(VkResult result, const char *operation)
{
   if (result != VK_SUCCESS) {
      fprintf(stderr, "m4-timestamp-query: %s returned %d\n", operation,
              result);
      exit(EXIT_FAILURE);
   }
}

#define LOAD_GLOBAL(name)                                                      \
   do {                                                                        \
      name = (PFN_##name)vkGetInstanceProcAddr(VK_NULL_HANDLE, #name);         \
      if (!name)                                                               \
         die("loader is missing " #name);                                     \
   } while (0)

#define LOAD_INSTANCE(instance, name)                                          \
   do {                                                                        \
      name = (PFN_##name)vkGetInstanceProcAddr(instance, #name);               \
      if (!name)                                                               \
         die("instance is missing " #name);                                   \
   } while (0)

#define LOAD_DEVICE(device, name)                                              \
   do {                                                                        \
      name = (PFN_##name)vkGetDeviceProcAddr(device, #name);                   \
      if (!name)                                                               \
         die("device is missing " #name);                                     \
   } while (0)

int
main(void)
{
   const int host_reset = getenv("M4_TEST_HOST_RESET") != NULL;
   void *loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
   if (!loader)
      die(dlerror());

   vkGetInstanceProcAddr =
      (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
   if (!vkGetInstanceProcAddr)
      die("libvulkan.so.1 has no vkGetInstanceProcAddr");
   LOAD_GLOBAL(vkCreateInstance);

   const VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "m4-timestamp-query",
      .apiVersion = VK_API_VERSION_1_2,
   };
   const VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
   };
   VkInstance instance;
   check(vkCreateInstance(&instance_info, NULL, &instance),
         "vkCreateInstance");

   LOAD_INSTANCE(instance, vkGetDeviceProcAddr);
   LOAD_INSTANCE(instance, vkDestroyInstance);
   LOAD_INSTANCE(instance, vkEnumeratePhysicalDevices);
   LOAD_INSTANCE(instance, vkGetPhysicalDeviceProperties);
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
      die("physical-device allocation failed");
   check(vkEnumeratePhysicalDevices(instance, &physical_count,
                                    physical_devices),
         "vkEnumeratePhysicalDevices(list)");

   VkPhysicalDevice physical = VK_NULL_HANDLE;
   uint32_t queue_family = UINT32_MAX;
   for (uint32_t p = 0; p < physical_count && !physical; ++p) {
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(physical_devices[p], &properties);
      if (properties.deviceID != T8132_DEVICE_ID)
         continue;
      if (!properties.limits.timestampComputeAndGraphics ||
          properties.limits.timestampPeriod != 1.0f)
         die("T8132 timestamp properties differ from the bounded profile");

      uint32_t count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[p], &count,
                                                NULL);
      VkQueueFamilyProperties *queues = calloc(count, sizeof(*queues));
      if (!queues)
         die("queue-family allocation failed");
      vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[p], &count,
                                                queues);
      for (uint32_t q = 0; q < count; ++q) {
         if ((queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
             queues[q].timestampValidBits == 64) {
            physical = physical_devices[p];
            queue_family = q;
            break;
         }
      }
      free(queues);
   }
   free(physical_devices);
   if (!physical)
      die("no T8132/G16G graphics queue has 64 timestamp bits");

   const float priority = 1.0f;
   const VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = queue_family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
   };
   const VkPhysicalDeviceHostQueryResetFeatures host_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
      .hostQueryReset = VK_TRUE,
   };
   const VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = host_reset ? &host_features : NULL,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
   };
   VkDevice device;
   check(vkCreateDevice(physical, &device_info, NULL, &device),
         "vkCreateDevice");

   LOAD_DEVICE(device, vkDestroyDevice);
   LOAD_DEVICE(device, vkCreateQueryPool);
   LOAD_DEVICE(device, vkDestroyQueryPool);
   LOAD_DEVICE(device, vkCreateCommandPool);
   LOAD_DEVICE(device, vkDestroyCommandPool);
   LOAD_DEVICE(device, vkAllocateCommandBuffers);
   LOAD_DEVICE(device, vkBeginCommandBuffer);
   LOAD_DEVICE(device, vkEndCommandBuffer);
   LOAD_DEVICE(device, vkCmdResetQueryPool);
   if (host_reset)
      LOAD_DEVICE(device, vkResetQueryPool);
   LOAD_DEVICE(device, vkCmdWriteTimestamp);
   LOAD_DEVICE(device, vkGetDeviceQueue);
   LOAD_DEVICE(device, vkCreateFence);
   LOAD_DEVICE(device, vkDestroyFence);
   LOAD_DEVICE(device, vkQueueSubmit);
   LOAD_DEVICE(device, vkWaitForFences);
   LOAD_DEVICE(device, vkGetQueryPoolResults);

   const VkQueryPoolCreateInfo query_info = {
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_TIMESTAMP,
      .queryCount = 2,
   };
   VkQueryPool query_pool;
   check(vkCreateQueryPool(device, &query_info, NULL, &query_pool),
         "vkCreateQueryPool(timestamp)");

   const VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = queue_family,
   };
   VkCommandPool command_pool;
   check(vkCreateCommandPool(device, &pool_info, NULL, &command_pool),
         "vkCreateCommandPool");
   const VkCommandBufferAllocateInfo allocate_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer command_buffer;
   check(vkAllocateCommandBuffers(device, &allocate_info, &command_buffer),
         "vkAllocateCommandBuffers");
   const VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   check(vkBeginCommandBuffer(command_buffer, &begin_info),
         "vkBeginCommandBuffer");
   if (host_reset)
      vkResetQueryPool(device, query_pool, 0, 2);
   else
      vkCmdResetQueryPool(command_buffer, query_pool, 0, 2);
   printf("query reset source=%s\n", host_reset ? "host API" : "GPU command");
   vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       query_pool, 0);
   vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       query_pool, 1);
   check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

   if (getenv("M4_TEST_RECORD_ONLY")) {
      puts("PASS: timestamp commands recorded (not executed)");
      vkDestroyCommandPool(device, command_pool, NULL);
      vkDestroyQueryPool(device, query_pool, NULL);
      vkDestroyDevice(device, NULL);
      vkDestroyInstance(instance, NULL);
      dlclose(loader);
      return EXIT_SUCCESS;
   }

   VkQueue queue;
   vkGetDeviceQueue(device, queue_family, 0, &queue);
   VkFence fence;
   const VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   check(vkCreateFence(device, &fence_info, NULL, &fence), "vkCreateFence");
   const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1, .pCommandBuffers = &command_buffer};
   check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
   check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(20000000000)), "vkWaitForFences");
   uint64_t values[4] = {0};
   VkResult first_result = vkGetQueryPoolResults(device, query_pool, 0, 2, sizeof(values), values,
      2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
   printf("query_result=%d timestamps=%llu,%llu availability=%llu,%llu\n", first_result,
      (unsigned long long)values[0], (unsigned long long)values[2],
      (unsigned long long)values[1], (unsigned long long)values[3]);
   fflush(stdout);
   if (getenv("M4_TEST_SECOND_READ")) {
      const struct timespec delay = {.tv_nsec = 100000000};
      nanosleep(&delay, NULL);
      uint64_t second[4] = {0};
      VkResult r = vkGetQueryPoolResults(device, query_pool, 0, 2, sizeof(second), second,
         2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      printf("second_result=%d timestamps=%llu,%llu availability=%llu,%llu\n", r,
         (unsigned long long)second[0], (unsigned long long)second[2],
         (unsigned long long)second[1], (unsigned long long)second[3]);
      fflush(stdout);
   }
   check(first_result, "first vkGetQueryPoolResults");
   if (!values[0] || values[2] < values[0] || values[2] == UINT64_MAX ||
       !values[1] || !values[3])
      die("FAIL: standalone query timestamps or availability invalid");
   vkDestroyFence(device, fence, NULL);
   puts("PASS: standalone M4 Vulkan timestamps and availability");
   vkDestroyCommandPool(device, command_pool, NULL);
   vkDestroyQueryPool(device, query_pool, NULL);
   vkDestroyDevice(device, NULL);
   vkDestroyInstance(instance, NULL);
   dlclose(loader);
   return EXIT_SUCCESS;
}
