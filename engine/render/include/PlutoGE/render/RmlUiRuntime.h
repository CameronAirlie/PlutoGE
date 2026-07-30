#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <vector>

namespace Rml
{
    class Context;
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
    // Owns the single screen-space RmlUi context used by runtime canvases.
    // CanvasComponent remains the scene-facing authoring and migration point.
    class RmlUiRuntime
    {
    public:
        static RmlUiRuntime &Get();

        bool Initialize(platform::Window &window);
        void Shutdown();
        void Render(const scene::Scene &scene, int width, int height, std::uint64_t frameSequence);
        [[nodiscard]] bool IsInitialized() const { return m_context != nullptr; }
        [[nodiscard]] Rml::Context *GetContext() const { return m_context; }
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
        [[nodiscard]] bool IsInputCaptured() const;
        void NotifyEvent(const std::string &key);

    private:
        RmlUiRuntime() = default;
        void SynchronizeDocuments(const scene::Scene &scene);
        void ProcessInput(platform::Window &window, const scene::Scene &scene);
        Rml::ElementDocument *FindDocument(const std::string &document) const;
        void AttachEventSubscriptions();
        void DetachEventSubscriptions();
        void LoadDocumentFonts(const std::filesystem::path &documentPath);

        std::unique_ptr<RenderInterface_GL3> m_renderer;
        std::unique_ptr<SystemInterface_GLFW> m_system;
        Rml::Context *m_context = nullptr;
        platform::Window *m_window = nullptr;
        std::unordered_map<std::string, Rml::ElementDocument *> m_documents;
        std::unordered_map<std::string, std::filesystem::file_time_type> m_documentWriteTimes;
        std::unordered_map<std::string, int> m_pendingEvents;
        std::unordered_set<std::string> m_eventSubscriptions;
        std::unordered_set<std::string> m_attachedEvents;
        std::unordered_set<std::string> m_reportedLoadFailures;
        std::unordered_set<std::string> m_loadedFontFaces;
        std::vector<std::vector<unsigned char>> m_fontData;
        std::unordered_map<std::string, std::unique_ptr<Rml::EventListener>> m_eventListeners;
        int m_width = 0;
        int m_height = 0;
        std::uint64_t m_lastInputFrame = 0;
    };
}
