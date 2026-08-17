#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <vector>

namespace PlutoGE::render
{
    class Renderer;
    class Shader;
    class ShadowPassCache;
    class ShadowPass : public IRenderPass
    {
    public:
        ShadowPass();
        ~ShadowPass() override;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        [[nodiscard]] bool CanSkipStaticFrame(const RenderContext &ctx) const;
        const char *GetName() const override { return "Shadow"; }

    private:
        static constexpr std::size_t kStreamBufferCount = 8;

        [[nodiscard]] std::size_t AcquireStreamBuffer()
        {
            const std::size_t index = m_streamBufferIndex;
            m_streamBufferIndex = (m_streamBufferIndex + 1) % kStreamBufferCount;
            return index;
        }

        Shader *m_shadowPassShader = nullptr;
        unsigned int m_shadowFramebuffer = 0;
        std::array<unsigned int, kStreamBufferCount> m_instanceBuffers{};
        std::array<std::size_t, kStreamBufferCount> m_instanceCapacities{};
        std::array<unsigned int, kStreamBufferCount> m_indirectBuffers{};
        std::array<std::size_t, kStreamBufferCount> m_indirectCapacities{};
        std::size_t m_streamBufferIndex = 0;
        bool m_indirectDrawEnabled = true;
        bool m_indirectDrawValidated = false;
        std::uint64_t m_shadowCasterFingerprint = 0;
        std::vector<std::size_t> m_sortedShadowCasterCommandIndices;
        bool m_hasShadowCasterFingerprint = false;
        bool m_allCachedShadowCastersStatic = false;
        std::unique_ptr<ShadowPassCache> m_cache;
    };
}
