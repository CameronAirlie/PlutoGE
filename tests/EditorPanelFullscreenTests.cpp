// Panel fullscreen: a docked panel covers the editor work area, keeps the windows it submits
// above it, and returns to the same dock node with focus when restored. Headless ImGui only.
#include "PlutoGE/ui/panels/Panel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char *what)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAILED: %s\n", what);
            ++g_failures;
        }
    }

    class TestPanel final : public PlutoGE::ui::Panel
    {
    public:
        using Panel::Panel;
        ImVec2 contentSize{};

        void Render() override
        {
            contentSize = ImGui::GetContentRegionAvail();
            // Stands in for the viewport's overlay toolbars, which are separate windows.
            ImGui::SetNextWindowPos(ImGui::GetWindowPos());
            ImGui::Begin("##TestOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextUnformatted("overlay");
            ImGui::End();
        }
    };

    ImGuiID g_dockId = 0;

    void Frame(TestPanel &game, TestPanel &other, bool buildLayout = false)
    {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        // A menu bar shrinks the work area, as the editor title bar does.
        if (ImGui::BeginMainMenuBar())
            ImGui::EndMainMenuBar();
        const ImGuiID dockspace = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);
        if (buildLayout)
        {
            ImGui::DockBuilderRemoveNode(dockspace);
            ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
            ImGuiID left = 0;
            ImGuiID right = ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.3f, &left, nullptr);
            ImGui::DockBuilderDockWindow("Other", left);
            ImGui::DockBuilderDockWindow("Game", right);
            ImGui::DockBuilderFinish(dockspace);
            g_dockId = right;
        }
        other.Update();
        game.Update();
        ImGui::Render();
    }

    int WindowDisplayIndex(const char *name)
    {
        ImGuiWindow *window = ImGui::FindWindowByName(name);
        return window ? ImGui::GetCurrentContext()->Windows.index_from_ptr(
                            ImGui::GetCurrentContext()->Windows.find(window))
                      : -1;
    }
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = ImVec2(1280, 800);
    io.Fonts->Build();

    TestPanel other({"Other"});
    TestPanel game({"Game"});
    Frame(game, other, true);
    for (int i = 0; i < 3; ++i)
        Frame(game, other);

    ImGuiWindow *docked = ImGui::FindWindowByName("Game");
    Check(docked && docked->DockIsActive && docked->DockId == g_dockId, "panel starts docked");
    const ImVec2 dockedSize = game.contentSize;

    game.SetFullscreen(true);
    for (int i = 0; i < 3; ++i)
        Frame(game, other);

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuiWindow *fullscreen = ImGui::FindWindowByName("###PanelFullscreenGame");
    Check(fullscreen && fullscreen->Active, "fullscreen window is submitted");
    Check(fullscreen && fullscreen->Pos.x == viewport->WorkPos.x && fullscreen->Pos.y == viewport->WorkPos.y,
          "fullscreen window starts at the work area origin");
    Check(game.contentSize.x == viewport->WorkSize.x && game.contentSize.y == viewport->WorkSize.y,
          "panel content fills the work area");
    Check(game.contentSize.x > dockedSize.x, "panel content grew");
    Check(viewport->WorkPos.y > viewport->Pos.y, "menu bar stays outside the fullscreen area");
    Check(docked && !docked->Active, "docked window is hidden while fullscreen");
    Check(game.HasKeyboardFocus(), "fullscreen panel has keyboard focus");
    Check(WindowDisplayIndex("##TestOverlay") > WindowDisplayIndex("###PanelFullscreenGame"),
          "panel overlay windows draw above the fullscreen window");

    // Clicking the fullscreen window (focus) must not cover its overlays.
    ImGui::SetWindowFocus("Other");
    Frame(game, other);
    ImGui::SetWindowFocus("###PanelFullscreenGame");
    Frame(game, other);
    Check(WindowDisplayIndex("##TestOverlay") > WindowDisplayIndex("###PanelFullscreenGame"),
          "overlays stay above after refocusing");

    game.SetFullscreen(false);
    for (int i = 0; i < 3; ++i)
        Frame(game, other);
    docked = ImGui::FindWindowByName("Game");
    Check(docked && docked->Active && docked->DockIsActive && docked->DockId == g_dockId, "restored into the same dock node");
    Check(game.contentSize.x == dockedSize.x && game.contentSize.y == dockedSize.y, "restored to the docked size");
    Check(game.HasKeyboardFocus(), "restored panel has keyboard focus");
    fullscreen = ImGui::FindWindowByName("###PanelFullscreenGame");
    Check(!fullscreen || !fullscreen->Active, "fullscreen window is gone after restore");

    // Closing a fullscreen panel restores it, so reopening docks it where it was.
    game.SetFullscreen(true);
    Frame(game, other);
    game.SetOpen(false);
    Frame(game, other);
    Check(!game.IsFullscreen(), "closing leaves fullscreen");
    game.SetOpen(true);
    for (int i = 0; i < 2; ++i)
        Frame(game, other);
    docked = ImGui::FindWindowByName("Game");
    Check(docked && docked->Active && docked->DockId == g_dockId, "reopened panel is docked");

    ImGui::DestroyContext();
    if (g_failures == 0)
        std::puts("Panel fullscreen tests passed.");
    return g_failures == 0 ? 0 : 1;
}
