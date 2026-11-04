#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    // The values reach the RHI scene-composite pass as its debug mode, so they stay fixed.
    enum class IndirectDebugView
    {
        None = 0,
        SsgiOnly = 2,
        CombinedIndirect = 3,
    };

    class SceneCompositeEffect : public IPostProcessEffect
    {
    public:
        SceneCompositeEffect() = default;
        ~SceneCompositeEffect() override = default;

        std::string GetTypeName() const override { return "SceneComposite"; }
        std::string GetDisplayName() const override { return "Scene Composite / Debug"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        bool IsSsgiEnabled() const { return m_enableSsgi; }
        IndirectDebugView GetIndirectDebugView() const { return m_indirectDebugView; }

    private:
        bool m_enableSsgi = true;
        IndirectDebugView m_indirectDebugView = IndirectDebugView::None;
    };
}