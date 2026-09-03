#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <vector>
#include <glm/mat4x4.hpp>
#include "PlutoGE/render/rhi/Types.h"

namespace Rml
{
    class Context;
    class Element;
    class ElementDocument;
    class EventListener;
}

class RenderInterface_GL3;
class SystemInterface_GLFW;

namespace PlutoGE::platform
{
    class Window;
}

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::render
{
    namespace rhi
    {
        class IRenderDevice;
    }
    class RmlUiRhiRenderer;
    struct RmlUiCpuTiming
    {
        float initializeMs = 0.0f;
        float resizeMs = 0.0f;
        float synchronizeMs = 0.0f;
        float inputUpdateMs = 0.0f;
        float worldSurfaceMs = 0.0f;
        float beginFrameMs = 0.0f;
        float backdropMs = 0.0f;
        float renderMs = 0.0f;
        float endFrameMs = 0.0f;
        int documentCount = 0;
        int visibleDocumentCount = 0;
        bool copiedBackdrop = false;

        [[nodiscard]] float TotalMs() const
        {
            return initializeMs + resizeMs + synchronizeMs + inputUpdateMs + worldSurfaceMs +
                   beginFrameMs + backdropMs + renderMs + endFrameMs;
        }
    };

    struct RmlUiWorldSurface
    {
        unsigned int texture = 0;
        glm::mat4 model{1.0f};
    };

    // Owns the single screen-space RmlUi context used by runtime canvases.
    // CanvasComponent remains the scene-facing authoring and migration point.
    class RmlUiRuntime
    {
    public:
        static RmlUiRuntime &Get();

        bool Initialize(platform::Window &window, rhi::IRenderDevice *rhiDevice = nullptr);
        void ResetRuntimeState();
        void Shutdown();
        void Render(const scene::Scene &scene, int width, int height, std::uint64_t frameSequence,
                    const glm::mat4 &view, const glm::mat4 &projection,
                    const std::function<void()> &drawWorldSurfaces = {});
        void RenderRhi(const scene::Scene &scene, rhi::IRenderDevice &device, rhi::TextureHandle target,
                       int width, int height, std::uint64_t frameSequence,
                       const glm::mat4 &view, const glm::mat4 &projection,
                       bool manageSubmission = true);
        [[nodiscard]] bool IsInitialized() const { return m_context != nullptr; }
        [[nodiscard]] Rml::Context *GetContext() const { return m_context; }
        [[nodiscard]] const RmlUiCpuTiming &GetCpuTiming() const { return m_cpuTiming; }
        [[nodiscard]] const std::vector<RmlUiWorldSurface> &GetWorldSurfaces() const { return m_worldSurfaceDraws; }
        bool ShowDocument(const std::string &document, bool visible);
        bool ReloadDocument(const std::string &document);
        bool SetElementText(const std::string &document, const std::string &id, const std::string &text);
        std::string GetElementText(const std::string &document, const std::string &id) const;
        bool SetElementAttribute(const std::string &document, const std::string &id,
                                 const std::string &name, const std::string &value);
        std::string GetElementAttribute(const std::string &document, const std::string &id,
                                        const std::string &name) const;
        bool SetElementClass(const std::string &document, const std::string &id,
                             const std::string &name, bool enabled);
        bool SetElementStyle(const std::string &document, const std::string &id,
                             const std::string &name, const std::string &value);
        bool SubscribeEvent(const std::string &document, const std::string &id, const std::string &event);
        bool ConsumeEvent(const std::string &document, const std::string &id, const std::string &event);
        [[nodiscard]] bool IsPointerInputCaptured() const;
        [[nodiscard]] bool IsKeyboardInputCaptured() const;
        void NotifyEvent(const std::string &key);
        void NotifyEventListenerDetached(const std::string &key, Rml::Element *element);

    private:
        RmlUiRuntime() = default;
        void SynchronizeDocuments(const scene::Scene &scene, const glm::mat4 &view, const glm::mat4 &projection);
        void ProcessInput(platform::Window &window, const scene::Scene &scene);
        Rml::ElementDocument *FindDocument(const std::string &document) const;
        void AttachEventSubscriptions();
        void DetachEventSubscriptions();
        void LoadDocumentFonts(const std::filesystem::path &documentPath);
        void MarkWorldSurfaceDirty(Rml::ElementDocument *document);
        bool ConsumeAssetFileChange();
        void CloseAssetFileWatcher();
        void RenderWorldSurfaces();
        void DestroyWorldSurfaceTargets();

        struct WorldSurfaceTarget
        {
            unsigned int framebuffer = 0;
            unsigned int texture = 0;
            int width = 0;
            int height = 0;
            glm::mat4 model{1.0f};
            bool dirty = true;
        };

        std::unique_ptr<RenderInterface_GL3> m_renderer;
        std::unique_ptr<RmlUiRhiRenderer> m_rhiRenderer;
        std::unique_ptr<SystemInterface_GLFW> m_system;
        Rml::Context *m_context = nullptr;
        platform::Window *m_window = nullptr;
        std::unordered_map<std::string, Rml::ElementDocument *> m_documents;
        // Script-side paths often use an Assets-relative spelling while the
        // loaded document map uses a project URI. Cache the resolved key so
        // frequent HUD mutations do not normalize paths and scan every time.
        mutable std::unordered_map<std::string, std::string> m_documentAliases;
        // Instance key -> asset reference. Projected canvases are instanced per entity.
        std::unordered_map<std::string, std::string> m_documentReferences;
        std::unordered_map<std::string, std::filesystem::file_time_type> m_documentWriteTimes;
        std::unordered_map<std::string, float> m_documentScales;
        std::unordered_map<std::string, bool> m_documentUsesBackdrop;
        std::unordered_map<std::string, std::string> m_generatedDocumentSources;
        std::unordered_map<std::string, std::string> m_resolvedGeneratedFonts;
        std::unordered_map<std::string, glm::vec4> m_documentProjectionState;
        std::unordered_map<std::string, glm::vec2> m_documentSizes;
        std::unordered_map<std::string, WorldSurfaceTarget> m_worldSurfaceTargets;
        std::vector<RmlUiWorldSurface> m_worldSurfaceDraws;
        std::unordered_map<std::string, int> m_pendingEvents;
        std::unordered_set<std::string> m_eventSubscriptions;
        std::unordered_set<std::string> m_attachedEvents;
        std::unordered_set<std::string> m_reportedLoadFailures;
        std::unordered_set<std::string> m_loadedFontFaces;
        std::vector<std::vector<unsigned char>> m_fontData;
        std::unordered_map<std::string, std::unique_ptr<Rml::EventListener>> m_eventListeners;
        std::unordered_map<std::string, Rml::Element *> m_eventListenerElements;
        int m_width = 0;
        int m_height = 0;
        std::uint64_t m_lastInputFrame = 0;
        void *m_assetFileChangeHandle = nullptr;
        std::filesystem::path m_watchedAssetDirectory;
        bool m_hotReloadEnabled = false;
        RmlUiCpuTiming m_cpuTiming;
    };
}
