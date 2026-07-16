#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <array>
#include <cstddef>
#include <memory>

namespace PlutoGE::render
{
    class Renderer;
    class Shader;
    class GeometryPassScratch;
    class GeometryPass : public IRenderPass
    {
    public:
        GeometryPass();
        ~GeometryPass() override;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Geometry"; }

    private:
        static constexpr std::size_t kStreamBufferCount = 3;
        Shader *m_geometryPassShader = nullptr;
        std::array<unsigned int, kStreamBufferCount> m_instanceBuffers{};
        std::array<std::size_t, kStreamBufferCount> m_instanceCapacities{};
        std::array<unsigned int, kStreamBufferCount> m_indirectBuffers{};
        std::array<std::size_t, kStreamBufferCount> m_indirectCapacities{};
        std::size_t m_streamBufferIndex = 0;
        bool m_indirectDrawEnabled = true;
        bool m_indirectDrawValidated = false;
        std::unique_ptr<GeometryPassScratch> m_scratch;
    };
}
