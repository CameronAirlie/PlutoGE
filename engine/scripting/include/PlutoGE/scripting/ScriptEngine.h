#pragma once

#include "PlutoGE/scripting/ScriptRuntime.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scripting
{
    struct ScriptBuildConfig
    {
        std::filesystem::path projectPath;
        std::filesystem::path outputDirectory;
        std::string configuration = "Debug";
        std::string framework;
    };

    struct ScriptBuildResult
    {
        bool succeeded = false;
        int exitCode = -1;
        std::string command;
    };

    class ScriptEngine
    {
    public:
        using ScriptFactory = std::function<std::unique_ptr<ScriptInstance>()>;

        void Initialize();
        void Shutdown();

        [[nodiscard]] ScriptBuildResult BuildProject(const ScriptBuildConfig &config) const;
        void SetRuntime(std::unique_ptr<IScriptRuntime> runtime);
        [[nodiscard]] bool LoadAssembly(const std::filesystem::path &assemblyPath);

        void RegisterManagedClass(const ScriptClassDefinition &scriptClass);
        void RegisterNativeClass(const ScriptClassDefinition &scriptClass, ScriptFactory factory);

        [[nodiscard]] bool HasClass(std::string_view fullName) const;
        [[nodiscard]] const ScriptClassDefinition *FindClass(std::string_view fullName) const;
        [[nodiscard]] std::vector<ScriptFieldDefinition> GetSerializedFields(std::string_view fullName) const;
        [[nodiscard]] std::unique_ptr<ScriptInstance> CreateInstance(std::string_view fullName) const;
        [[nodiscard]] const std::filesystem::path &GetAssemblyPath() const { return m_assemblyPath; }

    private:
        struct RegisteredScriptClass
        {
            ScriptClassDefinition definition;
            ScriptFactory nativeFactory;
            bool managed = false;
        };

        std::unordered_map<std::string, RegisteredScriptClass> m_classes;
        std::unique_ptr<IScriptRuntime> m_runtime;
        std::filesystem::path m_assemblyPath;
        bool m_initialized = false;
    };
}