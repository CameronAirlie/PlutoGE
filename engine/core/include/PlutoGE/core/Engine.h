#pragma once

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

        static Engine &GetInstance()
        {
            static Engine instance;
            return instance;
        }

        [[nodiscard]] const EngineConfig &GetConfig() const { return m_config; }

        [[nodiscard]] platform::Window &GetWindow() { return m_window; }
        [[nodiscard]] render::Renderer &GetRenderer() { return m_renderer; }
        [[nodiscard]] assets::AssetManager &GetAssetManager() { return m_assetManager; }
        [[nodiscard]] assetimport::MeshImporter &GetMeshImporter() { return m_meshImporter; }
        [[nodiscard]] render::TextureManager &GetTextureManager() { return m_textureManager; }
        [[nodiscard]] scripting::ScriptEngine &GetScriptEngine() { return m_scriptEngine; }
        [[nodiscard]] scene::Scene *GetScene() { return m_scene; }
        ImportedRenderMeshAsset ImportMeshAsset(const std::string &filePath);
        render::Mesh *ImportMesh(const std::string &filePath);
        void QueueMeshImport(scene::EntityID entityId, const std::string &filePath);
        void UpdateAsyncMeshImports();
        [[nodiscard]] MeshImportStatus GetMeshImportStatus(scene::EntityID entityId) const;
        void SetScene(scene::Scene *scene) { m_scene = scene; }

    private:
        struct PendingMeshImportJob
        {
            scene::EntityID entityId = 0;
            std::string normalizedPath;
            std::future<assetimport::ImportedMeshSourceAsset> future;
        };

        ImportedRenderMeshAsset BuildImportedRenderMeshAsset(const std::string &normalizedPath, const assetimport::ImportedMeshAsset &importedMeshAsset);
        ImportedRenderMeshAsset FinalizeImportedMeshAsset(const std::string &filePath, assetimport::ImportedMeshSourceAsset importedMeshSourceAsset);

        Engine() = default;
        EngineConfig m_config;
        platform::Window m_window;
        render::Renderer m_renderer;
        assets::AssetManager m_assetManager;
        assetimport::MeshImporter m_meshImporter;
        render::TextureManager m_textureManager;
        scripting::ScriptEngine m_scriptEngine;
        scene::Scene *m_scene = nullptr;
        std::unordered_map<std::string, std::vector<std::unique_ptr<render::Material>>> m_importedMaterialCache;
        std::unordered_map<scene::EntityID, PendingMeshImportJob> m_pendingMeshImports;
        std::unordered_map<scene::EntityID, std::string> m_meshImportErrors;

        bool m_isInitialized = false;
    };
}