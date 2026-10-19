#include "PlutoGE/render/RenderCommand.h"

#include <iostream>
#include <stdexcept>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        MeshConfig config;
        config.data.vertices.resize(3);
        config.data.indices = {0, 1, 2, 0, 1, 2, 0, 1, 2};
        Submesh withLods{.indexOffset = 0, .indexCount = 3};
        withLods.lods = {{0, 3}, {3, 3}, {6, 3}};
        Submesh withoutLods{.indexOffset = 0, .indexCount = 3};
        config.submeshes = {withLods, withoutLods};
        Mesh mesh(config);

        RenderCommand command;
        command.mesh = &mesh;
        Require(SelectShadowCasterLod(command) == 0, "Default casters follow the visible LOD");
        command.minShadowLodIndex = 1;
        Require(SelectShadowCasterLod(command) == 1, "Producer floor coarsens shadow casters");
        command.lodIndex = 2;
        Require(SelectShadowCasterLod(command) == 2, "Casters are never finer than the visible LOD");
        command.lodIndex = 0;
        command.minShadowLodIndex = 8;
        Require(SelectShadowCasterLod(command) == 2, "Floors clamp to the submesh's coarsest LOD");
        command.minShadowLodIndex = 0;
        Require(SelectShadowCasterLod(command, 1) == 1, "Backend floors apply like producer floors");
        command.submeshIndex = 1;
        command.minShadowLodIndex = 3;
        Require(SelectShadowCasterLod(command) == 0, "Submeshes without LODs keep LOD0");
        std::cout << "PASS: shadow caster LOD policy\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
