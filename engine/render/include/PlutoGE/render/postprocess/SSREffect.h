#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <memory>

namespace PlutoGE::render
{
    class RenderTarget;
    class Shader;

    class SSREffect : public ShaderPostProcessEffect
    {
    public:
        ~SSREffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "SSR"; }
        std::string GetDisplayName() const override { return "Screen Space Reflections"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        struct Settings
        {
            float intensity, maxRayDistance, thickness, startOffset, edgeFade;
            float fresnelPower, metallicBoost;
            int stepCount, binarySearchSteps;
        };
        [[nodiscard]] Settings GetSettings() const noexcept
        {
            return {m_intensity, m_maxRayDistance, m_thickness, m_startOffset,
                    m_edgeFade, m_fresnelPower, m_metallicBoost,
                    m_stepCount, m_binarySearchSteps};
        }

    private:
        void EnsureTraceTarget(int width, int height);

        Shader *m_shader = nullptr;
        Shader *m_compositeShader = nullptr;
        std::unique_ptr<RenderTarget> m_traceTarget;
        float m_intensity = 0.8f;
        float m_maxRayDistance = 30.0f;
        float m_thickness = 0.35f;
        float m_startOffset = 0.08f;
        float m_edgeFade = 0.12f;
        float m_fresnelPower = 5.0f;
        float m_metallicBoost = 0.75f;
        int m_stepCount = 48;
        int m_binarySearchSteps = 5;
    };
}
