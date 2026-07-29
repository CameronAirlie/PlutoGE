#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <string>

namespace PlutoGE::render
{
    class Material;
}

namespace PlutoGE::scene
{
    struct DecalComponentConfig
    {
        std::string materialAssetReference;
        glm::vec4 tint{1.0f};
        float normalCutoff = 0.25f;
        float lifetime = 0.0f;
        float fadeDuration = 0.0f;
    };

    class DecalComponent final : public TypedComponent<DecalComponent>
    {
    public:
        explicit DecalComponent(const DecalComponentConfig &config = {});

        void Initialize() override;
        void Update(float deltaTime) override;
        void SubmitRenderCommand() const;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetMaterialAssetReference(std::string assetReference);
        const std::string &GetMaterialAssetReference() const { return m_materialAssetReference; }
        void SetTint(const glm::vec4 &tint) { m_tint = tint; }
        const glm::vec4 &GetTint() const { return m_tint; }
        void SetNormalCutoff(float cutoff);
        float GetNormalCutoff() const { return m_normalCutoff; }
        void SetLifetime(float lifetime, float fadeDuration = 0.0f);
        float GetLifetime() const { return m_lifetime; }
        float GetFadeDuration() const { return m_fadeDuration; }

    private:
        void ResolveMaterial();

        std::string m_materialAssetReference;
        render::Material *m_material = nullptr;
        glm::vec4 m_tint{1.0f};
        float m_normalCutoff = 0.25f;
        float m_lifetime = 0.0f;
        float m_fadeDuration = 0.0f;
        float m_age = 0.0f;
    };
}
