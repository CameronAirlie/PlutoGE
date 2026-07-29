#include "PlutoGE/scene/components/DecalComponent.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace PlutoGE::scene
{
    DecalComponent::DecalComponent(const DecalComponentConfig &config)
        : m_materialAssetReference(config.materialAssetReference),
          m_tint(config.tint),
          m_normalCutoff(std::clamp(config.normalCutoff, -1.0f, 1.0f)),
          m_lifetime(std::max(config.lifetime, 0.0f)),
          m_fadeDuration(std::max(config.fadeDuration, 0.0f))
    {
        ResolveMaterial();
    }

    void DecalComponent::Initialize()
    {
        ResolveMaterial();
    }

    void DecalComponent::ResolveMaterial()
    {
        m_material = m_materialAssetReference.empty()
                        ? nullptr
                        : core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(m_materialAssetReference);
    }

    void DecalComponent::SetMaterialAssetReference(std::string assetReference)
    {
        if (m_materialAssetReference == assetReference)
            return;
        m_materialAssetReference = std::move(assetReference);
        ResolveMaterial();
    }

    void DecalComponent::SetNormalCutoff(float cutoff)
    {
        m_normalCutoff = std::clamp(cutoff, -1.0f, 1.0f);
    }

    void DecalComponent::SetLifetime(float lifetime, float fadeDuration)
    {
        m_lifetime = std::max(lifetime, 0.0f);
        m_fadeDuration = std::clamp(fadeDuration, 0.0f, m_lifetime);
        m_age = 0.0f;
    }

    void DecalComponent::Update(float deltaTime)
    {
        if (m_lifetime <= 0.0f)
            return;
        m_age += std::max(deltaTime, 0.0f);
        if (m_age >= m_lifetime && GetOwner() && GetOwner()->GetScene())
            GetOwner()->GetScene()->DestroyEntity(GetOwner()->GetID());
    }

    void DecalComponent::SubmitRenderCommand() const
    {
        if (!m_material || !GetOwner())
            return;

        glm::vec4 tint = m_tint;
        const float remaining = m_lifetime - m_age;
        if (m_lifetime > 0.0f && m_fadeDuration > 0.0f && remaining < m_fadeDuration)
            tint.a *= std::clamp(remaining / m_fadeDuration, 0.0f, 1.0f);

        core::Engine::GetInstance().GetRenderer().SubmitDecalCommand(render::DecalCommand{
            .model = GetOwner()->GetWorldTransform(),
            .material = m_material,
            .tint = tint,
            .normalCutoff = m_normalCutoff,
        });
    }

    std::vector<Property> DecalComponent::Serialize() const
    {
        const std::string tint = std::to_string(m_tint.r) + "," + std::to_string(m_tint.g) + "," +
                                 std::to_string(m_tint.b) + "," + std::to_string(m_tint.a);
        return {
            {"MaterialAsset", PropertyType::String, m_materialAssetReference},
            {"Tint", PropertyType::Color, tint},
            {"Normal Cutoff", PropertyType::Float, std::to_string(m_normalCutoff)},
            {"Lifetime", PropertyType::Float, std::to_string(m_lifetime)},
            {"Fade Duration", PropertyType::Float, std::to_string(m_fadeDuration)},
        };
    }

    void DecalComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "MaterialAsset")
                m_materialAssetReference = property.value;
            else if (property.name == "Tint")
                std::sscanf(property.value.c_str(), "%f,%f,%f,%f", &m_tint.r, &m_tint.g, &m_tint.b, &m_tint.a);
            else if (property.name == "Normal Cutoff")
                m_normalCutoff = std::clamp(std::stof(property.value), -1.0f, 1.0f);
            else if (property.name == "Lifetime")
                m_lifetime = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Fade Duration")
                m_fadeDuration = std::max(std::stof(property.value), 0.0f);
        }
        m_fadeDuration = std::min(m_fadeDuration, m_lifetime);
        ResolveMaterial();
    }
}
