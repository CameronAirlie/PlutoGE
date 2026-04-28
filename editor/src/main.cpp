#include "PlutoGE/ui/EditorShell.h"

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>

int main(int argc, char **argv)
{
    auto &editor = PlutoGE::ui::EditorShell::GetInstance();

    editor.Initialize();

    editor.Render();

    editor.Shutdown();

    return 0;
}