#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <imgui.h>

namespace PlutoGE::ui
{
    bool Panel::BeginPanel()
    {
        return ImGui::Begin(m_config.name.c_str(), &m_isOpen, ImGuiWindowFlags_None);
    }

    void Panel::EndPanel()
    {
        ImGui::End();
    }

    void Panel::Update()
    {
        const bool shouldRender = BeginPanel();
        if (shouldRender)
        {
            Render();
        }
        EndPanel();
    }
}