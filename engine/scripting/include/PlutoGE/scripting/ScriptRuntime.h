#pragma once

#include "PlutoGE/scripting/ScriptTypes.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::scripting
{
    class ScriptInstance
    {
    public:
        virtual ~ScriptInstance() = default;

        virtual void OnCreate() {}
        virtual void OnUpdate(float deltaTime) {}

        virtual void ApplyFieldValues(const std::unordered_map<std::string, ScriptFieldValue> &fieldValues)
        {
            m_fieldValues = fieldValues;
        }

        [[nodiscard]] virtual std::optional<ScriptFieldValue> GetFieldValue(std::string_view fieldName) const;

        void SetOwner(scene::Entity *owner)
        {
            m_owner = owner;
            OnOwnerAssigned(owner);
        }

        [[nodiscard]] scene::Entity *GetOwner() const { return m_owner; }

    protected:
        [[nodiscard]] const std::unordered_map<std::string, ScriptFieldValue> &GetFieldValues() const { return m_fieldValues; }
        virtual void OnOwnerAssigned(scene::Entity *owner)
        {
            (void)owner;
        }

    private:
        scene::Entity *m_owner = nullptr;
        std::unordered_map<std::string, ScriptFieldValue> m_fieldValues;
    };

    class IScriptRuntime
    {
    public:
        virtual ~IScriptRuntime() = default;

        virtual bool LoadAssembly(const std::filesystem::path &assemblyPath) = 0;
        [[nodiscard]] virtual bool IsLoaded() const = 0;
        [[nodiscard]] virtual std::vector<ScriptClassDefinition> GetScriptClasses() const = 0;
        [[nodiscard]] virtual std::unique_ptr<ScriptInstance> CreateInstance(const ScriptClassDefinition &scriptClass) const = 0;
    };
}