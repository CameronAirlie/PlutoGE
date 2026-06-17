#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <cstddef>
#include <glad/glad.h>

namespace PlutoGE::render
{
    class Shader;

    class TransparentPass : public IRenderPass
    {
    public:
        TransparentPass() = default;
        ~TransparentPass() = default;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Transparent"; }

    private:
        Shader *m_transparentShader = nullptr;
        GLuint m_instanceBuffer = 0;
        std::size_t m_instanceCapacity = 0;
    };
}
