/* SPDX-License-Identifier: MIT
 * STK 1.5 macOS loader adapter, for the private comparison bundle only.
 * Re-export the standard Vulkan loader, which selects an explicit ICD.
 * STK requires the old MVK query at dlopen and reads only
 * baseVertexInstanceDrawing (ge_vulkan_features.cpp); a second use is debug-only.
 * Indexed vertexOffset and direct firstInstance are core Vulkan capabilities.
 * Optional indirect firstInstance remains queried through Vulkan by STK.
 * This is NOT a general implementation of the deprecated MVK interface.
 */
#include <MoltenVK/vk_mvk_moltenvk.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

static VkResult record_instance(const VkInstanceCreateInfo *info,
                               const VkAllocationCallbacks *alloc, VkInstance *instance)
{
    VkInstanceCreateInfo copy = *info;
    const char *extensions[64];
    VkExtensionProperties available[128];
    uint32_t count = 128;
    VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &count, available);
    int has_settings = 0;
    if (result != VK_SUCCESS || info->enabledExtensionCount > 64)
        return vkCreateInstance(info, alloc, instance);
    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(available[i].extensionName, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME)) has_settings = 1;
    copy.enabledExtensionCount = 0;
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++) {
        const char *name = info->ppEnabledExtensionNames[i];
        if (!has_settings && !strcmp(name, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME)) {
            const VkLayerSettingsCreateInfoEXT *settings = copy.pNext;
            if (!settings || settings->sType != VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT)
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            for (uint32_t j = 0; j < settings->settingCount; j++)
                if (strcmp(settings->pSettings[j].pLayerName, "MoltenVK"))
                    return VK_ERROR_EXTENSION_NOT_PRESENT;
            copy.pNext = settings->pNext;
            fprintf(stderr, "STK comparison adaptation: omit MoltenVK-only layer settings on ICD without VK_EXT_layer_settings\n");
            continue;
        }
        extensions[copy.enabledExtensionCount++] = name;
    }
    copy.ppEnabledExtensionNames = extensions;
    result = vkCreateInstance(&copy, alloc, instance);
    fprintf(stderr, "STK Vulkan instance: result=%d\n", result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo *info,
                            const VkAllocationCallbacks *alloc, VkImage *image)
{
    if (info->extent.width >= 1000 && (info->usage &
        (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))
        fprintf(stderr, "STK Vulkan image: %ux%u format=%u usage=%x\n",
                info->extent.width, info->extent.height, info->format, info->usage);
    void *handle = dlopen("/private/var/tmp/stk-comparison-20260921/libvulkan.1.4.357.dylib", RTLD_NOW | RTLD_LOCAL);
    PFN_vkCreateImage real = handle ? (PFN_vkCreateImage)dlsym(handle, "vkCreateImage") : NULL;
    VkResult result = real ? real(device, info, alloc, image) : VK_ERROR_INITIALIZATION_FAILED;
    if (handle) dlclose(handle);
    return result;
}
static VkResult record_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR *info,
                                const VkAllocationCallbacks *alloc, VkSwapchainKHR *swapchain)
{
    fprintf(stderr, "STK Vulkan swapchain: %ux%u present_mode=%u\n",
            info->imageExtent.width, info->imageExtent.height, info->presentMode);
    return vkCreateSwapchainKHR(device, info, alloc, swapchain);
}
static PFN_vkVoidFunction device_proc(VkDevice device, const char *name)
{
    PFN_vkVoidFunction fn = vkGetDeviceProcAddr(device, name);
    if (!fn) return NULL;
    if (!strcmp(name, "vkCreateImage")) return (PFN_vkVoidFunction)vkCreateImage;
    if (!strcmp(name, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)record_swapchain;
    return fn;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    void *handle = dlopen("/private/var/tmp/stk-comparison-20260921/libvulkan.1.4.357.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!handle) return NULL;
    PFN_vkGetInstanceProcAddr real = (PFN_vkGetInstanceProcAddr)dlsym(handle, "vkGetInstanceProcAddr");
    PFN_vkVoidFunction fn = real ? real(instance, name) : NULL;
    dlclose(handle);
    if (!fn) return NULL;
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)record_instance;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)device_proc;
    if (!strcmp(name, "vkCreateImage")) return (PFN_vkVoidFunction)vkCreateImage;
    if (!strcmp(name, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)record_swapchain;
    return fn;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceMetalFeaturesMVK(
    VkPhysicalDevice physical, MVKPhysicalDeviceMetalFeatures *features, size_t *size)
{
    MVKPhysicalDeviceMetalFeatures result = { .baseVertexInstanceDrawing = VK_TRUE };
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    fprintf(stderr, "STK comparison loader: %s; MVK compatibility query supplies only core base vertex/instance support\n", properties.deviceName);
    size_t n = *size < sizeof(result) ? *size : sizeof(result);
    if (features)
        memcpy(features, &result, n);
    *size = sizeof(result);
    return n < sizeof(result) ? VK_INCOMPLETE : VK_SUCCESS;
}
