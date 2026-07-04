#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::assets
{
    enum class ProjectAssetType
    {
        Unknown,
        Scene,
        Prefab,
        Script,
        Mesh,
        Animation,
        AnimationClip,
        SourceModel,
        Material,
        ShaderGraph,
        AnimationGraph,
        ParticleSystem,
        PostProcessPreset,
        Texture,
        Assembly,
        ScriptableObject,
    };

    struct ProjectAssetEntry
    {
        std::string reference;
        std::uintmax_t size = 0;
        ProjectAssetType type = ProjectAssetType::Unknown;
    };

    struct ProjectEditorCameraSettings
    {
        float positionX = 0.0f;
        float positionY = 2.0f;
        float positionZ = 6.0f;
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float fovY = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    struct ProjectPostProcessParameter
    {
        std::string name;
        int type = 0;
        std::string value;
    };

    struct ProjectPostProcessEffect
    {
        std::string typeName;
        bool enabled = true;
        std::vector<ProjectPostProcessParameter> parameters;
    };

    struct ProjectManifest
    {
        std::string name = "UntitledProject";
        std::string assetDirectory = "Assets";
        std::string startupScene;
        std::string scriptAssembly;
        std::string windowTitle = "PlutoGE Runtime";
        int windowWidth = 1280;
        int windowHeight = 720;
        bool vSyncEnabled = true;
        float editorFontSize = 15.0f;
        ProjectEditorCameraSettings editorCamera;
        std::string editorCameraPostProcessPreset;
        std::vector<ProjectPostProcessEffect> editorCameraPostProcessEffects;
        std::vector<ProjectAssetEntry> assetEntries;
    };

    class Project
    {
    public:
        static constexpr std::string_view kProjectAssetScheme = "project://";
        static constexpr std::string_view kEngineAssetScheme = "engine://";
        static constexpr std::string_view kBuiltinCubeMeshReference = "engine://builtin/mesh/cube";
        static constexpr std::string_view kBuiltinSphereMeshReference = "engine://builtin/mesh/sphere";
        static constexpr std::string_view kBuiltinPlaneMeshReference = "engine://builtin/mesh/plane";
        static constexpr std::string_view kBuiltinCylinderMeshReference = "engine://builtin/mesh/cylinder";
        static constexpr std::string_view kBuiltinQuadMeshReference = "engine://builtin/mesh/quad";
        static constexpr std::string_view kBuiltinDefaultMaterialReference = "engine://builtin/material/default";
        static constexpr std::string_view kBuiltinDefaultShadedMaterialReference = "engine://builtin/material/default-shaded";
        static constexpr std::string_view kBuiltinDefaultShaderGraphReference = "engine://builtin/shadergraph/default-lit";
        static constexpr std::string_view kBuiltinDefaultUnlitShaderGraphReference = "engine://builtin/shadergraph/default-unlit";

        Project(std::filesystem::path manifestPath, ProjectManifest manifest);

        static std::unique_ptr<Project> Create(const std::filesystem::path &manifestPath,
                                               std::string projectName,
                                               std::string *errorMessage = nullptr);
        static std::unique_ptr<Project> Load(const std::filesystem::path &manifestPath,
                                             std::string *errorMessage = nullptr);

        static bool IsProjectAssetReference(std::string_view reference);
        static bool IsEngineAssetReference(std::string_view reference);
        static ProjectAssetType GetAssetTypeForReference(std::string_view reference);
        static std::string_view GetAssetTypeName(ProjectAssetType type);
        static ProjectAssetType ParseAssetTypeName(std::string_view typeName);
        static std::vector<std::string> GetBuiltinAssetReferences();

        bool Save(std::string *errorMessage = nullptr) const;
        void RefreshAssetRegistry();

        std::string MakeAssetReference(const std::filesystem::path &filePath) const;
        std::filesystem::path ResolveAssetReference(std::string_view reference) const;
        std::string FindSceneAssetReference(std::string_view nameOrReference) const;
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
