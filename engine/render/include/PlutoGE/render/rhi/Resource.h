#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <utility>

namespace PlutoGE::render::rhi
{
    template <typename HandleType, void (IRenderDevice::*Destroy)(HandleType)>
    class UniqueResource
    {
    public:
        UniqueResource() = default;
        UniqueResource(IRenderDevice &device, HandleType handle) noexcept : m_device(&device), m_handle(handle) {}
        ~UniqueResource() { Reset(); }

        UniqueResource(const UniqueResource &) = delete;
        UniqueResource &operator=(const UniqueResource &) = delete;

        UniqueResource(UniqueResource &&other) noexcept
            : m_device(std::exchange(other.m_device, nullptr)),
              m_handle(std::exchange(other.m_handle, {}))
        {
        }

        UniqueResource &operator=(UniqueResource &&other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_device = std::exchange(other.m_device, nullptr);
                m_handle = std::exchange(other.m_handle, {});
            }
            return *this;
        }

        void Reset() noexcept
        {
            if (m_device && m_handle)
                (m_device->*Destroy)(m_handle);
            m_device = nullptr;
            m_handle = {};
        }

        [[nodiscard]] HandleType Get() const noexcept { return m_handle; }
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_handle); }

    private:
        IRenderDevice *m_device = nullptr;
        HandleType m_handle;
    };

    using Buffer = UniqueResource<BufferHandle, &IRenderDevice::DestroyBuffer>;
    using Texture = UniqueResource<TextureHandle, &IRenderDevice::DestroyTexture>;
    using Sampler = UniqueResource<SamplerHandle, &IRenderDevice::DestroySampler>;
    using GraphicsPipeline = UniqueResource<PipelineHandle, &IRenderDevice::DestroyPipeline>;
}
