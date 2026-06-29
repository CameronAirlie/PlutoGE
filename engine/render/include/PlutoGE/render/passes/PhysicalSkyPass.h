#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <glad/glad.h>
#include <cstdint>
#include <limits>

namespace PlutoGE::scene
{
    struct Light;
}

namespace PlutoGE::render
{
    class Shader;

    class PhysicalSkyPass : public IRenderPass
    {
    public:
        ~PhysicalSkyPass() override;
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Physical Sky"; }

        bool PrepareEnvironment(const RenderContext &ctx);
        GLuint GetEnvironmentTextureID() const { return m_environmentAvailable ? m_environmentTexture : 0; }
        int GetEnvironmentWidth() const { return m_environmentWidth; }
        int GetEnvironmentHeight() const { return m_environmentHeight; }
        float GetDirectionalLightVisibility(const scene::Light *light) const;

    private:
        Shader *m_shader = nullptr;
        GLuint m_vao = 0;
        GLuint m_environmentTexture = 0;
        GLuint m_environmentFramebuffer = 0;
        int m_environmentWidth = 512;
        int m_environmentHeight = 256;
        std::uint64_t m_lastEnvironmentFrame = std::numeric_limits<std::uint64_t>::max();
        const void *m_lastEnvironmentScene = nullptr;
        const scene::Light *m_environmentSun = nullptr;
        float m_environmentSunVisibility = 1.0f;
        bool m_environmentAvailable = false;
    };
}
