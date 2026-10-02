#include "PlutoGE/ui/SourceTextEditor.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char *message)
{ if (!condition) throw std::runtime_error(message); }

int main() try
{
    ImGui::CreateContext();
    struct ContextScope { ~ContextScope() { ImGui::DestroyContext(); } } contextScope;
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {640, 480};
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    std::string source = "<rml><body/></rml>";
    ImGuiID id = 0;
    const auto frame = [&](bool focus)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({600, 400});
        ImGui::Begin("Source editor fixture");
        id = ImGui::GetID("source");
        if (focus) ImGui::SetKeyboardFocusHere();
        PlutoGE::ui::EditSourceText("source", source, {500, 250});
        ImGui::End();
        ImGui::Render();
    };
    frame(true);
    frame(false);
    Check(ImGui::GetInputTextState(id) != nullptr, "Source widget was not active");
    const auto original = source;
    io.AddInputCharactersUTF8("!");
    frame(false);
    Check(source != original && source.find('!') != std::string::npos, "Typing did not update caller source");
    source = original; // Session undo while the source widget remains active.
    frame(false);
    Check(source == original && ImGui::GetInputTextState(id)->GetText() == source, "Active widget overwrote session undo");
    source = std::string(8192, 'x'); // External reload with a different capacity.
    frame(false);
    Check(source.size() == 8192 && ImGui::GetInputTextState(id)->TextLen == 8192, "External reload did not resize active widget");
    source.clear();
    frame(false);
    Check(source.empty() && ImGui::GetInputTextState(id)->TextLen == 0, "Empty replacement was not reflected");
    std::cout << "Source text widget editing and external-update regressions passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
