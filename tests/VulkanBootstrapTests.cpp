#include "PlutoGE/render/rhi/vulkan/VulkanBootstrap.h"

#include <iostream>

int main()
{
    const auto info = PlutoGE::render::rhi::vulkan::ProbeVulkanDevice();
    if (!info.available)
    {
        std::cerr << info.error << '\n';
        return 1;
    }
    if (info.deviceName.empty() || info.apiVersion == 0)
        return 2;
    std::cout << "Vulkan device: " << info.deviceName << '\n';
    return 0;
}
