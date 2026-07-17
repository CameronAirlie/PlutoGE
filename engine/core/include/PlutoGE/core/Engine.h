#pragma once

#include "PlutoGE/audio/AudioSystem.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/scripting/ScriptEngine.h"

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    class Texture;
    class Material;
    class Shader;
}

namespace PlutoGE::scene
{
    using EntityID = uint32_t;
    class Scene;
}

namespace PlutoGE::core
{
    struct ImportedRenderMeshAsset
    {
        render::Mesh *mesh = nullptr;
        std::vector<render::Material *> materials;
        const std::vector<render::AnimationClip> *animations = nullptr;
    };

    struct EngineConfig
    {
        // Future configuration options can be added here
        platform::WindowConfig windowConfig; // Configuration for the window, set during initialization
    };

    struct MeshImportStatus
    {
        bool pending = false;
        std::string filePath;
        std::string errorMessage;
    };

    class Engine
    {
    public:
        ~Engine() = default;

        bool Initialize(const EngineConfig &config = EngineConfig());
        void Run();
        void Shutdown();

        // Defined out of line so shared-engine builds have one process-wide
        // instance rather than separate header-local instances in the DLL and
        // each host executable.
        static Engine &GetInstance();

        [[nodiscard]] const EngineConfig &GetConfig() const { return m_config; }

        [[nodiscard]] platform::Window &GetWindow() { return m_window; }
        [[nodiscard]] render::Renderer &GetRenderer() { return m_renderer; }
        [[nodiscard]] assets::AssetManager &GetAssetManager() { return m_assetManager; }
        [[nodiscard]] assetimport::MeshImporter &GetMeshImporter() { return m_meshImporter; }
        [[nodiscard]] render::TextureManager &GetTextureManager() { return m_textureManager; }
        [[nodiscard]] scripting::ScriptEngine &GetScriptEngine() { return m_scriptEngine; }
        [[nodiscard]] audio::AudioSystem &GetAudioSystem() { return m_audioSystem; }
        [[nodiscard]] scene::Scene *GetScene() { return m_scene; }
        void StartRuntime();
        void StopRuntime();
        [[nodiscard]] bool IsRuntimeRunning() const { return m_isRuntimeRunning; }
        ImportedRenderMeshAsset ImportMeshAsset(const std::string &filePath, const assetimport::MeshImportOptions &options = {});
        ImportedRenderMeshAsset GenerateMeshAssetLods(const std::string &filePath, const assetimport::MeshImportOptions &options = {});
        render::Mesh *ImportMesh(const std::string &filePath);
        void QueueMeshImport(scene::EntityID entityId, const std::string &filePath);
        void UpdateAsyncMeshImports();
        [[nodiscard]] MeshImportStatus GetMeshImportStatus(scene::EntityID entityId) const;
        void SetScene(scene::Scene *scene);
        bool RequestSceneLoad(std::string sceneAssetReference);
        std::optional<std::string> ConsumeSceneLoadRequest();

    private:
        struct PendingMeshImportJob
        {
            scene::EntityID entityId = 0;
            std::string normalizedPath;
            std::future<assetimport::ImportedMeshSourceAsset> future;
        };

        ImportedRenderMeshAsset BuildImportedRenderMeshAsset(const std::string &normalizedPath, const assetimport::ImportedMeshAsset &importedMeshAsset);
        ImportedRenderMeshAsset FinalizeImportedMeshAsset(const std::string &filePath, assetimport::ImportedMeshSourceAsset importedMeshSourceAsset, const assetimport::MeshImportOptions &options = {});

        Engine() = default;
        EngineConfig m_config;
        platform::Window m_window;
        render::Renderer m_renderer;
        assets::AssetManager m_assetManager;
        assetimport::MeshImporter m_meshImporter;
        render::TextureManager m_textureManager;
        scripting::ScriptEngine m_scriptEngine;
        audio::AudioSystem m_audioSystem;
        scene::Scene *m_scene = nullptr;
        struct ImportedMaterialCacheEntry
        {
            uint64_t fingerprint = 0;
            std::vector<std::unique_ptr<render::Material>> materials;
        };
        std::unordered_map<std::string, ImportedMaterialCacheEntry> m_importedMaterialCache;
        std::vector<ImportedMaterialCacheEntry> m_retiredImportedMaterialCache;
        std::unordered_map<scene::EntityID, PendingMeshImportJob> m_pendingMeshImports;
        std::unordered_map<scene::EntityID, std::string> m_meshImportErrors;

        bool m_isInitialized = false;
        bool m_isRuntimeRunning = false;
        std::optional<std::string> m_pendingSceneLoadRequest;
    };
}
