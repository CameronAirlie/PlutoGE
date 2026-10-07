#include "PlutoGE/ui/ViewportOverlayLayout.h"
#include "PlutoGE/ui/EditorIcons.h"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}
int main()
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    using namespace PlutoGE::ui;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    try
    {
        RegisterViewportOverlaySettings();
        auto &scene = GetViewportOverlayLayout("SceneTransform");
        scene.fraction = ImVec2(0.25f, 0.75f);
        scene.customized = true;
        auto &game = GetViewportOverlayLayout("GameView");
        game.fraction = ImVec2(1, 0);
        game.customized = true;
        GetViewportOverlayLayout("DefaultTools");
        SaveViewportOverlayLayout();
        const std::string saved = ImGui::SaveIniSettingsToMemory();
        Require(saved.find("DefaultTools") == std::string::npos, "Default position should not be serialized");
        ImGui::ClearIniSettings();
        ImGui::LoadIniSettingsFromMemory(saved.c_str());
        const auto &restored = GetViewportOverlayLayout("SceneTransform");
        Require(restored.customized && restored.fraction.x == 0.25f && restored.fraction.y == 0.75f,
                "Moved toolbar did not survive a settings round trip");
        Require(GetViewportOverlayLayout("GameView").fraction.x == 1, "Viewport layouts must remain independent");
        GetViewportOverlayLayout("SceneTransform").customized = false;
        const std::string reset = ImGui::SaveIniSettingsToMemory();
        Require(reset.find("SceneTransform") == std::string::npos, "Reset position was still serialized");
        ImGui::LoadIniSettingsFromMemory("[ViewportOverlay][Outside]\nPosition=-2,3\n\n[ViewportOverlay][Invalid]\nPosition=nan,inf\n");
        Require(GetViewportOverlayLayout("Outside").fraction.x == 0 &&
                GetViewportOverlayLayout("Outside").fraction.y == 1, "Out-of-range settings not clamped");
        Require(!GetViewportOverlayLayout("Invalid").customized, "Nonfinite settings accepted");

        // Exercise the shipped library font, including merging into different editor faces.
        auto &io = ImGui::GetIO();
        ImFontConfig defaultConfig{};
        defaultConfig.SizePixels = 16;
        ImFont *faces[3];
        for (int i = 0; i < 3; ++i)
        {
            faces[i] = i == 0 ? io.Fonts->AddFontDefault(&defaultConfig) :
                io.Fonts->AddFontFromFileTTF(i == 1 ? PLUTO_FONT_DIRECTORY "/Georama-Regular.ttf" :
                    PLUTO_FONT_DIRECTORY "/MartianMono-StdRg.ttf", 16);
            Require(faces[i] != nullptr, "Editor face missing");
            ImFontConfig config{};
            config.MergeMode = true;
            Require(io.Fonts->AddFontFromFileTTF(PLUTO_FONT_DIRECTORY "/fa-solid-900.ttf", 16,
                        &config, icons::Ranges) != nullptr, "Icon font could not be merged");
        }
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Require(pixels && width > 0 && height > 0, "Font atlas failed");
        for (auto *face : faces)
            for (int i = 0; icons::Ranges[i]; i += 2)
                Require(face->GetFontBaked(16)->FindGlyphNoFallback(icons::Ranges[i]) != nullptr, "Toolbar library glyph missing");
        ImGui::DestroyContext();
        std::cout << "Viewport overlay persistence and icon atlas checks passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        ImGui::DestroyContext();
        return 1;
    }
}

