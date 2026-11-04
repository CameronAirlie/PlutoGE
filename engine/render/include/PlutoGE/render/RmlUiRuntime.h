#pragma once

#include <memory>
#include "PlutoGE/render/RmlElementLookup.h"
#include "PlutoGE/render/RmlFontRasterScale.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
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
        class ICommandContext;
    }
    class RmlUiRhiRenderer;
    class RmlLoadingDocument;
    class ScenePortrait;
    struct RmlUiCpuTiming
    {
        float initializeMs = 0.0f;
        float resizeMs = 0.0f;
        float synchronizeMs = 0.0f;
        float inputUpdateMs = 0.0f;
        float beginFrameMs = 0.0f;
        float renderMs = 0.0f;
        float endFrameMs = 0.0f;
        int documentCount = 0;
        int visibleDocumentCount = 0;

        [[nodiscard]] float TotalMs() const
        {
            return initializeMs + resizeMs + synchronizeMs + inputUpdateMs +
                   beginFrameMs + renderMs + endFrameMs;
        }
    };

    // Owns separate screen and world RmlUi contexts used by runtime canvases.
    // CanvasComponent remains the scene-facing authoring and migration point.
    class RmlUiRuntime
    {
    public:
        static RmlUiRuntime &Get();

        // Fails without an RHI device; RmlUi renders only through RmlUiRhiRenderer.
        bool Initialize(platform::Window &window, rhi::IRenderDevice *rhiDevice);
        void ResetRuntimeState();
        std::shared_ptr<RmlLoadingDocument> CreateLoadingDocument(const std::string &reference,
            platform::Window &window, rhi::IRenderDevice &device);
        // Editor-only, synchronous source overlay. It is scoped to document
        // construction and never changes files or runtime document sources.
        std::shared_ptr<RmlLoadingDocument> CreatePreviewDocument(
            const std::string &documentPath, const std::unordered_map<std::string, std::string> &sources,
            platform::Window &window, rhi::IRenderDevice &device, std::vector<std::string> &diagnostics);
        // Scoped by the loading session around controller callbacks only.
        Rml::ElementDocument *SetLoadingDocumentTarget(Rml::ElementDocument *document);
        // Run before the host begins scene recording; cached previews submit only when dirty.
        void PrepareScenePortraits(const scene::Scene &scene, rhi::IRenderDevice &device);
        void Shutdown();
        // `commands`, when given, receives the UI's commands (for example a deferred list a render thread replays)
        // in place of the device's immediate context; nothing is then submitted.
        void RenderRhi(const scene::Scene &scene, rhi::IRenderDevice &device, rhi::TextureHandle target,
                       int width, int height, std::uint64_t frameSequence,
                       const glm::mat4 &view, const glm::mat4 &projection,
                       bool manageSubmission = true, rhi::ICommandContext *commands = nullptr);
        [[nodiscard]] bool IsInitialized() const { return m_context != nullptr; }
        [[nodiscard]] Rml::Context *GetContext() const { return m_context; }
        [[nodiscard]] const RmlUiCpuTiming &GetCpuTiming() const { return m_cpuTiming; }
        // Screen-space dp lengths only; physical pixels and world documents are unaffected.
        bool SetInterfaceScale(float scale);
        [[nodiscard]] float GetInterfaceScale() const noexcept { return m_interfaceScale; }
        [[nodiscard]] Rml::Context *GetWorldContext() const { return m_worldContext; }
        bool ShowDocument(const std::string &document, bool visible);
        bool ReloadDocument(const std::string &document);
        bool SetElementText(const std::string &document, const std::string &id, const std::string &text);
        std::string GetElementText(const std::string &document, const std::string &id) const;
        bool ScrollElementIntoView(const std::string &document, const std::string &id);
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
        friend class RmlLoadingDocument;
        std::vector<std::weak_ptr<RmlLoadingDocument>> m_loadingDocuments;
        Rml::ElementDocument *m_loadingDocumentTarget = nullptr;
        RmlUiRuntime() = default;
        void ClearScenePortraits();
        struct PortraitEntry
        {
            std::shared_ptr<ScenePortrait> renderer;
            std::string source, signature;
        };
        std::unordered_map<std::string, PortraitEntry> m_portraits;
        const scene::Scene *m_portraitScene = nullptr;
        std::uint64_t m_portraitSequence = 0;
        void SynchronizeDocuments(const scene::Scene &scene, const glm::mat4 &view, const glm::mat4 &projection);
        void ProcessInput(platform::Window &window, const scene::Scene &scene);
        Rml::ElementDocument *FindDocument(const std::string &document) const;
        void AttachEventSubscriptions();
        void DetachEventSubscriptions();
        void LoadDocumentFonts(const std::filesystem::path &documentPath);
        bool ConsumeAssetFileChange();
        void CloseAssetFileWatcher();

        std::unique_ptr<RmlUiRhiRenderer> m_rhiRenderer;
        std::unique_ptr<SystemInterface_GLFW> m_system;
        Rml::Context *m_context = nullptr;
        Rml::Context *m_worldContext = nullptr;
        Rml::Context *m_pointerContext = nullptr;
        Rml::Context *m_keyboardContext = nullptr;
        float m_interfaceScale = 1.0f;
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
        std::unordered_map<std::string, std::string> m_generatedDocumentSources;
        std::unordered_map<std::string, std::string> m_resolvedGeneratedFonts;
        // Mirrors the process-wide RmlUi atlas density, so runtime resets keep it.
        RmlFontRasterScale m_fontRasterScale;
        std::chrono::steady_clock::time_point m_lastFontRasterUpdate{};
        std::unordered_map<std::string, glm::vec4> m_documentProjectionState;
        std::unordered_map<std::string, glm::vec2> m_documentSizes;
        std::unordered_map<std::string, int> m_pendingEvents;
        mutable RmlElementLookup m_elementLookup;
        mutable std::unordered_set<std::string> m_missingDocuments;
        bool m_eventSubscriptionsDirty = true;
        std::unordered_set<std::string> m_eventSubscriptions;
        std::unordered_set<std::string> m_attachedEvents;
        std::unordered_set<std::string> m_reportedLoadFailures;
        std::unordered_set<std::string> m_loadedFontFaces;
        std::vector<std::vector<unsigned char>> m_fontData;
        std::unordered_map<std::string, std::unique_ptr<Rml::EventListener>> m_eventListeners;
        std::unique_ptr<Rml::EventListener> m_panZoom;
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
