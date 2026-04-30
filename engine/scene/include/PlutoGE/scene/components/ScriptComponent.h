#pragma once

#include "Component.h"
#include "PlutoGE/scripting/ScriptTypes.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scripting
{
    class ScriptInstance;
}

namespace PlutoGE::scene
{
    struct ScriptComponentConfig
    {
        std::string scriptClass;
        std::unordered_map<std::string, scripting::ScriptFieldValue> fieldValues;
    };

    class ScriptComponent final : public Component
    {
    public:
        explicit ScriptComponent(const ScriptComponentConfig &config = {});
        ~ScriptComponent() override = default;

        void Update(float deltaTime) override;

        void SetScriptClass(const std::string &scriptClass);
        [[nodiscard]] const std::string &GetScriptClass() const { return m_scriptClass; }

        [[nodiscard]] bool SetFieldValue(const std::string &fieldName, const scripting::ScriptFieldValue &value);
        [[nodiscard]] std::optional<scripting::ScriptFieldValue> GetFieldValue(const std::string &fieldName) const;
        [[nodiscard]] std::vector<scripting::ScriptFieldDefinition> GetSerializedFields() const;

        void Reload();

    private:
        void EnsureInstance();
        void ApplySerializedFields();

        std::string m_scriptClass;
        std::unordered_map<std::string, scripting::ScriptFieldValue> m_fieldValues;
        std::unique_ptr<scripting::ScriptInstance> m_instance;
        bool m_started = false;
    };
}