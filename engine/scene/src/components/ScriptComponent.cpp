#include "PlutoGE/scene/components/ScriptComponent.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptRuntime.h"

namespace PlutoGE::scene
{
    ScriptComponent::ScriptComponent(const ScriptComponentConfig &config)
        : m_scriptClass(config.scriptClass), m_fieldValues(config.fieldValues)
    {
    }

    void ScriptComponent::Update(float deltaTime)
    {
        if (m_scriptClass.empty())
        {
            return;
        }

        EnsureInstance();
        if (!m_instance)
        {
            return;
        }

        if (!m_started)
        {
            m_instance->OnCreate();
            m_started = true;
        }

        m_instance->OnUpdate(deltaTime);
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
        const auto iterator = m_fieldValues.find(fieldName);
        if (iterator == m_fieldValues.end())
        {
            return std::nullopt;
        }

        return iterator->second;
    }

    std::vector<scripting::ScriptFieldDefinition> ScriptComponent::GetSerializedFields() const
    {
        return core::Engine::GetInstance().GetScriptEngine().GetSerializedFields(m_scriptClass);
    }

    void ScriptComponent::Reload()
    {
        m_instance.reset();
        m_started = false;
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