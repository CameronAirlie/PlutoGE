#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class Shader;

    class DecalPass final : public IRenderPass
    {
    public:
        ~DecalPass() override;
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Decals"; }

    private:
        Shader *m_shader = nullptr;
        unsigned int m_fullscreenVao = 0;
    };
}
