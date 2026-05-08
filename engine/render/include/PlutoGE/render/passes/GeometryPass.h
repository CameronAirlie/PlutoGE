#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class Renderer;
    class Shader;
    class GeometryPass : public IRenderPass
    {
    public:
        GeometryPass() = default;
        ~GeometryPass() = default;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Geometry"; }

    private:
        Shader *m_geometryPassShader = nullptr;
    };
}