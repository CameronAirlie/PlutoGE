#pragma once
#include "PlutoGE/render/LoadingScreenRenderer.h"
#include <chrono>
#include <memory>
#include <string>

namespace PlutoGE::render { class RmlLoadingDocument; }
namespace PlutoGE::scripting { class ScriptInstance; }
namespace PlutoGE::core
{
    class Engine;
    struct SceneLoadStatus;
    // One instance per transition (or editor preview). No scene ownership,
    // gameplay ticking, or editor dependency. Destroy before Engine::Shutdown.
    class LoadingScreenSession
    {
    public:
        LoadingScreenSession(Engine &engine, render::LoadingScreenStyle style, bool runController = true);
        ~LoadingScreenSession();
        bool Render(const SceneLoadStatus &status, unsigned width, unsigned height);
        void Present(const SceneLoadStatus &status);
        render::rhi::TextureHandle GetTexture() const { return m_renderer.GetColorTexture(); }
        const std::string &GetError() const { return m_error; }
    private:
        void Prepare();
        void StopController() noexcept;
        Engine &m_engine;
        render::LoadingScreenStyle m_style;
        render::LoadingScreenRenderer m_renderer;
        std::shared_ptr<render::RmlLoadingDocument> m_document;
        std::unique_ptr<scripting::ScriptInstance> m_controller;
        std::chrono::steady_clock::time_point m_started, m_previous;
        std::string m_error;
        bool m_prepared = false;
        bool m_runController;
    };
}
