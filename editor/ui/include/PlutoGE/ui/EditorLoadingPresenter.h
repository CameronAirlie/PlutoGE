#pragma once
#include "PlutoGE/core/SceneLoading.h"
#include "PlutoGE/render/LoadingScreenStyle.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include "PlutoGE/ui/EditorViewportOverlay.h"
#include <memory>

namespace PlutoGE::core { class Engine; class LoadingScreenSession; }
namespace PlutoGE::ui
{
    class PanelManager;

    // Scoped host adapter. Call only between completed editor frames. The engine
    // loading lifecycle remains unaware of editor panels and ImGui.
    class EditorLoadingPresenter
    {
    public:
        EditorLoadingPresenter(core::Engine &engine, PanelManager &panels,
                               EditorViewportRegion region, render::LoadingScreenStyle style);
        ~EditorLoadingPresenter();
        EditorLoadingPresenter(const EditorLoadingPresenter &) = delete;
        EditorLoadingPresenter &operator=(const EditorLoadingPresenter &) = delete;
        void Present(const core::SceneLoadStatus &status);
    private:
        core::Engine &m_engine;
        PanelManager &m_panels;
        EditorViewportRegion m_region;
        std::unique_ptr<core::LoadingScreenSession> m_session;
        EditorTextureHandle m_texture;
        bool m_scriptInputEnabled;
    };
}
