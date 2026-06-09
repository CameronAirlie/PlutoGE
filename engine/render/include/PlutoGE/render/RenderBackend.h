#pragma once

#include <string_view>

namespace PlutoGE::render
{
    enum class RenderBackend
    {
        OpenGL,
        NvrhiD3D12,
        NvrhiVulkan,
    };

    constexpr std::string_view ToString(RenderBackend backend)
    {
        switch (backend)
        {
        case RenderBackend::OpenGL:
            return "OpenGL";
        case RenderBackend::NvrhiD3D12:
            return "NVRHI D3D12";
        case RenderBackend::NvrhiVulkan:
            return "NVRHI Vulkan";
        }

        return "Unknown";
    }

    constexpr bool IsNvrhiBackend(RenderBackend backend)
    {
        return backend == RenderBackend::NvrhiD3D12 || backend == RenderBackend::NvrhiVulkan;
    }
}
