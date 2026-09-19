/* SPDX-License-Identifier: MIT
 * Print the selected device's pipeline-cache UUID without submitting work.
 * cc -O2 vulkan-cache-key-probe.c -lvulkan -o vulkan-cache-key-probe
 */
#include <stdio.h>
#include <vulkan/vulkan.h>
int main(void) {
   VkInstance instance;
   VkApplicationInfo app = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .apiVersion=VK_API_VERSION_1_3};
   VkInstanceCreateInfo info = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo=&app};
   if (vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS) return 1;
   uint32_t count=1;
   VkPhysicalDevice device;
   if (vkEnumeratePhysicalDevices(instance,&count,&device) != VK_SUCCESS || !count) return 2;
   VkPhysicalDeviceProperties props;
   vkGetPhysicalDeviceProperties(device,&props);
   for (unsigned i=0;i<VK_UUID_SIZE;i++) printf("%02x",props.pipelineCacheUUID[i]);
   puts("");
   vkDestroyInstance(instance,NULL);
   return 0;
}
