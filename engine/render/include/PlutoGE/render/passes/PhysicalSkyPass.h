#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    class Shader;

    class PhysicalSkyPass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Physical Sky"; }

    private:
        Shader *m_shader = nullptr;
        GLuint m_vao = 0;
    };
}
