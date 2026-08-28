#pragma once

#include <cstdint>
#include <string>

namespace PlutoGE::render::rhi::vulkan
{
    struct VulkanDeviceInfo
    {
        bool available = false;
        std::string deviceName;
        std::uint32_t apiVersion = 0;
        std::string error;
    };

    // Performs the same instance, physical-device, queue-family, and logical-
    // device checks required by the renderer without exposing Vulkan handles.
    [[nodiscard]] VulkanDeviceInfo ProbeVulkanDevice();
}
