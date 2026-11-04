#include "PlutoGE/platform/Window.h"

#include <glad/glad.h>
#include "VctProbeCacheChecks.h"
#include "VctEmissionCoverageChecks.h"

#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    PlutoGE::platform::Window window;
    if (!window.Create({
            .title = "PlutoGE VCT shader test",
            .width = 64,
            .height = 64,
            .resizable = false,
            .visible = false,
        }))
    {
        std::cerr << "Failed to create the hidden OpenGL test window.\n";
        return 1;
    }

    if (!window.EnsureOpenGLContextCurrent(true))
    {
        std::cerr << "Failed to prepare the OpenGL dispatch table.\n";
        window.Close();
        return 1;
    }

    if (argc > 1 && std::string_view(argv[1]) == "--vct-coverage")
        return CheckVctEmissionCoverage() ? 0 : 1;

    while (glGetError() != GL_NO_ERROR)
    {
    }

    if (!CheckVctProbeCache() || !CheckVctStationaryLighting() || glGetError() != GL_NO_ERROR)
    {
        std::cerr << "VCT cache GPU validation failed.\n";
        window.Close();
        return 1;
    }
    window.Close();
    return 0;
}
