#pragma once

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/TextureManager.h"

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
        [[nodiscard]] render::TextureManager &GetTextureManager() { return m_textureManager; }
        [[nodiscard]] scene::Scene *GetScene() { return m_scene; }
        void SetScene(scene::Scene *scene) { m_scene = scene; }

    private:
        Engine() = default;
        EngineConfig m_config;
        platform::Window m_window;
        render::Renderer m_renderer;
        assets::AssetManager m_assetManager;
        render::TextureManager m_textureManager;
        scene::Scene *m_scene = nullptr;

        bool m_isInitialized = false;
    };
}