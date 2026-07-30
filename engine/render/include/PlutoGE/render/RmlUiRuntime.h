#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Rml
{
    class Context;
    class ElementDocument;
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

    private:
        RmlUiRuntime() = default;
        void SynchronizeDocuments(const scene::Scene &scene);
        void ProcessInput(platform::Window &window);

        std::unique_ptr<RenderInterface_GL3> m_renderer;
        std::unique_ptr<SystemInterface_GLFW> m_system;
        Rml::Context *m_context = nullptr;
        platform::Window *m_window = nullptr;
        std::unordered_map<std::string, Rml::ElementDocument *> m_documents;
        int m_width = 0;
        int m_height = 0;
        std::uint64_t m_lastInputFrame = 0;
    };
}
