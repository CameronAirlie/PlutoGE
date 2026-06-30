#pragma once

#include <GraphEditor.h>
#include <imgui.h>

namespace PlutoGE::ui::graph_editor_panel
{
    inline void ShowWithCanvasWheelGuard(GraphEditor::Delegate &delegate,
                                         const GraphEditor::Options &options,
                                         GraphEditor::ViewState &viewState,
                                         bool enabled,
                                         GraphEditor::FitOnScreen *fit)
    {
        const ImVec2 canvasScreenPos = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        const ImRect canvasRect(canvasScreenPos, ImVec2(canvasScreenPos.x + canvasSize.x, canvasScreenPos.y + canvasSize.y));

        ImGuiIO &io = ImGui::GetIO();
        const float savedMouseWheel = io.MouseWheel;
        const float savedMouseWheelH = io.MouseWheelH;
        if (!canvasRect.Contains(io.MousePos))
        {
            io.MouseWheel = 0.0f;
            io.MouseWheelH = 0.0f;
        }

        GraphEditor::Show(delegate, options, viewState, enabled, fit);

        io.MouseWheel = savedMouseWheel;
        io.MouseWheelH = savedMouseWheelH;
    }
}
