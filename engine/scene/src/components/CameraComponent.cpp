#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/LPVEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/render/postprocess/RSMEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <map>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr const char *kEffectPrefix = "PostProcessEffects.";
        constexpr const char *kEffectParamsPrefix = "Params.";

        PropertyType ToScenePropertyType(render::PostProcessParameterType parameterType)
        {
            switch (parameterType)
            {
            case render::PostProcessParameterType::Float:
                return PropertyType::Float;
            case render::PostProcessParameterType::Int:
                return PropertyType::Int;
            case render::PostProcessParameterType::Bool:
                return PropertyType::Bool;
            case render::PostProcessParameterType::Enum:
                return PropertyType::Enum;
            case render::PostProcessParameterType::String:
            default:
                return PropertyType::String;
            }
        }

        render::PostProcessParameterType ToPostProcessParameterType(PropertyType propertyType)
        {
            switch (propertyType)
            {
            case PropertyType::Float:
                return render::PostProcessParameterType::Float;
            case PropertyType::Int:
                return render::PostProcessParameterType::Int;
            case PropertyType::Bool:
                return render::PostProcessParameterType::Bool;
            case PropertyType::Enum:
                return render::PostProcessParameterType::Enum;
            case PropertyType::String:
            case PropertyType::Vec3:
            case PropertyType::Color:
            default:
                return render::PostProcessParameterType::String;
            }
        }

        struct SerializedEffectData
        {
            std::string typeName;
            bool enabled = true;
            std::vector<render::PostProcessParameter> parameters;
        };

        render::PostProcessParameter *FindParameter(std::vector<render::PostProcessParameter> &parameters, const std::string &name)
        {
            auto it = std::find_if(parameters.begin(), parameters.end(), [&name](const auto &parameter)
                                   { return parameter.name == name; });
            if (it == parameters.end())
            {
                return nullptr;
            }

            return &(*it);
        }
    }

    CameraComponent::CameraComponent(render::Camera *camera) : m_camera(camera)
    {
        EmplacePostProcessEffect<render::SSGIEffect>();
        EmplacePostProcessEffect<render::LPVEffect>();
        EmplacePostProcessEffect<render::RSMEffect>();
        EmplacePostProcessEffect<render::ToneMappingEffect>();
        EmplacePostProcessEffect<render::SceneCompositeEffect>();
        EmplacePostProcessEffect<render::GammaCorrectionEffect>(2.2f);
    }

    CameraComponent::~CameraComponent() = default;

    void CameraComponent::Update(float deltaTime)
    {
        if (m_camera)
        {
            auto entity = GetOwner();
            glm::mat4 transform = entity->GetWorldTransform();

            m_camera->GetCameraData(transform, 1, 1);
        }
    }

    render::CameraData CameraComponent::GetCameraData(int width, int height) const
    {
        if (m_camera)
        {
            auto entity = GetOwner();
            glm::mat4 transform = entity->GetWorldTransform();

            return m_camera->GetCameraData(transform, width, height);
        }
        return render::CameraData{}; // Return default camera data if no camera is set
    }

    void CameraComponent::AddPostProcessEffect(std::unique_ptr<render::IPostProcessEffect> effect)
    {
        if (!effect)
        {
            return;
        }

        effect->Initialize();
        m_postProcessEffects.push_back(std::move(effect));
    }

    bool CameraComponent::AddPostProcessEffectByType(std::string_view typeName)
    {
        auto effect = render::CreatePostProcessEffect(typeName);
        if (!effect)
        {
            return false;
        }

        AddPostProcessEffect(std::move(effect));
        return true;
    }

    void CameraComponent::ClearPostProcessEffects()
    {
        m_postProcessEffects.clear();
    }

    bool CameraComponent::RemovePostProcessEffect(size_t index)
    {
        if (index >= m_postProcessEffects.size())
        {
            return false;
        }

        m_postProcessEffects.erase(m_postProcessEffects.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    bool CameraComponent::MovePostProcessEffect(size_t fromIndex, size_t toIndex)
    {
        if (fromIndex >= m_postProcessEffects.size() || toIndex >= m_postProcessEffects.size() || fromIndex == toIndex)
        {
            return false;
        }

        auto effect = std::move(m_postProcessEffects[fromIndex]);
        m_postProcessEffects.erase(m_postProcessEffects.begin() + static_cast<std::ptrdiff_t>(fromIndex));
        m_postProcessEffects.insert(m_postProcessEffects.begin() + static_cast<std::ptrdiff_t>(toIndex), std::move(effect));
        return true;
    }

    render::IPostProcessEffect *CameraComponent::GetPostProcessEffect(size_t index)
    {
        if (index >= m_postProcessEffects.size())
        {
            return nullptr;
        }

        return m_postProcessEffects[index].get();
    }

    const render::IPostProcessEffect *CameraComponent::GetPostProcessEffect(size_t index) const
    {
        if (index >= m_postProcessEffects.size())
        {
            return nullptr;
        }

        return m_postProcessEffects[index].get();
    }

    std::vector<Property> CameraComponent::Serialize() const
    {
        std::vector<Property> properties;
        if (m_camera)
        {
            properties.push_back({"FOV", scene::PropertyType::Float, std::to_string(m_camera->GetFOV())});
            properties.push_back({"NearPlane", scene::PropertyType::Float, std::to_string(m_camera->GetNearPlane())});
            properties.push_back({"FarPlane", scene::PropertyType::Float, std::to_string(m_camera->GetFarPlane())});
        }

        properties.push_back({"PostProcessEffectCount", scene::PropertyType::Int, std::to_string(m_postProcessEffects.size())});

        for (size_t index = 0; index < m_postProcessEffects.size(); ++index)
        {
            const auto &effect = m_postProcessEffects[index];
            if (!effect)
            {
                continue;
            }

            const std::string effectPrefix = std::string(kEffectPrefix) + std::to_string(index) + ".";
            properties.push_back({effectPrefix + "Type", scene::PropertyType::String, effect->GetTypeName()});
            properties.push_back({effectPrefix + "Enabled", scene::PropertyType::Bool, effect->IsEnabled() ? "true" : "false"});

            for (const auto &parameter : effect->GetParameters())
            {
                properties.push_back({
                    effectPrefix + kEffectParamsPrefix + parameter.name,
                    ToScenePropertyType(parameter.type),
                    parameter.value,
                    parameter.enumOptions,
                });
            }
        }

        return properties;
    }

    void CameraComponent::Deserialize(const std::vector<Property> &properties)
    {
        if (!m_camera)
        {
            m_camera = std::make_unique<render::Camera>(render::CameraConfig{});
        }

        bool hasSerializedEffects = false;
        std::map<size_t, SerializedEffectData> serializedEffects;

        for (const auto &property : properties)
        {
            if (property.name == "FOV")
            {
                m_camera->SetFOV(std::stof(property.value));
            }
            else if (property.name == "NearPlane")
            {
                m_camera->SetNearPlane(std::stof(property.value));
            }
            else if (property.name == "FarPlane")
            {
                m_camera->SetFarPlane(std::stof(property.value));
            }
            else if (property.name == "PostProcessEffectCount")
            {
                hasSerializedEffects = true;
            }
            else if (property.name.rfind(kEffectPrefix, 0) == 0)
            {
                hasSerializedEffects = true;

                const std::string remainder = property.name.substr(std::char_traits<char>::length(kEffectPrefix));
                const size_t separatorIndex = remainder.find('.');
                if (separatorIndex == std::string::npos)
                {
                    continue;
                }

                const size_t effectIndex = static_cast<size_t>(std::stoul(remainder.substr(0, separatorIndex)));
                const std::string effectField = remainder.substr(separatorIndex + 1);
                auto &serializedEffect = serializedEffects[effectIndex];

                if (effectField == "Type")
                {
                    serializedEffect.typeName = property.value;
                }
                else if (effectField == "Enabled")
                {
                    serializedEffect.enabled = (property.value == "true");
                }
                else if (effectField.rfind(kEffectParamsPrefix, 0) == 0)
                {
                    const std::string parameterName = effectField.substr(std::char_traits<char>::length(kEffectParamsPrefix));
                    if (parameterName.empty())
                    {
                        continue;
                    }

                    auto *parameter = FindParameter(serializedEffect.parameters, parameterName);
                    if (!parameter)
                    {
                        serializedEffect.parameters.push_back(render::PostProcessParameter{
                            .name = parameterName,
                            .type = ToPostProcessParameterType(property.type),
                            .value = property.value,
                            .enumOptions = property.enumOptions,
                        });
                        continue;
                    }

                    parameter->type = ToPostProcessParameterType(property.type);
                    parameter->value = property.value;
                    parameter->enumOptions = property.enumOptions;
                }
            }
        }

        if (!hasSerializedEffects)
        {
            return;
        }

        ClearPostProcessEffects();
        for (auto &[index, serializedEffect] : serializedEffects)
        {
            auto effect = render::CreatePostProcessEffect(serializedEffect.typeName);
            if (!effect)
            {
                continue;
            }

            effect->SetEnabled(serializedEffect.enabled);
            effect->Initialize();
            effect->SetParameters(serializedEffect.parameters);
            m_postProcessEffects.push_back(std::move(effect));
        }
    }
}