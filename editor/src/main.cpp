#include "PlutoGE/ui/EditorShell.h"

#include <cstdlib>
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
    auto &editor = PlutoGE::ui::EditorShell::GetInstance();

    if (!editor.Initialize())
    {
        return 1;
    }

    editor.Render();

    editor.Shutdown();

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
