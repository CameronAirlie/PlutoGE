#include "PlutoGE/scene/components/ScriptComponent.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <unordered_set>

namespace PlutoGE::scene
{
    namespace
    {
        scene::PropertyType ToPropertyType(scripting::ScriptFieldType fieldType)
        {
            switch (fieldType)
            {
            case scripting::ScriptFieldType::Boolean:
                return scene::PropertyType::Bool;
            case scripting::ScriptFieldType::Int32:
                return scene::PropertyType::Int;
            case scripting::ScriptFieldType::Float:
                return scene::PropertyType::Float;
            case scripting::ScriptFieldType::Double:
                return scene::PropertyType::Double;
            case scripting::ScriptFieldType::String:
            case scripting::ScriptFieldType::PrefabAsset:
            case scripting::ScriptFieldType::ScriptableObjectAsset:
                return scene::PropertyType::String;
            case scripting::ScriptFieldType::Vector2:
                return scene::PropertyType::Vec2;
            case scripting::ScriptFieldType::Vector3:
                return scene::PropertyType::Vec3;
            case scripting::ScriptFieldType::EntityId:
            case scripting::ScriptFieldType::GameObject:
            case scripting::ScriptFieldType::MeshComponent:
            case scripting::ScriptFieldType::CameraComponent:
            case scripting::ScriptFieldType::LightComponent:
            case scripting::ScriptFieldType::RigidbodyComponent:
            case scripting::ScriptFieldType::ColliderComponent:
            case scripting::ScriptFieldType::AnimationComponent:
            case scripting::ScriptFieldType::CanvasComponent:
            case scripting::ScriptFieldType::RectTransformComponent:
            case scripting::ScriptFieldType::UIImageComponent:
            case scripting::ScriptFieldType::UITextComponent:
            case scripting::ScriptFieldType::UIButtonComponent:
            case scripting::ScriptFieldType::ParticleSystemComponent:
            case scripting::ScriptFieldType::SoundEmitterComponent:
                return scene::PropertyType::Entity;
            case scripting::ScriptFieldType::None:
            default:
                return scene::PropertyType::String;
            }
        }

        scripting::ScriptFieldType GetScriptFieldType(const scripting::ScriptFieldValue &value)
        {
            if (std::holds_alternative<bool>(value))
            {
                return scripting::ScriptFieldType::Boolean;
            }

            if (std::holds_alternative<int32_t>(value))
            {
                return scripting::ScriptFieldType::Int32;
            }

            if (std::holds_alternative<float>(value))
            {
                return scripting::ScriptFieldType::Float;
            }

            if (std::holds_alternative<double>(value))
            {
                return scripting::ScriptFieldType::Double;
            }

            if (std::holds_alternative<std::string>(value))
            {
                return scripting::ScriptFieldType::String;
            }

            if (std::holds_alternative<glm::vec2>(value))
            {
                return scripting::ScriptFieldType::Vector2;
            }

            if (std::holds_alternative<glm::vec3>(value))
            {
                return scripting::ScriptFieldType::Vector3;
            }

            if (std::holds_alternative<uint32_t>(value))
            {
                return scripting::ScriptFieldType::EntityId;
            }

            return scripting::ScriptFieldType::None;
        }

