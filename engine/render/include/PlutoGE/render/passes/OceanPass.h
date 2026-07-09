#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <memory>

namespace PlutoGE::render
{
    class RenderTarget;
    class Shader;

    class OceanPass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Ocean"; }

    private:
        Shader *m_shader = nullptr;
        std::unique_ptr<RenderTarget> m_sceneColorCopy;
    };
}