#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

    enum class IndirectDebugView
    {
        None = 0,
        LpvOnly,
        SsgiOnly,
        CombinedIndirect,
    };

    class SceneCompositeEffect : public ShaderPostProcessEffect
    {
    public:
        SceneCompositeEffect() = default;
        ~SceneCompositeEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "SceneComposite"; }
        std::string GetDisplayName() const override { return "Scene Composite / Debug"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        bool IsLpvEnabled() const { return m_enableLpv; }
        bool IsSsgiEnabled() const { return m_enableSsgi; }
        IndirectDebugView GetIndirectDebugView() const { return m_indirectDebugView; }

    private:
        Shader *m_shader = nullptr;
        bool m_enableLpv = true;
        bool m_enableSsgi = true;
        IndirectDebugView m_indirectDebugView = IndirectDebugView::None;
    };
}