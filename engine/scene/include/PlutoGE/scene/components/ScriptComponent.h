#pragma once

#include "Component.h"
#include "PlutoGE/scripting/ScriptTypes.h"

#include <cstdint>
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
        std::string source;
        std::string scriptClass;
        std::unordered_map<std::string, scripting::ScriptFieldValue> fieldValues;
    };

    class ScriptComponent final : public TypedComponent<ScriptComponent>
    {
    public:
        explicit ScriptComponent(const ScriptComponentConfig &config = {});
        ~ScriptComponent() override = default;

        void Start();
        void Stop();
        void Update(float deltaTime) override;
        void OnCollisionEnter(uint32_t otherEntityId);
        void OnCollisionExit(uint32_t otherEntityId);
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetSource(const std::string &source);
        [[nodiscard]] const std::string &GetSource() const { return m_scriptClass; }
        void SetScriptClass(const std::string &scriptClass);
        [[nodiscard]] const std::string &GetScriptClass() const { return m_scriptClass; }

        [[nodiscard]] bool SetFieldValue(const std::string &fieldName, const scripting::ScriptFieldValue &value);
        [[nodiscard]] std::optional<scripting::ScriptFieldValue> GetFieldValue(const std::string &fieldName) const;
        [[nodiscard]] const std::unordered_map<std::string, scripting::ScriptFieldValue> &GetFieldValues() const { return m_fieldValues; }
        [[nodiscard]] std::unordered_map<std::string, scripting::ScriptFieldValue> GetFieldValuesSnapshot() const;
        [[nodiscard]] std::vector<scripting::ScriptFieldDefinition> GetSerializedFields() const;
        [[nodiscard]] bool IsStarted() const { return m_started; }

        void RemapEntityReferences(const std::unordered_map<uint32_t, uint32_t> &entityIdRemap);

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
