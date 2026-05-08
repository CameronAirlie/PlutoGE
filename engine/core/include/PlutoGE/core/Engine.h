#pragma once

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/scripting/ScriptEngine.h"

#include <memory>
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
        void SetScene(scene::Scene *scene) { m_scene = scene; }

    private:
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

        bool m_isInitialized = false;
    };
}