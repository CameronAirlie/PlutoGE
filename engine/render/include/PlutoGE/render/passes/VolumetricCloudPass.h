#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <glad/glad.h>
#include <memory>

namespace PlutoGE::render
{
    class Shader;
    class RenderTarget;

    class VolumetricCloudPass : public IRenderPass
    {
    public:
        ~VolumetricCloudPass() override;
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Volumetric Clouds"; }

    private:
        Shader *m_shader = nullptr;
        Shader *m_compositeShader = nullptr;
        std::unique_ptr<RenderTarget> m_cloudTarget;
        GLuint m_vao = 0;

        bool EnsureCloudTarget(int width, int height);
    };
}
