/* SPDX-License-Identifier: MIT
 * First submission followed by an empty submission carrying a fence.
 * An already-signalled queue timeline point must not satisfy that fence.
 * Live M4 runs require the watchdog and thermal guards.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define CHECK(x)                                                               \
   do {                                                                        \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #x);                 \
         exit(1);                                                              \
      }                                                                        \
   } while (0)
#define VK(x)   CHECK((x) == VK_SUCCESS)
#define WORDS   (1024 * 1024)
#define PATTERN UINT32_C(0x7b2da916)

static void
cpu_barrier(void)
{
#ifdef __aarch64__
   if (getenv("M4_TEST_CPU_BARRIER") &&
       !strcmp(getenv("M4_TEST_CPU_BARRIER"), "oshld"))
      __asm__ volatile("dmb oshld" ::: "memory");
   else
      __asm__ volatile("dsb sy" ::: "memory");
#else
   __sync_synchronize();
#endif
}

static double
monotonic(void)
{
   struct timespec now;
   CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
   return now.tv_sec + now.tv_nsec / 1e9;
}

int
main(void)
{
   VkInstance instance;
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
   VK(vkCreateInstance(&ici, NULL, &instance));
   uint32_t count = 0;
   VK(vkEnumeratePhysicalDevices(instance, &count, NULL));
   CHECK(count == 1);
   VkPhysicalDevice physical;
   VK(vkEnumeratePhysicalDevices(instance, &count, &physical));
   VkPhysicalDeviceProperties properties;
   vkGetPhysicalDeviceProperties(physical, &properties);
   printf("Device: %s\n", properties.deviceName);
   fflush(stdout);
   CHECK(strstr(properties.deviceName, "Apple M4") ||
         (getenv("M4_TEST_LLVMPIPE") &&
          strstr(properties.deviceName, "llvmpipe")));
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
   VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
   CHECK(families);
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
   uint32_t family;
   for (family = 0; family < count; family++)
      if (families[family].queueCount &&
          (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
         break;
   CHECK(family < count);
   free(families);
   float priority = 1;
   VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = family,
      .queueCount = 1,
      .pQueuePriorities = &priority};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                             .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qci};
   VkDevice device;
   VK(vkCreateDevice(physical, &dci, NULL, &device));
   VkQueue queue;
   vkGetDeviceQueue(device, family, 0, &queue);
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                             .size = WORDS * sizeof(uint32_t),
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
   VkBuffer buffer;
   VK(vkCreateBuffer(device, &bci, NULL, &buffer));
   VkMemoryRequirements req;
   vkGetBufferMemoryRequirements(device, buffer, &req);
   VkPhysicalDeviceMemoryProperties memprops;
   vkGetPhysicalDeviceMemoryProperties(physical, &memprops);
   uint32_t memory_type;
   const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   for (memory_type = 0; memory_type < memprops.memoryTypeCount; memory_type++)
      if ((req.memoryTypeBits & (1u << memory_type)) &&
          (memprops.memoryTypes[memory_type].propertyFlags & wanted) ==
             wanted &&
          (!getenv("M4_TEST_UNCACHED") ||
           !(memprops.memoryTypes[memory_type].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_CACHED_BIT)))
         break;
   CHECK(memory_type < memprops.memoryTypeCount);
   printf("Memory type: %u flags=%#x\n", memory_type,
          memprops.memoryTypes[memory_type].propertyFlags);
   fflush(stdout);
   VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = req.size,
                               .memoryTypeIndex = memory_type};
   VkDeviceMemory memory;
   VK(vkAllocateMemory(device, &mai, NULL, &memory));
   VK(vkBindBufferMemory(device, buffer, memory, 0));
   uint32_t *data;
   VK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&data));
   memset(data, 0xa5, WORDS * sizeof(*data));
   if (getenv("M4_TEST_CLEAN_BEFORE")) {
#ifdef __aarch64__
      uint64_t ctr;
      __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
      const unsigned line_size = 4u << ((ctr >> 16) & 15);
      for (uintptr_t address = (uintptr_t)data;
           address < (uintptr_t)data + WORDS * sizeof(*data);
           address += line_size)
         __asm__ volatile("dc cvac, %0" : : "r"(address) : "memory");
      __asm__ volatile("dsb sy" ::: "memory");
#else
      CHECK(!"M4_TEST_CLEAN_BEFORE requires AArch64");
#endif
   }
   VkCommandPoolCreateInfo pci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = family};
   VkCommandPool pool;
   VK(vkCreateCommandPool(device, &pci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
   VkCommandBuffer command;
   VK(vkAllocateCommandBuffers(device, &cai, &command));
   VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   VK(vkBeginCommandBuffer(command, &begin));
   vkCmdFillBuffer(command, buffer, 0, VK_WHOLE_SIZE, PATTERN);
   VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0,
                        NULL);
   VK(vkEndCommandBuffer(command));
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   VK(vkCreateFence(device, &fci, NULL, &fence));
   VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                          .commandBufferCount = 1,
                          .pCommandBuffers = &command};
   if (getenv("M4_TEST_SUBMIT_BARRIER"))
      cpu_barrier();
   printf("TIME submit %.9f\n", monotonic());
   fflush(stdout);
   const bool direct_fence = getenv("M4_TEST_DIRECT_FENCE") != NULL;
   VK(vkQueueSubmit(queue, 1, &submit, direct_fence ? fence : VK_NULL_HANDLE));
   /* Deliberately signal via an empty submit, as Zink does. */
   VkSubmitInfo empty = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
   if (!direct_fence)
      VK(vkQueueSubmit(queue, 1, &empty, fence));
   VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(5000000000)));
   printf("TIME fence-return %.9f\n", monotonic());
   fflush(stdout);
   if (getenv("M4_TEST_READ_DELAY_MS")) {
      char *end;
      unsigned long ms = strtoul(getenv("M4_TEST_READ_DELAY_MS"), &end, 10);
      CHECK(!*end && ms <= 1000);
      struct timespec delay = {.tv_sec = ms / 1000,
                               .tv_nsec = (ms % 1000) * 1000000L};
      nanosleep(&delay, NULL);
   }
   if (getenv("M4_TEST_CPU_BARRIER"))
      cpu_barrier();
   unsigned bad = 0;
   for (unsigned i = 0; i < WORDS; i++) {
      if (data[i] != PATTERN) {
         if (bad < 3)
            fprintf(stderr, "word=%u got=%08x expected=%08x\n", i, data[i],
                    PATTERN);
         bad++;
      }
   }
   printf("TIME read-complete %.9f\n", monotonic());
   printf("first-submit bad=%u/%u\n", bad, WORDS);
   fflush(stdout);
   if (getenv("M4_TEST_SECOND_READ")) {
      struct timespec delay = {.tv_nsec = 100000000L};
      nanosleep(&delay, NULL);
      unsigned second_bad = 0;
      for (unsigned i = 0; i < WORDS; i++)
         second_bad += data[i] != PATTERN;
      printf("TIME second-read %.9f bad=%u/%u\n", monotonic(), second_bad,
             WORDS);
      fflush(stdout);
      bad += second_bad;
   }
   VK(vkDeviceWaitIdle(device));
   vkDestroyFence(device, fence, NULL);
   vkDestroyCommandPool(device, pool, NULL);
   vkUnmapMemory(device, memory);
   vkDestroyBuffer(device, buffer, NULL);
   vkFreeMemory(device, memory, NULL);
   vkDestroyDevice(device, NULL);
   vkDestroyInstance(instance, NULL);
   printf("%s: first-submit empty-fence readback\n", bad ? "FAIL" : "PASS");
   return bad != 0;
}
