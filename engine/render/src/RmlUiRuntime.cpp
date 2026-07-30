#include "PlutoGE/render/RmlUiRuntime.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <RmlUi/Core.h>
#include <RmlUi_Platform_GLFW.h>
#include <RmlUi_Renderer_GL3.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <unordered_set>

namespace PlutoGE::render
{
    namespace
    {
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
                continue;

            const std::string path = assets.ResolveAssetPath(reference);
            if (path.empty())
                continue;
            if (auto *document = m_context->LoadDocument(path))
            {
                document->Show();
                m_documents.emplace(reference, document);
            }
        }
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
