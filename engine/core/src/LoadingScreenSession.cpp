#include "PlutoGE/core/LoadingScreenSession.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/LoadingScreenAsset.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/render/RmlLoadingDocument.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace PlutoGE::core
{
    namespace
    {
        struct DocumentScope
        {
            Rml::ElementDocument *previous;
            explicit DocumentScope(render::RmlLoadingDocument *document)
                : previous(render::RmlUiRuntime::Get().SetLoadingDocumentTarget(document ? document->GetDocument() : nullptr)) {}
            ~DocumentScope() { render::RmlUiRuntime::Get().SetLoadingDocumentTarget(previous); }
        };
    }
    LoadingScreenSession::LoadingScreenSession(Engine &engine, render::LoadingScreenStyle style, bool runController)
        : m_engine(engine), m_style(std::move(style)), m_started(std::chrono::steady_clock::now()),
          m_previous(m_started), m_runController(runController) {}

    LoadingScreenSession::~LoadingScreenSession() { StopController(); }

    void LoadingScreenSession::StopController() noexcept
    {
        if (!m_controller) return;
        try { DocumentScope scope(m_document.get()); m_controller->OnDestroy(); }
        catch (const std::exception &e) { std::cerr << "Loading controller cleanup: " << e.what() << '\n'; }
        catch (...) {}
        m_controller.reset();
    }

    void LoadingScreenSession::Prepare()
    {
        m_prepared = true;
        if (m_style.assetReference.empty()) return;
        try
        {
            assets::LoadingScreenAsset asset;
            content::InputFile file(m_engine.GetAssetManager().ResolveAssetPath(m_style.assetReference));
            if (!file.is_open() || !assets::ReadLoadingScreenAsset(file, asset))
                throw std::runtime_error("Invalid loading-screen asset: " + m_style.assetReference);
            m_document = render::RmlUiRuntime::Get().CreateLoadingDocument(
                asset.document, m_engine.GetWindow(), *m_engine.GetRenderDevice());
            if (!m_document) throw std::runtime_error("Cannot load RML: " + asset.document);
            if (m_runController && !asset.controller.empty())
            {
                auto &scripts = m_engine.GetScriptEngine();
                const auto *definition = scripts.FindClass(asset.controller);
                if (!definition || std::find(definition->assignableTypeNames.begin(), definition->assignableTypeNames.end(),
                    "PlutoGE.ScriptCore.LoadingScreenController") == definition->assignableTypeNames.end())
                    throw std::runtime_error("Loading controller must derive from LoadingScreenController: " + asset.controller);
                m_controller = scripts.CreateInstance(asset.controller);
                if (!m_controller) throw std::runtime_error("Cannot create loading controller: " + asset.controller);
                DocumentScope scope(m_document.get());
                m_controller->OnCreate();
            }
        }
        catch (const std::exception &e)
        {
            m_error = e.what();
            std::cerr << "Loading screen: " << m_error << '\n';
            StopController();
            // Controller cleanup may hide its document (RmlDocument.Dispose).
            // Keep valid authored UI visible when controller setup fails.
            if (m_document)
            {
                DocumentScope scope(m_document.get());
                render::RmlUiRuntime::Get().ShowDocument("loading://active", true);
            }
        }
    }

    bool LoadingScreenSession::Render(const SceneLoadStatus &status, unsigned width, unsigned height)
    {
        if (!width || !height || !m_engine.GetRenderDevice()) return false;
        if (!m_prepared) Prepare();
        if (!m_renderer.IsInitialized() && !m_renderer.Initialize(*m_engine.GetRenderDevice())) return false;
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - m_started).count();
        const float delta = std::chrono::duration<float>(now - m_previous).count();
        m_previous = now;
        if (m_document)
        {
            try
            {
                DocumentScope scope(m_document.get());
                auto &ui = render::RmlUiRuntime::Get();
                constexpr const char *labels[] = {"Preparing", "Reading scene", "Building scene", "Starting scene", "Ready", "Loading failed"};
                const auto stage = static_cast<unsigned>(status.stage);
                ui.SetElementText("loading://active", "loading-stage", labels[std::min(stage, 5u)]);
                ui.SetElementText("loading://active", "loading-scene", status.path);
                ui.SetElementText("loading://active", "loading-error", status.error);
                if (m_controller) m_controller->OnUpdate(delta);
                if (!m_renderer.Render(width, height, elapsed, stage, m_style, false)) return false;
                if (m_document->Render(GetTexture(), static_cast<int>(width), static_cast<int>(height))) return true;
                throw std::runtime_error("Loading RML context is no longer available");
            }
            catch (const std::exception &e)
            {
                m_error = e.what();
                std::cerr << "Loading screen: " << m_error << '\n';
                m_engine.GetRenderDevice()->GetImmediateContext().RecoverInterruptedFrame();
                StopController();
                m_document.reset();
            }
        }
        return m_renderer.Render(width, height, elapsed, static_cast<unsigned>(status.stage), m_style);
    }

    void LoadingScreenSession::Present(const SceneLoadStatus &status)
    {
        if (m_engine.GetConfig().isEditorHost) throw std::logic_error("Editor loading requires viewport composition");
        auto &window = m_engine.GetWindow();
        window.PollEvents();
        const auto extents = window.GetExtents();
        if (window.ShouldClose() || extents.width <= 0 || extents.height <= 0) return;
        auto *swapchain = m_engine.GetSwapchain();
        if (!swapchain) throw std::runtime_error("Loading screen requires a swapchain");
        const auto width = static_cast<unsigned>(extents.width), height = static_cast<unsigned>(extents.height);
        if (swapchain->GetWidth() != width || swapchain->GetHeight() != height)
            if (!m_engine.GetRhiRenderService().Resize(width, height)) return;
        if (!Render(status, width, height) || !swapchain->Present(GetTexture(),
            m_engine.GetConfig().graphicsApi == render::rhi::GraphicsApi::Vulkan))
            throw std::runtime_error("Could not present loading screen");
    }
}
