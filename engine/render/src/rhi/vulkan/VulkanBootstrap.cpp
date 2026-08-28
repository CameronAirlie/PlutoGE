#include "PlutoGE/render/rhi/vulkan/VulkanBootstrap.h"

#include <volk.h>

#include <cstring>
#include <vector>

namespace PlutoGE::render::rhi::vulkan
{
    VulkanDeviceInfo ProbeVulkanDevice()
    {
        VulkanDeviceInfo result;
        if (volkInitialize() != VK_SUCCESS)
        {
            result.error = "The Vulkan loader could not be initialized.";
            return result;
        }

        VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = "PlutoGE";
        applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        applicationInfo.pEngineName = "PlutoGE";
        applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &applicationInfo;
        VkInstance instance = VK_NULL_HANDLE;
        const VkResult instanceResult = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if (instanceResult != VK_SUCCESS)
        {
            result.error = "vkCreateInstance failed with code " + std::to_string(instanceResult) + ".";
            return result;
        }
        volkLoadInstance(instance);

        std::uint32_t deviceCount = 0;
        VkResult enumerateResult = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (enumerateResult != VK_SUCCESS || deviceCount == 0)
        {
            result.error = "No Vulkan physical device is available.";
            vkDestroyInstance(instance, nullptr);
            return result;
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        enumerateResult = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        if (enumerateResult != VK_SUCCESS)
        {
            result.error = "Vulkan physical-device enumeration failed.";
            vkDestroyInstance(instance, nullptr);
            return result;
        }

        VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
        std::uint32_t graphicsQueueFamily = 0;
        for (VkPhysicalDevice device : devices)
        {
            std::uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
            for (std::uint32_t family = 0; family < familyCount; ++family)
            {
                if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
                {
                    selectedDevice = device;
                    graphicsQueueFamily = family;
                    break;
                }
            }
            if (selectedDevice)
                break;
        }

        if (!selectedDevice)
        {
            result.error = "No Vulkan graphics queue is available.";
            vkDestroyInstance(instance, nullptr);
            return result;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(selectedDevice, &properties);
        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = graphicsQueueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        dynamicRendering.dynamicRendering = VK_TRUE;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = &dynamicRendering;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        VkDevice logicalDevice = VK_NULL_HANDLE;
        const VkResult deviceResult = vkCreateDevice(selectedDevice, &deviceInfo, nullptr, &logicalDevice);
        if (deviceResult != VK_SUCCESS)
        {
            result.error = "vkCreateDevice failed with code " + std::to_string(deviceResult) + ".";
            vkDestroyInstance(instance, nullptr);
            return result;
        }
        volkLoadDevice(logicalDevice);

        result.available = true;
        result.deviceName = properties.deviceName;
        result.apiVersion = properties.apiVersion;
        vkDestroyDevice(logicalDevice, nullptr);
        vkDestroyInstance(instance, nullptr);
        return result;
    }
}
