#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

    class SceneCompositeEffect : public ShaderPostProcessEffect
    {
    public:
        SceneCompositeEffect() = default;
        ~SceneCompositeEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "SceneComposite"; }
        std::string GetDisplayName() const override { return "Scene Composite"; }

    private:
        Shader *m_shader = nullptr;
    };
}