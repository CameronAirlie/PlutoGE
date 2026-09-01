#include "PlutoGE/ui/EditorShell.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

int RunEditor(int argc, char **argv)
{
#ifdef _WIN32
    std::ofstream startupLog(std::filesystem::path(argv[0]).parent_path() / "PlutoGEEditor.log",
                             std::ios::out | std::ios::trunc);
    auto *previousErrorBuffer = std::cerr.rdbuf(startupLog.rdbuf());
    auto *previousLogBuffer = std::clog.rdbuf(startupLog.rdbuf());
#endif
    auto &editor = PlutoGE::ui::EditorShell::GetInstance();

    const std::filesystem::path startupProject = argc > 1 ? argv[1] : std::filesystem::path{};
    if (!editor.Initialize(startupProject))
    {
        return 1;
    }

    if (argc > 1 && !editor.LoadProjectFromPath(argv[1]))
    {
        std::cerr << "Failed to load project: " << argv[1] << std::endl;
        editor.Shutdown();
        return 1;
    }

    editor.Render();

    editor.Shutdown();

#ifdef _WIN32
    std::cerr.rdbuf(previousErrorBuffer);
    std::clog.rdbuf(previousLogBuffer);
#endif
    return 0;
}

#if defined(_WIN32) && defined(PLUTO_EDITOR_WINDOWED)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return RunEditor(__argc, __argv);
}
#else
int main(int argc, char **argv)
{
    return RunEditor(argc, argv);
}
#endif
