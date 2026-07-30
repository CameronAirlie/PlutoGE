#include "PlutoGE/render/RmlUiRuntime.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi_Platform_GLFW.h>
#include <RmlUi_Renderer_GL3.h>
#include <PlutoGE_RmlUi_Target.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::render
{
    namespace
    {
        constexpr char kEventSeparator = '\x1f';

        std::string ResolveDocumentPath(assets::AssetManager &assets, const std::string &reference)
        {
            if (reference.empty())
                return {};

            // Canvas document paths are authored relative to the project's asset
            // directory. AssetManager intentionally leaves ordinary relative
            // paths relative to the process working directory, so qualify them
            // here. Explicit schemes and absolute paths retain their normal
            // AssetManager behaviour.
            const std::filesystem::path referencePath(reference);
            if (!referencePath.is_absolute() && reference.find("://") == std::string::npos &&
                !assets.GetProjectRootDirectory().empty())
            {
                return (std::filesystem::path(assets.GetProjectRootDirectory()) /
                        assets.GetProjectAssetDirectory() / referencePath)
                    .lexically_normal().string();
            }

            return assets.ResolveAssetPath(reference);
        }

        std::filesystem::path NormalizeDocumentPath(assets::AssetManager &assets,
                                                    const std::string &reference)
        {
            const std::string resolved = ResolveDocumentPath(assets, reference);
            if (resolved.empty())
                return {};

            std::error_code error;
            auto normalized = std::filesystem::weakly_canonical(resolved, error);
            if (error)
                normalized = std::filesystem::path(resolved).lexically_normal();
            return normalized;
        }

        std::string EventKey(const std::string &document, const std::string &id, const std::string &event)
        {
            return document + kEventSeparator + id + kEventSeparator + event;
        }

        std::filesystem::file_time_type DocumentSourceWriteTime(const std::filesystem::path &document,
                                                                std::error_code &error)
        {
            auto newest = std::filesystem::last_write_time(document, error);
            if (error) return {};
            for (std::filesystem::directory_iterator it(document.parent_path(), error), end; !error && it != end; it.increment(error))
            {
                if (it->is_regular_file() && it->path().extension() == ".rcss")
                {
                    std::error_code timeError;
                    newest = std::max(newest, std::filesystem::last_write_time(it->path(), timeError));
                }
            }
            return newest;
        }

        class RuntimeEventListener final : public Rml::EventListener
        {
        public:
            RuntimeEventListener(RmlUiRuntime &owner, std::string key)
                : m_owner(owner), m_key(std::move(key)) {}

            void ProcessEvent(Rml::Event &) override { m_owner.NotifyEvent(m_key); }
            void OnDetach(Rml::Element *element) override
            {
                m_owner.NotifyEventListenerDetached(m_key, element);
            }

        private:
            RmlUiRuntime &m_owner;
            std::string m_key;
        };

        void CollectDocuments(scene::Entity *entity, std::unordered_map<std::string, bool> &documents)
        {
            if (!entity || !entity->IsActive())
                return;

            if (const auto *canvas = entity->GetComponent<scene::CanvasComponent>();
                canvas && canvas->IsEnabled() &&
                canvas->GetBackend() == scene::UIRenderBackend::RmlUi &&
                !canvas->GetDocumentPath().empty())
            {
                documents[canvas->GetDocumentPath()] = true;
                // A document canvas owns its subtree. Nested native canvases are
                // still discovered through their own roots during migration.
            }

            if (const auto *widget = entity->GetComponent<scene::RmlWidgetComponent>();
                widget && widget->IsEnabled() && !widget->GetSource().empty())
            {
                auto [entry, inserted] = documents.try_emplace(widget->GetSource(), widget->IsVisible());
                if (!inserted)
                    entry->second = entry->second || widget->IsVisible();
            }

            for (auto *child : entity->GetChildren())
                CollectDocuments(child, documents);
        }

        int CurrentModifiers(GLFWwindow *window)
        {
            int glfwModifiers = 0;
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
                glfwModifiers |= GLFW_MOD_SHIFT;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
                glfwModifiers |= GLFW_MOD_CONTROL;
            if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
                glfwModifiers |= GLFW_MOD_ALT;
            if (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
                glfwModifiers |= GLFW_MOD_SUPER;
            return RmlGLFW::ConvertKeyModifiers(glfwModifiers);
        }
    }

    RmlUiRuntime &RmlUiRuntime::Get()
    {
        static RmlUiRuntime instance;
        return instance;
    }

    bool RmlUiRuntime::Initialize(platform::Window &window)
    {
        if (m_context)
            return true;

        m_window = &window;
        m_renderer = std::make_unique<RenderInterface_GL3>();
        if (!static_cast<bool>(*m_renderer))
        {
            m_renderer.reset();
            m_window = nullptr;
            return false;
        }

        m_system = std::make_unique<SystemInterface_GLFW>();
        m_system->SetWindow(static_cast<GLFWwindow *>(window.GetWindow()));
        Rml::SetRenderInterface(m_renderer.get());
        Rml::SetSystemInterface(m_system.get());
        if (!Rml::Initialise())
        {
            m_system.reset();
            m_renderer.reset();
            m_window = nullptr;
            return false;
        }

        const auto extents = window.GetExtents();
        m_width = std::max(extents.width, 1);
        m_height = std::max(extents.height, 1);
        m_context = Rml::CreateContext("PlutoGE.Runtime", {m_width, m_height});
        if (!m_context)
        {
            Rml::Shutdown();
            m_system.reset();
            m_renderer.reset();
            m_window = nullptr;
            return false;
        }
        return true;
    }

    void RmlUiRuntime::Shutdown()
    {
        if (!m_context && !m_renderer && !m_system)
            return;

        DetachEventSubscriptions();
        m_documents.clear();
        m_documentWriteTimes.clear();
        m_eventSubscriptions.clear();
        m_reportedLoadFailures.clear();
        m_loadedFontFaces.clear();
        m_fontData.clear();
        m_pendingEvents.clear();
        if (m_context)
        {
            Rml::RemoveContext(m_context->GetName());
            m_context = nullptr;
        }
        Rml::Shutdown();
        m_system.reset();
        m_renderer.reset();
        m_window = nullptr;
        m_width = 0;
        m_height = 0;
        m_lastInputFrame = 0;
    }

    void RmlUiRuntime::SynchronizeDocuments(const scene::Scene &scene)
    {
        std::unordered_map<std::string, bool> requestedDocuments;
        for (auto *root : scene.GetRootEntities())
            CollectDocuments(root, requestedDocuments);

        bool detachedForDocumentChange = false;
        for (auto it = m_documents.begin(); it != m_documents.end();)
        {
            if (!requestedDocuments.contains(it->first))
            {
                if (!detachedForDocumentChange)
                {
                    DetachEventSubscriptions();
                    detachedForDocumentChange = true;
                }
                if (it->second)
                    it->second->Close();
                m_documentWriteTimes.erase(it->first);
                it = m_documents.erase(it);
            }
            else
            {
                ++it;
            }
        }

        auto &assets = core::Engine::GetInstance().GetAssetManager();
        for (const auto &[reference, visible] : requestedDocuments)
        {
            if (m_documents.contains(reference))
            {
                const std::string resolved = ResolveDocumentPath(assets, reference);
                std::error_code error;
                const auto writeTime = DocumentSourceWriteTime(resolved, error);
                const auto known = m_documentWriteTimes.find(reference);
                if (!error && known != m_documentWriteTimes.end() && writeTime != known->second)
                    ReloadDocument(reference);
                if (auto *document = m_documents.at(reference))
                {
                    if (visible)
                    {
                        document->Show();
                    }
                    else
                    {
                        m_context->UnfocusDocument(document);
                        document->Hide();
                    }
                }
                continue;
            }

            const std::string path = ResolveDocumentPath(assets, reference);
            if (path.empty())
            {
                if (m_reportedLoadFailures.insert(reference).second)
                    std::cerr << "[RmlUi] Could not resolve Canvas document '" << reference << "'.\n";
                continue;
            }

            std::error_code existsError;
            if (!std::filesystem::is_regular_file(path, existsError))
            {
                if (m_reportedLoadFailures.insert(reference).second)
                    std::cerr << "[RmlUi] Canvas document '" << reference
                              << "' was not found at '" << path << "'.\n";
                continue;
            }

            LoadDocumentFonts(path);
            if (auto *document = m_context->LoadDocument(path))
            {
                visible ? document->Show() : document->Hide();
                m_documents.emplace(reference, document);
                m_reportedLoadFailures.erase(reference);
                std::clog << "[RmlUi] Loaded Canvas document '" << reference
                          << "' from '" << path << "'.\n";
                std::error_code error;
                const auto writeTime = DocumentSourceWriteTime(path, error);
                if (!error)
                    m_documentWriteTimes[reference] = writeTime;
            }
            else if (m_reportedLoadFailures.insert(reference).second)
            {
                std::cerr << "[RmlUi] Failed to parse or load Canvas document '" << reference
                          << "' from '" << path << "'. Check the preceding RmlUi messages for markup errors.\n";
            }
        }
        AttachEventSubscriptions();
    }

    void RmlUiRuntime::LoadDocumentFonts(const std::filesystem::path &documentPath)
    {
        // RmlUi requires fonts to be registered through LoadFontFace; RCSS
        // @font-face rules are a PlutoGE authoring convenience parsed here.
        // Keep the byte buffers alive until Rml::Shutdown as required by RmlUi.
        std::ifstream documentStream(documentPath, std::ios::binary);
        if (!documentStream)
            return;
        const std::string documentSource(
            (std::istreambuf_iterator<char>(documentStream)),
            std::istreambuf_iterator<char>());

        // Only inspect stylesheets linked by this document. Scanning every
        // sibling RCSS produced misleading missing-font warnings from
        // unrelated documents stored in the same UI directory.
        static const std::regex styleLinkRule(
            R"rml(<link\b[^>]*\bhref\s*=\s*(?:"([^"]+\.rcss)"|'([^']+\.rcss)')[^>]*>)rml",
            std::regex::icase);
        std::vector<std::filesystem::path> styleSheets;
        for (std::sregex_iterator link(documentSource.begin(), documentSource.end(), styleLinkRule), end;
             link != end; ++link)
        {
            const std::string relativePath =
                (*link)[1].matched ? (*link)[1].str() : (*link)[2].str();
            styleSheets.push_back(
                (documentPath.parent_path() / relativePath).lexically_normal());
        }

        for (const auto &styleSheetPath : styleSheets)
        {
            std::ifstream stream(styleSheetPath, std::ios::binary);
            if (!stream)
                continue;
            const std::string source((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());

            static const std::regex faceRule(R"(@font-face\s*\{([^}]*)\})",
                                             std::regex::icase);
            static const std::regex familyRule(
                R"rml(font-family\s*:\s*(?:"([^"]+)"|'([^']+)'|([^;]+))\s*;)rml",
                std::regex::icase);
            static const std::regex sourceRule(
                R"rml(src\s*:\s*url\(\s*(?:"([^"]+)"|'([^']+)'|([^)]+))\s*\)\s*;)rml",
                std::regex::icase);

            for (std::sregex_iterator face(source.begin(), source.end(), faceRule), endFace;
                 face != endFace; ++face)
            {
                const std::string body = (*face)[1].str();
                std::smatch familyMatch;
                std::smatch sourceMatch;
                if (!std::regex_search(body, familyMatch, familyRule) ||
                    !std::regex_search(body, sourceMatch, sourceRule))
                    continue;

                const auto matchedValue = [](const std::smatch &match) {
                    for (std::size_t i = 1; i < match.size(); ++i)
                    {
                        if (!match[i].matched) continue;
                        std::string value = match[i].str();
                        const auto first = value.find_first_not_of(" \t\r\n");
                        const auto last = value.find_last_not_of(" \t\r\n");
                        return first == std::string::npos
                            ? std::string{}
                            : value.substr(first, last - first + 1);
                    }
                    return std::string{};
                };
                const std::string family = matchedValue(familyMatch);
                const std::string relativeSource = matchedValue(sourceMatch);
                const auto fontPath = (styleSheetPath.parent_path() / relativeSource)
                                          .lexically_normal();
                const std::string key = fontPath.generic_string() + "\x1f" + family;
                if (m_loadedFontFaces.contains(key))
                    continue;

                std::ifstream fontStream(fontPath, std::ios::binary);
                if (!fontStream)
                {
                    std::cerr << "[RmlUi] Font '" << family << "' was not found at '"
                              << fontPath.string() << "' (declared in '"
                              << styleSheetPath.string() << "').\n";
                    m_loadedFontFaces.insert(key);
                    continue;
                }

                std::vector<unsigned char> bytes(
                    (std::istreambuf_iterator<char>(fontStream)),
                    std::istreambuf_iterator<char>());
                if (bytes.empty())
                    continue;

                m_fontData.push_back(std::move(bytes));
                const auto &stored = m_fontData.back();
                if (Rml::LoadFontFace(
                        {stored.data(), stored.size()}, family,
                        Rml::Style::FontStyle::Normal,
                        Rml::Style::FontWeight::Auto))
                {
                    std::clog << "[RmlUi] Loaded font face '" << family << "' from '"
                              << fontPath.string() << "'.\n";
                    m_loadedFontFaces.insert(key);
                }
                else
                {
                    std::cerr << "[RmlUi] Failed to load font face '" << family
                              << "' from '" << fontPath.string() << "'.\n";
                    m_fontData.pop_back();
                    m_loadedFontFaces.insert(key);
                }
            }
        }
    }

    Rml::ElementDocument *RmlUiRuntime::FindDocument(const std::string &document) const
    {
        const auto found = m_documents.find(document);
        if (found != m_documents.end())
            return found->second;

        // Canvas paths and serialized script fields may spell the same asset
        // differently (for example a project asset URI versus a path relative
        // to the Assets directory). Resolve both forms before giving up so the
        // managed RmlDocument API addresses the document selected by Canvas.
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        const auto requestedPath = NormalizeDocumentPath(assets, document);
        if (requestedPath.empty())
            return nullptr;

        for (const auto &[reference, loaded] : m_documents)
        {
            if (NormalizeDocumentPath(assets, reference) == requestedPath)
                return loaded;
        }
        return nullptr;
    }

    bool RmlUiRuntime::ShowDocument(const std::string &document, bool visible)
    {
        auto *target = FindDocument(document);
        if (!target)
            return false;
        if (visible)
        {
            target->Show();
        }
        else
        {
            m_context->UnfocusDocument(target);
            target->Hide();
        }
        return true;
    }

    bool RmlUiRuntime::ReloadDocument(const std::string &document)
    {
        if (!m_context)
            return false;
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        const std::string path = ResolveDocumentPath(assets, document);
        if (path.empty())
            return false;
        LoadDocumentFonts(path);
        if (auto found = m_documents.find(document); found != m_documents.end())
        {
            DetachEventSubscriptions();
            if (found->second) found->second->Close();
            m_documents.erase(found);
        }

        // LoadDocument caches parsed style sheets and templates globally.
        // Closing and reopening the RML alone would otherwise reuse the stale
        // RCSS object after the file watcher detects a change.
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();

        auto *loaded = m_context->LoadDocument(path);
        if (!loaded)
            return false;
        loaded->Show();
        m_documents[document] = loaded;
        std::error_code error;
        const auto writeTime = DocumentSourceWriteTime(path, error);
        if (!error) m_documentWriteTimes[document] = writeTime;
        std::clog << "[RmlUi] Hot reloaded document and styles for '" << document << "'.\n";
        AttachEventSubscriptions();
        return true;
    }

    bool RmlUiRuntime::SetElementText(const std::string &document, const std::string &id, const std::string &text)
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        if (!element) return false;
        element->SetInnerRML(text);
        return true;
    }

    std::string RmlUiRuntime::GetElementText(const std::string &document, const std::string &id) const
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        return element ? element->GetInnerRML() : std::string{};
    }

    bool RmlUiRuntime::SetElementAttribute(const std::string &document, const std::string &id,
                                           const std::string &name, const std::string &value)
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        if (!element) return false;
        element->SetAttribute(name, value);
        return true;
    }

    std::string RmlUiRuntime::GetElementAttribute(const std::string &document, const std::string &id,
                                                  const std::string &name) const
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        return element ? element->GetAttribute<Rml::String>(name, {}) : std::string{};
    }

    bool RmlUiRuntime::SetElementClass(const std::string &document, const std::string &id,
                                       const std::string &name, bool enabled)
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        if (!element) return false;
        element->SetClass(name, enabled);
        return true;
    }

    bool RmlUiRuntime::SetElementStyle(const std::string &document, const std::string &id,
                                       const std::string &name, const std::string &value)
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        return element && element->SetProperty(name, value);
    }

    bool RmlUiRuntime::SubscribeEvent(const std::string &document, const std::string &id, const std::string &event)
    {
        const std::string key = EventKey(document, id, event);
        m_eventSubscriptions.insert(key);
        AttachEventSubscriptions();
        return m_attachedEvents.contains(key);
    }

    bool RmlUiRuntime::ConsumeEvent(const std::string &document, const std::string &id, const std::string &event)
    {
        const std::string key = EventKey(document, id, event);
        auto found = m_pendingEvents.find(key);
        if (found == m_pendingEvents.end() || found->second <= 0)
            return false;
        --found->second;
        return true;
    }

    void RmlUiRuntime::NotifyEvent(const std::string &key)
    {
        ++m_pendingEvents[key];
    }

    void RmlUiRuntime::NotifyEventListenerDetached(const std::string &key, Rml::Element *element)
    {
        const auto found = m_eventListenerElements.find(key);
        if (found != m_eventListenerElements.end() && found->second == element)
        {
            found->second = nullptr;
            m_attachedEvents.erase(key);
        }
    }

    void RmlUiRuntime::AttachEventSubscriptions()
    {
        for (const auto &key : m_eventSubscriptions)
        {
            if (m_attachedEvents.contains(key))
                continue;
            const auto first = key.find(kEventSeparator);
            const auto second = key.find(kEventSeparator, first + 1);
            if (first == std::string::npos || second == std::string::npos)
                continue;
            const std::string document = key.substr(0, first);
            const std::string id = key.substr(first + 1, second - first - 1);
            const std::string event = key.substr(second + 1);
            auto *doc = FindDocument(document);
            auto *element = doc ? doc->GetElementById(id) : nullptr;
            if (!element)
                continue;

            auto listener = m_eventListeners.find(key);
            if (listener == m_eventListeners.end())
            {
                listener = m_eventListeners.emplace(
                    key, std::make_unique<RuntimeEventListener>(*this, key)).first;
            }
            m_eventListenerElements[key] = element;
            element->AddEventListener(event, listener->second.get());
            m_attachedEvents.insert(key);
        }
    }

    void RmlUiRuntime::DetachEventSubscriptions()
    {
        for (const auto &[key, listener] : m_eventListeners)
        {
            const auto first = key.find(kEventSeparator);
            const auto second = key.find(kEventSeparator, first + 1);
            if (first == std::string::npos || second == std::string::npos)
                continue;

            const std::string event = key.substr(second + 1);
            const auto attachedElement = m_eventListenerElements.find(key);
            auto *element = attachedElement == m_eventListenerElements.end()
                                ? nullptr
                                : attachedElement->second;
            if (element)
                element->RemoveEventListener(event, listener.get());
        }
        m_eventListeners.clear();
        m_eventListenerElements.clear();
        m_attachedEvents.clear();
    }

    bool RmlUiRuntime::IsPointerInputCaptured() const
    {
        return m_context && m_context->IsMouseInteracting();
    }

    bool RmlUiRuntime::IsKeyboardInputCaptured() const
    {
        if (!m_context)
            return false;

        const auto *focusedElement = m_context->GetFocusElement();
        return focusedElement && focusedElement->IsVisible(true);
    }

    void RmlUiRuntime::ProcessInput(platform::Window &window, const scene::Scene &scene)
    {
        auto *glfwWindow = static_cast<GLFWwindow *>(window.GetWindow());
        if (!glfwWindow || !window.IsScriptInputEnabled())
            return;

        const auto &input = window.GetInputState();
        const int modifiers = CurrentModifiers(glfwWindow);

        int mouseX = static_cast<int>(input.mouseState.x);
        int mouseY = static_cast<int>(input.mouseState.y);
        glm::vec2 overrideCanvasSize{};
        glm::vec2 overrideMousePosition{};
        bool pointerInside = true;
        if (scene.GetRuntimeUIInputOverride(
                overrideCanvasSize, overrideMousePosition, pointerInside))
        {
            if (pointerInside)
            {
                mouseX = static_cast<int>(std::lround(overrideMousePosition.x));
                // Native runtime UI uses a bottom-left origin. RmlUi uses the
                // window/HTML convention with its origin at the top-left.
                mouseY = static_cast<int>(std::lround(
                    overrideCanvasSize.y - overrideMousePosition.y));
            }
            else
            {
                mouseX = -1;
                mouseY = -1;
            }
        }
        m_context->ProcessMouseMove(mouseX, mouseY, modifiers);

        for (int button = 0; button < 8; ++button)
        {
            if (input.IsMouseButtonPressed(static_cast<std::uint16_t>(button)))
                m_context->ProcessMouseButtonDown(button, modifiers);
            if (input.IsMouseButtonReleased(static_cast<std::uint16_t>(button)))
                m_context->ProcessMouseButtonUp(button, modifiers);
        }
        if (input.mouseState.scrollDeltaX != 0.0 || input.mouseState.scrollDeltaY != 0.0)
        {
            m_context->ProcessMouseWheel(
                {-static_cast<float>(input.mouseState.scrollDeltaX),
                 -static_cast<float>(input.mouseState.scrollDeltaY)},
                modifiers);
        }

        for (int key = 0; key < static_cast<int>(input.keys.size()); ++key)
        {
            const auto identifier = RmlGLFW::ConvertKey(key);
            if (identifier == Rml::Input::KI_UNKNOWN)
                continue;
            if (input.keys[key] && !input.previousKeys[key])
                m_context->ProcessKeyDown(identifier, modifiers);
            if (!input.keys[key] && input.previousKeys[key])
                m_context->ProcessKeyUp(identifier, modifiers);
        }
        for (const auto codepoint : input.textInput)
            m_context->ProcessTextInput(static_cast<Rml::Character>(codepoint));
    }

    void RmlUiRuntime::Render(const scene::Scene &scene, int width, int height, std::uint64_t frameSequence)
    {
        auto &window = core::Engine::GetInstance().GetWindow();
        if (!Initialize(window))
            return;

        if (width != m_width || height != m_height)
        {
            m_width = width;
            m_height = height;
            m_context->SetDimensions({width, height});
            m_renderer->SetViewport(width, height);
        }

        SynchronizeDocuments(scene);
        if (m_documents.empty())
            return;

        if (frameSequence != m_lastInputFrame)
        {
            ProcessInput(window, scene);
            m_context->Update();
            m_lastInputFrame = frameSequence;
        }

        m_renderer->SetViewport(width, height);
        m_renderer->BeginFrame();
        PlutoGE_CopyRmlUiBackdrop(width, height);
        m_context->Render();
        m_renderer->EndFrame();
    }
}
