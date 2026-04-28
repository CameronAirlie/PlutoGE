#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <imgui.h>

namespace PlutoGE::ui
{
    void Panel::BeginPanel()
    {
        ImGui::Begin(m_config.name.c_str(), &m_isOpen, ImGuiWindowFlags_None);
    }

    void Panel::EndPanel()
    {
        ImGui::End();
    }

    void Panel::Update()
    {
        BeginPanel();
        Render();
        EndPanel();
    }
}