        std::string SerializeFieldValue(const scripting::ScriptFieldValue &value)
        {
            return std::visit(
                [](const auto &typedValue) -> std::string
                {
                    using ValueType = std::decay_t<decltype(typedValue)>;

                    if constexpr (std::is_same_v<ValueType, std::monostate>)
                    {
                        return {};
                    }
                    else if constexpr (std::is_same_v<ValueType, bool>)
                    {
                        return typedValue ? "true" : "false";
                    }
                    else if constexpr (std::is_same_v<ValueType, int32_t> || std::is_same_v<ValueType, uint32_t>)
                    {
                        return std::to_string(typedValue);
                    }
                    else if constexpr (std::is_same_v<ValueType, float> || std::is_same_v<ValueType, double>)
                    {
                        return std::to_string(typedValue);
                    }
                    else if constexpr (std::is_same_v<ValueType, std::string>)
                    {
                        return typedValue;
                    }
                    else if constexpr (std::is_same_v<ValueType, glm::vec2>)
                    {
                        return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y);
                    }
                    else if constexpr (std::is_same_v<ValueType, glm::vec3>)
                    {
                        return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y) + "," + std::to_string(typedValue.z);
                    }
                    else
                    {
                        return {};
                    }
                },
                value);
        }

        glm::vec2 ParseVec2(const std::string &value)
        {
            glm::vec2 parsedValue{0.0f};
#ifdef _WIN32
            sscanf_s(value.c_str(), "%f,%f", &parsedValue.x, &parsedValue.y);
#else
            std::sscanf(value.c_str(), "%f,%f", &parsedValue.x, &parsedValue.y);
#endif
            return parsedValue;
        }

        glm::vec3 ParseVec3(const std::string &value)
        {
            glm::vec3 parsedValue{0.0f};
#ifdef _WIN32
            sscanf_s(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
#else
            std::sscanf(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
#endif
            return parsedValue;
        }

        scripting::ScriptFieldValue DeserializeFieldValue(scene::PropertyType propertyType, const std::string &value)
        {
            switch (propertyType)
            {
            case scene::PropertyType::Bool:
                return value == "true";
            case scene::PropertyType::Int:
                return static_cast<int32_t>(std::stoi(value));
            case scene::PropertyType::Float:
                return std::stof(value);
            case scene::PropertyType::Double:
                return std::stod(value);
            case scene::PropertyType::String:
                return value;
            case scene::PropertyType::Vec2:
                return ParseVec2(value);
            case scene::PropertyType::Vec3:
                return ParseVec3(value);
            case scene::PropertyType::Entity:
                return static_cast<uint32_t>(std::stoul(value));
            case scene::PropertyType::Color:
            case scene::PropertyType::Enum:
            default:
                return value;
            }
        }
    }

    ScriptComponent::ScriptComponent(const ScriptComponentConfig &config)
        : m_scriptClass(config.source.empty() ? config.scriptClass : config.source), m_fieldValues(config.fieldValues)
    {
    }

    void ScriptComponent::Start()
    {
        if (m_started || m_scriptClass.empty())
        {
            return;
        }

        EnsureInstance();
        if (!m_instance)
        {
            return;
        }

        m_instance->OnCreate();
        m_started = true;
    }

    void ScriptComponent::Stop()
    {
        if (!m_instance && !m_started)
        {
            return;
        }

        m_instance.reset();
        m_started = false;
    }

    void ScriptComponent::Update(float deltaTime)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning())
        {
            return;
        }

        if (m_scriptClass.empty())
        {
            return;
        }

        Start();
        if (!m_instance)
        {
            return;
        }

        m_instance->OnUpdate(deltaTime);
    }

    void ScriptComponent::LateUpdate(float deltaTime)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning())
        {
            return;
        }

        if (m_scriptClass.empty())
        {
            return;
        }

        Start();
        if (!m_instance)
        {
            return;
        }

        m_instance->OnLateUpdate(deltaTime);
    }

    void ScriptComponent::OnCollisionEnter(uint32_t otherEntityId)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning() || m_scriptClass.empty())
        {
            return;
        }

        Start();
        if (m_instance)
        {
            m_instance->OnCollisionEnter(otherEntityId);
        }
    }

    void ScriptComponent::OnCollisionExit(uint32_t otherEntityId)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning() || m_scriptClass.empty())
        {
            return;
        }

        Start();
        if (m_instance)
        {
            m_instance->OnCollisionExit(otherEntityId);
        }
    }

    void ScriptComponent::OnAnimationEvent(const render::AnimationClip::Event &event)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning() || m_scriptClass.empty())
            return;
        Start();
        if (m_instance)
            m_instance->OnAnimationEvent(event.name, event.stringParameter, event.floatParameter, event.intParameter);
    }

    std::vector<Property> ScriptComponent::Serialize() const
    {
        std::vector<Property> properties;
        properties.push_back(Property{
            .name = "Source",
            .type = PropertyType::String,
            .value = m_scriptClass,
        });

        std::unordered_set<std::string> serializedFieldNames;
        const auto serializedFields = GetSerializedFields();
        properties.reserve(properties.size() + serializedFields.size() + m_fieldValues.size());

        for (const auto &field : serializedFields)
        {
            serializedFieldNames.insert(field.name);

            auto fieldValue = m_fieldValues.contains(field.name)
                                  ? m_fieldValues.at(field.name)
                                  : (scripting::IsFieldValueCompatible(field.type, field.defaultValue)
                                         ? field.defaultValue
                                         : scripting::MakeDefaultFieldValue(field.type));

            properties.push_back(Property{
                .name = field.name,
                .type = ToPropertyType(field.type),
                .value = SerializeFieldValue(fieldValue),
            });
        }

        for (const auto &[fieldName, fieldValue] : m_fieldValues)
        {
            if (serializedFieldNames.contains(fieldName))
            {
                continue;
            }

            properties.push_back(Property{
                .name = fieldName,
                .type = ToPropertyType(GetScriptFieldType(fieldValue)),
                .value = SerializeFieldValue(fieldValue),
            });
        }

        return properties;
    }

    void ScriptComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::unordered_map<std::string, scripting::ScriptFieldValue> fieldValues;
        std::string source = m_scriptClass;

        for (const auto &property : properties)
        {
            if (property.name == "Source")
            {
                source = property.value;
                continue;
            }

            fieldValues[property.name] = DeserializeFieldValue(property.type, property.value);
        }

        const bool sourceChanged = m_scriptClass != source;
        m_fieldValues = std::move(fieldValues);

        if (sourceChanged)
        {
            SetSource(source);
            return;
        }

        ApplySerializedFields();
    }

    void ScriptComponent::SetSource(const std::string &source)
    {
        SetScriptClass(source);
    }

    void ScriptComponent::SetScriptClass(const std::string &scriptClass)
    {
        if (m_scriptClass == scriptClass)
        {
            return;
        }

        m_scriptClass = scriptClass;
        Reload();
    }

    bool ScriptComponent::SetFieldValue(const std::string &fieldName, const scripting::ScriptFieldValue &value)
    {
        auto &scriptEngine = core::Engine::GetInstance().GetScriptEngine();
        if (const auto *scriptClass = scriptEngine.FindClass(m_scriptClass))
        {
            const auto *fieldDefinition = scripting::FindFieldDefinition(*scriptClass, fieldName);
            if (!fieldDefinition || !fieldDefinition->serialized || !scripting::IsFieldValueCompatible(fieldDefinition->type, value))
            {
                return false;
            }
        }

        m_fieldValues[fieldName] = value;
        ApplySerializedFields();
        return true;
    }

    std::optional<scripting::ScriptFieldValue> ScriptComponent::GetFieldValue(const std::string &fieldName) const
    {
        if (m_instance && core::Engine::GetInstance().IsRuntimeRunning())
        {
            if (const auto runtimeValue = m_instance->GetFieldValue(fieldName))
            {
                return runtimeValue;
            }
        }

        const auto iterator = m_fieldValues.find(fieldName);
        if (iterator == m_fieldValues.end())
        {
            return std::nullopt;
        }

        return iterator->second;
    }

    std::unordered_map<std::string, scripting::ScriptFieldValue> ScriptComponent::GetFieldValuesSnapshot() const
    {
        auto fieldValues = m_fieldValues;
        if (m_instance && core::Engine::GetInstance().IsRuntimeRunning())
        {
            for (auto &[fieldName, fieldValue] : m_instance->GetFieldValuesSnapshot())
            {
                fieldValues.insert_or_assign(std::move(fieldName), std::move(fieldValue));
            }
        }

        return fieldValues;
    }

    std::vector<scripting::ScriptFieldDefinition> ScriptComponent::GetSerializedFields() const
    {
        return core::Engine::GetInstance().GetScriptEngine().GetSerializedFields(m_scriptClass);
    }

    void ScriptComponent::RemapEntityReferences(const std::unordered_map<uint32_t, uint32_t> &entityIdRemap)
    {
        bool changed = false;
        for (auto &[fieldName, fieldValue] : m_fieldValues)
        {
            (void)fieldName;
            auto *entityId = std::get_if<uint32_t>(&fieldValue);
            if (!entityId)
            {
                continue;
            }

            const auto remappedEntity = entityIdRemap.find(*entityId);
            if (remappedEntity == entityIdRemap.end())
            {
                continue;
            }

            *entityId = remappedEntity->second;
            changed = true;
        }

        if (changed)
        {
            ApplySerializedFields();
        }
    }

    void ScriptComponent::Reload()
    {
        Stop();
    }

    void ScriptComponent::EnsureInstance()
    {
        if (m_instance || m_scriptClass.empty())
        {
            return;
        }

        m_instance = core::Engine::GetInstance().GetScriptEngine().CreateInstance(m_scriptClass);
        if (!m_instance)
        {
            return;
        }

        m_instance->SetOwner(GetOwner());
        ApplySerializedFields();
    }

    void ScriptComponent::ApplySerializedFields()
    {
        if (!m_instance)
        {
            return;
        }

        auto &scriptEngine = core::Engine::GetInstance().GetScriptEngine();
        if (const auto *scriptClass = scriptEngine.FindClass(m_scriptClass))
        {
            for (const auto &field : scriptClass->fields)
            {
                if (!field.serialized)
                {
                    continue;
                }

                if (!m_fieldValues.contains(field.name))
                {
                    m_fieldValues[field.name] = scripting::IsFieldValueCompatible(field.type, field.defaultValue)
                                                    ? field.defaultValue
                                                    : scripting::MakeDefaultFieldValue(field.type);
                }
            }
        }

        m_instance->ApplyFieldValues(m_fieldValues);
    }
}
