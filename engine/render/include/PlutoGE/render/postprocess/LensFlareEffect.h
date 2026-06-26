#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <string>

namespace PlutoGE::render
{
    class Shader;
    class Texture;

    class LensFlareEffect : public ShaderPostProcessEffect
    {
    public:
        LensFlareEffect() = default;
        ~LensFlareEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "LensFlare"; }
        std::string GetDisplayName() const override { return "Lens Flare"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

    private:
        Texture *ResolveFlareTexture();

        Shader *m_shader = nullptr;
        Texture *m_flareTexture = nullptr;
        std::string m_texturePath;
        std::string m_loadedTexturePath;
        float m_intensity = 0.35f;
        float m_threshold = 1.0f;
        float m_scale = 1.0f;
        float m_ghostDispersal = 0.55f;
    };
}
