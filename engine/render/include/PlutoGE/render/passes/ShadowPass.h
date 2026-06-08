#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <cstddef>
#include <cstdint>

namespace PlutoGE::render
{
    class Renderer;
    class Shader;
    class ShadowPass : public IRenderPass
    {
    public:
        ShadowPass() = default;
        ~ShadowPass() = default;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Shadow"; }

    private:
        Shader *m_shadowPassShader = nullptr;
        unsigned int m_shadowFramebuffer = 0;
        unsigned int m_instanceBuffer = 0;
        std::size_t m_instanceCapacity = 0;
        std::uint64_t m_shadowCasterFingerprint = 0;
        bool m_hasShadowCasterFingerprint = false;
    };
}
