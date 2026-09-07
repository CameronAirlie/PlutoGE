#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <cstdint>

namespace PlutoGE::assets
{
    enum class ValidationSeverity { Warning, Error };
    struct ValidationDiagnostic
    {
        ValidationSeverity severity;
        std::string code;
        std::string owner;
        std::uint32_t entity = 0;
        std::size_t line = 0;
        std::string message;
    };
    struct ProjectValidationInput
    {
        std::filesystem::path assetRoot;
        std::string startupScene;
        std::filesystem::path scriptAssembly;
        // An absent class catalogue means class availability cannot be verified.
        std::optional<std::set<std::string>> scriptClasses;
        std::set<std::string> builtinReferences;
        std::function<std::filesystem::path(const std::string &)> resolveEngineReference;
        std::optional<std::string> currentScene;
        std::string currentSceneOwner = "Current unsaved scene";
    };
    struct ProjectValidationResult
    {
        std::vector<ValidationDiagnostic> diagnostics;
        std::size_t checkedFiles = 0;
        bool HasErrors() const;
    };
    // Read-only synchronous validation. No scene loading, metadata writes or UI dependencies.
    ProjectValidationResult ValidateProject(const ProjectValidationInput &input);
}
