#include "PlutoGE/scripting/ScriptEngine.h"

#include "PlutoGE/scripting/HostFxrScriptRuntime.h"

#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <utility>

namespace PlutoGE::scripting
{
    namespace
    {
        std::string QuoteArgument(const std::filesystem::path &path)
        {
            return '"' + path.string() + '"';
        }

        class NullScriptRuntime final : public IScriptRuntime
        {
        public:
            bool LoadAssembly(const std::filesystem::path &assemblyPath) override
            {
                m_loadedAssembly = assemblyPath;
                return false;
            }

            [[nodiscard]] bool IsLoaded() const override
            {
                return false;
            }

            [[nodiscard]] std::vector<ScriptClassDefinition> GetScriptClasses() const override
            {
                return {};
            }

            [[nodiscard]] std::unique_ptr<ScriptInstance> CreateInstance(const ScriptClassDefinition &scriptClass) const override
            {
                (void)scriptClass;
                return nullptr;
            }

        private:
            std::filesystem::path m_loadedAssembly;
        };

    }

    ScriptBuildResult ScriptEngine::BuildProject(const ScriptBuildConfig &config) const
    {
        ScriptBuildResult result;
        if (config.projectPath.empty())
        {
            return result;
        }

        std::ostringstream command;
        command << "dotnet build " << QuoteArgument(config.projectPath)
                << " -c " << config.configuration;

        if (!config.outputDirectory.empty())
        {
            command << " -o " << QuoteArgument(config.outputDirectory);
        }

        if (!config.framework.empty())
        {
            command << " -f " << config.framework;
        }

        result.command = command.str();
        result.exitCode = std::system(result.command.c_str());
        result.succeeded = result.exitCode == 0;
        return result;
    }

    bool IsFieldValueCompatible(ScriptFieldType type, const ScriptFieldValue &value)
    {
        switch (type)
        {
        case ScriptFieldType::Boolean:
            return std::holds_alternative<bool>(value);
        case ScriptFieldType::Int32:
            return std::holds_alternative<int32_t>(value);
        case ScriptFieldType::Float:
            return std::holds_alternative<float>(value);
        case ScriptFieldType::Double:
            return std::holds_alternative<double>(value);
        case ScriptFieldType::String:
            return std::holds_alternative<std::string>(value);
        case ScriptFieldType::Vector2:
            return std::holds_alternative<glm::vec2>(value);
        case ScriptFieldType::Vector3:
            return std::holds_alternative<glm::vec3>(value);
        case ScriptFieldType::EntityId:
            return std::holds_alternative<uint32_t>(value);
        case ScriptFieldType::None:
        default:
            return std::holds_alternative<std::monostate>(value);
        }
    }

    ScriptFieldValue MakeDefaultFieldValue(ScriptFieldType type)
    {
        switch (type)
        {
        case ScriptFieldType::Boolean:
            return false;
        case ScriptFieldType::Int32:
            return int32_t{0};
        case ScriptFieldType::Float:
            return 0.0f;
        case ScriptFieldType::Double:
            return 0.0;
        case ScriptFieldType::String:
            return std::string{};
        case ScriptFieldType::Vector2:
            return glm::vec2{0.0f, 0.0f};
        case ScriptFieldType::Vector3:
            return glm::vec3{0.0f, 0.0f, 0.0f};
        case ScriptFieldType::EntityId:
            return uint32_t{0};
        case ScriptFieldType::None:
        default:
            return std::monostate{};
        }
    }

    const ScriptFieldDefinition *FindFieldDefinition(const ScriptClassDefinition &scriptClass, std::string_view fieldName)
    {
        for (const auto &field : scriptClass.fields)
        {
            if (field.name == fieldName)
            {
                return &field;
            }
        }

        return nullptr;
    }

    std::optional<ScriptFieldValue> ScriptInstance::GetFieldValue(std::string_view fieldName) const
    {
        const auto iterator = m_fieldValues.find(std::string(fieldName));
        if (iterator == m_fieldValues.end())
        {
            return std::nullopt;
        }

        return iterator->second;
    }

    void ScriptEngine::Initialize()
    {
        if (m_initialized)
        {
            return;
        }

        if (!m_runtime)
        {
            m_runtime = std::make_unique<HostFxrScriptRuntime>();
        }

        m_initialized = true;
    }

    void ScriptEngine::Shutdown()
    {
        for (auto iterator = m_classes.begin(); iterator != m_classes.end();)
        {
            if (iterator->second.managed)
            {
                iterator = m_classes.erase(iterator);
                continue;
            }

            ++iterator;
        }

        m_assemblyPath.clear();
        m_initialized = false;
    }

    void ScriptEngine::SetRuntime(std::unique_ptr<IScriptRuntime> runtime)
    {
        m_runtime = std::move(runtime);
    }

    bool ScriptEngine::LoadAssembly(const std::filesystem::path &assemblyPath)
    {
        if (!m_runtime)
        {
            return false;
        }

        if (!m_runtime->LoadAssembly(assemblyPath))
        {
            return false;
        }

        m_assemblyPath = assemblyPath;
        for (auto iterator = m_classes.begin(); iterator != m_classes.end();)
        {
            if (iterator->second.managed)
            {
                iterator = m_classes.erase(iterator);
                continue;
            }

            ++iterator;
        }

        for (const auto &scriptClass : m_runtime->GetScriptClasses())
        {
            RegisterManagedClass(scriptClass);
        }

        return true;
    }

    void ScriptEngine::RegisterManagedClass(const ScriptClassDefinition &scriptClass)
    {
        m_classes[scriptClass.GetFullName()] = RegisteredScriptClass{scriptClass, {}, true};
    }

    void ScriptEngine::RegisterNativeClass(const ScriptClassDefinition &scriptClass, ScriptFactory factory)
    {
        m_classes[scriptClass.GetFullName()] = RegisteredScriptClass{scriptClass, std::move(factory), false};
    }

    bool ScriptEngine::HasClass(std::string_view fullName) const
    {
        return m_classes.contains(std::string(fullName));
    }

    const ScriptClassDefinition *ScriptEngine::FindClass(std::string_view fullName) const
    {
        const auto iterator = m_classes.find(std::string(fullName));
        if (iterator == m_classes.end())
        {
            return nullptr;
        }

        return &iterator->second.definition;
    }

    std::vector<ScriptFieldDefinition> ScriptEngine::GetSerializedFields(std::string_view fullName) const
    {
        const auto *scriptClass = FindClass(fullName);
        if (!scriptClass)
        {
            return {};
        }

        std::vector<ScriptFieldDefinition> fields;
        fields.reserve(scriptClass->fields.size());

        for (const auto &field : scriptClass->fields)
        {
            if (field.serialized)
            {
                fields.push_back(field);
            }
        }

        return fields;
    }

    std::unique_ptr<ScriptInstance> ScriptEngine::CreateInstance(std::string_view fullName) const
    {
        const auto iterator = m_classes.find(std::string(fullName));
        if (iterator == m_classes.end())
        {
            return nullptr;
        }

        if (iterator->second.nativeFactory)
        {
            return iterator->second.nativeFactory();
        }

        if (!m_runtime || !m_runtime->IsLoaded())
        {
            return nullptr;
        }

        return m_runtime->CreateInstance(iterator->second.definition);
    }
}