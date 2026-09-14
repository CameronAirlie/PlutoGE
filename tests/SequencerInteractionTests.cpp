#include "PlutoGE/ui/SequencerTimelineView.h"
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
int main() try
{
    using namespace PlutoGE;
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.0f / 60;
    unsigned char *pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    scene::Scene scene;
    scene::Timeline data;
    data.duration = 3;
    data.tracks = {{1, scene::TimelineChannel::Position, scene::TimelineInterpolation::Linear, {}},
                   {1, scene::TimelineChannel::Scale, scene::TimelineInterpolation::Linear, {{0, {1, 1, 1}, {}}}}};
    int selected = -1, key = -1, dragTrack = -1, dragKey = -1, current = 0, first = 0;
    bool dirty = false, expanded = true;
    ImVec2 origin;
    auto frame = [&]()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({780, 560});
        ImGui::Begin("Sequencer test", nullptr, ImGuiWindowFlags_NoDecoration);
        ui::SequencerTimelineView view(data, scene, selected, key, dragTrack, dragKey, dirty);
        ImSequencer::Sequencer(&view, &current, &expanded, &selected, &first, ImSequencer::SEQUENCER_CHANGE_FRAME);
        for (auto *window : ImGui::GetCurrentContext()->Windows)
            if (window->ChildId == 889) origin = window->DC.CursorStartPos;
        ImGui::End();
        ImGui::Render();
        if (!io.MouseDown[0]) dragTrack = dragKey = -1;
    };
    frame(); frame();
    auto click = [&](ImVec2 position)
    {
        io.AddMousePosEvent(position.x, position.y); frame();
        io.AddMouseButtonEvent(0, true); frame();
        Check(ImGui::GetCurrentContext()->ActiveId != 0, "Test did not exercise the active canvas button");
        io.AddMouseButtonEvent(0, false); frame();
    };
    click({origin.x + 30, origin.y + 10});
    Check(selected == 0 && key == -1, "Empty track label did not select");
    click({origin.x + 320, origin.y + 30});
    Check(selected == 1 && key == -1, "Channel background did not select");
    click({origin.x + 320, origin.y + 10});
    Check(selected == 0 && key == -1, "Empty channel background did not select");
    click({origin.x + 210, origin.y + 30});
    Check(selected == 1 && key == 0, "Key diamond did not select through canvas ActiveId");
    Check(!dirty, "Selecting tracks or keys modified timeline data");
    ImGui::DestroyContext();
    std::cout << "Actual ImSequencer mouse selection passed for labels, channels and keys\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
