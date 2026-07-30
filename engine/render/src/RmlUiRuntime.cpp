#include "PlutoGE/render/RmlUiRuntime.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi_Platform_GLFW.h>
#include <RmlUi_Renderer_GL3.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <unordered_set>

namespace PlutoGE::render
{
    namespace
    {
        constexpr char kEventSeparator = '\x1f';

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

        private:
            RmlUiRuntime &m_owner;
            std::string m_key;
        };

        void CollectDocuments(scene::Entity *entity, std::unordered_set<std::string> &paths)
        {
            if (!entity || !entity->IsActive())
                return;

            if (const auto *canvas = entity->GetComponent<scene::CanvasComponent>();
                canvas && canvas->IsEnabled() &&
                canvas->GetBackend() == scene::UIRenderBackend::RmlUi &&
                !canvas->GetDocumentPath().empty())
            {
                paths.insert(canvas->GetDocumentPath());
                // A document canvas owns its subtree. Nested native canvases are
                // still discovered through their own roots during migration.
            }

            for (auto *child : entity->GetChildren())
                CollectDocuments(child, paths);
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

        m_documents.clear();
        m_documentWriteTimes.clear();
        m_eventListeners.clear();
        m_eventSubscriptions.clear();
        m_attachedEvents.clear();
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
        std::unordered_set<std::string> requestedPaths;
        for (auto *root : scene.GetRootEntities())
            CollectDocuments(root, requestedPaths);

        for (auto it = m_documents.begin(); it != m_documents.end();)
        {
            if (!requestedPaths.contains(it->first))
            {
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
        for (const auto &reference : requestedPaths)
        {
            if (m_documents.contains(reference))
            {
                const std::string resolved = assets.ResolveAssetPath(reference);
                std::error_code error;
                const auto writeTime = DocumentSourceWriteTime(resolved, error);
                const auto known = m_documentWriteTimes.find(reference);
                if (!error && known != m_documentWriteTimes.end() && writeTime != known->second)
                    ReloadDocument(reference);
                continue;
            }

            const std::string path = assets.ResolveAssetPath(reference);
            if (path.empty())
                continue;
            if (auto *document = m_context->LoadDocument(path))
            {
                document->Show();
                m_documents.emplace(reference, document);
                std::error_code error;
                const auto writeTime = DocumentSourceWriteTime(path, error);
                if (!error)
                    m_documentWriteTimes[reference] = writeTime;
            }
        }
        AttachEventSubscriptions();
    }

    Rml::ElementDocument *RmlUiRuntime::FindDocument(const std::string &document) const
    {
        const auto found = m_documents.find(document);
        return found == m_documents.end() ? nullptr : found->second;
    }

    bool RmlUiRuntime::ShowDocument(const std::string &document, bool visible)
    {
        auto *target = FindDocument(document);
        if (!target)
            return false;
        if (visible) target->Show();
        else target->Hide();
        return true;
    }

    bool RmlUiRuntime::ReloadDocument(const std::string &document)
    {
        if (!m_context)
            return false;
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        const std::string path = assets.ResolveAssetPath(document);
        if (path.empty())
            return false;
        if (auto found = m_documents.find(document); found != m_documents.end())
        {
            if (found->second) found->second->Close();
            m_documents.erase(found);
        }
        m_eventListeners.clear();
        m_attachedEvents.clear();
        auto *loaded = m_context->LoadDocument(path);
        if (!loaded)
            return false;
        loaded->Show();
        m_documents[document] = loaded;
        std::error_code error;
        const auto writeTime = DocumentSourceWriteTime(path, error);
        if (!error) m_documentWriteTimes[document] = writeTime;
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
            auto listener = std::make_unique<RuntimeEventListener>(*this, key);
            element->AddEventListener(event, listener.get());
            m_eventListeners.push_back(std::move(listener));
            m_attachedEvents.insert(key);
        }
    }

    bool RmlUiRuntime::IsInputCaptured() const
    {
        return m_context && (m_context->IsMouseInteracting() || m_context->GetFocusElement() != nullptr);
    }

    void RmlUiRuntime::ProcessInput(platform::Window &window)
    {
        auto *glfwWindow = static_cast<GLFWwindow *>(window.GetWindow());
        if (!glfwWindow || !window.IsScriptInputEnabled())
            return;

        const auto &input = window.GetInputState();
        const int modifiers = CurrentModifiers(glfwWindow);
        m_context->ProcessMouseMove(
            static_cast<int>(input.mouseState.x),
            static_cast<int>(input.mouseState.y),
            modifiers);

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
            ProcessInput(window);
            m_context->Update();
            m_lastInputFrame = frameSequence;
        }

        m_renderer->SetViewport(width, height);
        m_renderer->BeginFrame();
        m_context->Render();
        m_renderer->EndFrame();
    }
}
