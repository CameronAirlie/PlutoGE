#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::assets
{
    struct ProjectAssetEntry
    {
        std::string reference;
        std::uintmax_t size = 0;
    };

    struct ProjectManifest
    {
        std::string name = "UntitledProject";
        std::string assetDirectory = "Assets";
        std::string startupScene;
        std::string windowTitle = "PlutoGE Runtime";
        int windowWidth = 1280;
        int windowHeight = 720;
        bool vSyncEnabled = true;
        std::vector<ProjectAssetEntry> assetEntries;
    };

    class Project
    {
    public:
        static constexpr std::string_view kProjectAssetScheme = "project://";
        static constexpr std::string_view kEngineAssetScheme = "engine://";
        static constexpr std::string_view kBuiltinCubeMeshReference = "engine://builtin/mesh/cube";
        static constexpr std::string_view kBuiltinDefaultMaterialReference = "engine://builtin/material/default";

        Project(std::filesystem::path manifestPath, ProjectManifest manifest);

        static std::unique_ptr<Project> Create(const std::filesystem::path &manifestPath,
                                               std::string projectName,
                                               std::string *errorMessage = nullptr);
        static std::unique_ptr<Project> Load(const std::filesystem::path &manifestPath,
                                             std::string *errorMessage = nullptr);

        static bool IsProjectAssetReference(std::string_view reference);
        static bool IsEngineAssetReference(std::string_view reference);
        static std::vector<std::string> GetBuiltinAssetReferences();

        bool Save(std::string *errorMessage = nullptr) const;
        void RefreshAssetRegistry();

        std::string MakeAssetReference(const std::filesystem::path &filePath) const;
        std::filesystem::path ResolveAssetReference(std::string_view reference) const;
        bool IsInAssetDirectory(const std::filesystem::path &filePath) const;

        const ProjectManifest &GetManifest() const { return m_manifest; }
        ProjectManifest &GetManifest() { return m_manifest; }
        const std::filesystem::path &GetManifestPath() const { return m_manifestPath; }
        const std::filesystem::path &GetRootDirectory() const { return m_rootDirectory; }
        std::filesystem::path GetAssetDirectoryPath() const;

    private:
        std::filesystem::path m_manifestPath;
        std::filesystem::path m_rootDirectory;
        ProjectManifest m_manifest;
    };

    std::filesystem::path GetRuntimeManifestPathForExecutable(const std::filesystem::path &executablePath);
    std::filesystem::path FindRuntimeExecutable(const std::filesystem::path &searchRoot);
    bool ExportStandaloneProject(const Project &project,
                                 const std::filesystem::path &destinationExecutablePath,
                                 const std::filesystem::path &runtimeExecutablePath,
                                 std::string *errorMessage = nullptr);
}