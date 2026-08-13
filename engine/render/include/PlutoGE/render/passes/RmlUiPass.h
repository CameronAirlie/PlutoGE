#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <memory>

namespace PlutoGE::render { class Shader; }

namespace PlutoGE::render
{
    class RmlUiPass final : public IRenderPass
    {
    public:
        ~RmlUiPass() override;
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "RmlUi"; }

    private:
        void DrawWorldSurfaces(const RenderContext &ctx);
        std::unique_ptr<Shader> m_surfaceShader;
        unsigned int m_surfaceVao = 0;
    };
}
