/* SPDX-License-Identifier: MIT
 * Query cache namespaces without creating a device or submitting GPU work.
 * Run in separate processes with normal/changed driconf or HK_PERFTEST values.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

int main(void)
{
   VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "hk-cache-options-probe",
      .pEngineName = "vkd3d",
      .apiVersion = VK_API_VERSION_1_3,
   };
   VkInstanceCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
   };
   VkInstance instance;
   assert(vkCreateInstance(&ci, NULL, &instance) == VK_SUCCESS);
   uint32_t count = 0;
   assert(vkEnumeratePhysicalDevices(instance, &count, NULL) == VK_SUCCESS);
   assert(count == 1);
   VkPhysicalDevice device;
   assert(vkEnumeratePhysicalDevices(instance, &count, &device) == VK_SUCCESS);
   VkPhysicalDeviceShaderObjectPropertiesEXT shader = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_PROPERTIES_EXT,
   };
   VkPhysicalDeviceProperties2 props = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &shader,
   };
   vkGetPhysicalDeviceProperties2(device, &props);
   printf("{\"pipeline\":\"");
   for (unsigned i = 0; i < VK_UUID_SIZE; i++)
      printf("%02x", props.properties.pipelineCacheUUID[i]);
   printf("\",\"shader\":\"");
   for (unsigned i = 0; i < VK_UUID_SIZE; i++)
      printf("%02x", shader.shaderBinaryUUID[i]);
   puts("\"}");
   vkDestroyInstance(instance, NULL);
   return 0;
}
