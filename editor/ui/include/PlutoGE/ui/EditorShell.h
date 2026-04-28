#pragma once

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/PanelManager.h"

namespace PlutoGE::ui
{
    class EditorShell
    {
    public:
        void Initialize();
        void Render();
        void Shutdown();

        [[nodiscard]] core::Engine &GetEngine() { return m_engine; }
        [[nodiscard]] PanelManager &GetPanelManager() { return m_panelManager; }

        [[nodiscard]] static EditorShell &GetInstance()
        {
            static EditorShell instance;
            return instance;
        }

    private:
        EditorShell() = default;
        ~EditorShell() = default;

        core::Engine m_engine = core::Engine::GetInstance();
        PanelManager m_panelManager;
    };
}