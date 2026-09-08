#pragma once
#include "PlutoGE/render/Shader.h"

namespace PlutoGE::render::detail
{
    // Source builders are shared with GPU contract tests; no GL state is needed.
    ShaderSource VctVoxelizationShaderSource();
    ShaderSource VctResolveShaderSource();
}
