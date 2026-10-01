#pragma once

#include "PlutoGE/core/SceneLoading.h"
#include "PlutoGE/import/MeshImportOptions.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/LoadingScreenStyle.h"
#include "PlutoGE/render/rhi/Types.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Almost every engine source includes this header. Subsystems are owned through
// pointers so that it does not also pull in every subsystem's interface; include
// the subsystem header where its members are used.
namespace PlutoGE::assets
{
    class AssetManager;
}

namespace PlutoGE::assetimport
{
    class MeshImporter;
    struct ImportedMeshAsset;
}

namespace PlutoGE::audio
{
    class AudioSystem;
}

namespace PlutoGE::render
{
    class Material;
    class Mesh;
    class Renderer;
    class RhiRenderService;
    class TextureManager;
    struct AnimationClip;

    namespace rhi
    {
        class IRenderDevice;
        class ISwapchain;
    }
}

namespace PlutoGE::scripting
{
    class ScriptEngine;
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
        platform::WindowConfig windowConfig; // Configuration for the window, set during initialization
        render::rhi::GraphicsApi graphicsApi = render::rhi::GraphicsApi::Vulkan;
        bool vSync = true;
        bool isEditorHost = false;
        render::rhi::TemporalUpscalerOptions temporalUpscaler;
        bool enableProfiling = false;
    };

    class Engine
    {
    public:
        ~Engine();

        bool Initialize(const EngineConfig &config = EngineConfig());
        void Shutdown();

        static Engine &GetInstance()
        {
            static Engine instance;
            return instance;
        }

        [[nodiscard]] const EngineConfig &GetConfig() const { return m_config; }

        [[nodiscard]] platform::Window &GetWindow() { return m_window; }
        [[nodiscard]] render::Renderer &GetRenderer() { return *m_renderer; }
        [[nodiscard]] render::rhi::IRenderDevice *GetRenderDevice() { return m_renderDevice.get(); }
        [[nodiscard]] render::rhi::ISwapchain *GetSwapchain() { return m_swapchain.get(); }
        [[nodiscard]] bool IsVSyncEnabled() const noexcept;
        bool SetVSyncEnabled(bool enabled);
        [[nodiscard]] render::RhiRenderService &GetRhiRenderService() { return *m_rhiRenderService; }
        [[nodiscard]] assets::AssetManager &GetAssetManager() { return *m_assetManager; }
        [[nodiscard]] assetimport::MeshImporter &GetMeshImporter() { return *m_meshImporter; }
        [[nodiscard]] render::TextureManager &GetTextureManager() { return *m_textureManager; }
        [[nodiscard]] scripting::ScriptEngine &GetScriptEngine() { return *m_scriptEngine; }
        [[nodiscard]] audio::AudioSystem &GetAudioSystem() { return *m_audioSystem; }
        [[nodiscard]] scene::Scene *GetScene() { return m_scene; }
        void StartRuntime();
        void StopRuntime();
        [[nodiscard]] bool IsRuntimeRunning() const { return m_isRuntimeRunning; }
        ImportedRenderMeshAsset ImportMeshAsset(const std::string &filePath, const assetimport::MeshImportOptions &options = {});
        ImportedRenderMeshAsset GenerateMeshAssetLods(const std::string &filePath, const assetimport::MeshImportOptions &options = {});
        void SetScene(scene::Scene *scene);
        bool RequestSceneLoad(std::string sceneAssetReference);
        SceneLoading &GetSceneLoading() noexcept { return m_sceneLoading; }
        // Built-in standalone fallback. Use LoadingScreenSession for project assets/controllers.
        void PresentLoadingScreen(const SceneLoadStatus &status, const render::LoadingScreenStyle &style = {});
        std::optional<std::string> ConsumeSceneLoadRequest();
        void RequestApplicationQuit();
        [[nodiscard]] bool ConsumeApplicationQuitRequest();

    private:
        ImportedRenderMeshAsset BuildImportedRenderMeshAsset(const std::string &normalizedPath, const assetimport::ImportedMeshAsset &importedMeshAsset);

        Engine();
        EngineConfig m_config;
        platform::Window m_window;
        // Declaration order is construction order; destruction runs in reverse.
        std::unique_ptr<render::Renderer> m_renderer;
        std::unique_ptr<render::rhi::IRenderDevice> m_renderDevice;
        std::unique_ptr<render::rhi::ISwapchain> m_swapchain;
        std::unique_ptr<render::RhiRenderService> m_rhiRenderService;
        std::unique_ptr<assets::AssetManager> m_assetManager;
        std::unique_ptr<assetimport::MeshImporter> m_meshImporter;
        std::unique_ptr<render::TextureManager> m_textureManager;
        std::unique_ptr<scripting::ScriptEngine> m_scriptEngine;
        std::unique_ptr<audio::AudioSystem> m_audioSystem;
        scene::Scene *m_scene = nullptr;
        struct ImportedMaterialCacheEntry
        {
            uint64_t fingerprint = 0;
            std::vector<std::unique_ptr<render::Material>> materials;
        };
        std::unordered_map<std::string, ImportedMaterialCacheEntry> m_importedMaterialCache;
        std::vector<ImportedMaterialCacheEntry> m_retiredImportedMaterialCache;

        bool m_isInitialized = false;
        bool m_isRuntimeRunning = false;
        SceneLoading m_sceneLoading;
        std::optional<std::string> m_pendingSceneLoadRequest;
        bool m_pendingApplicationQuitRequest = false;
    };
}
