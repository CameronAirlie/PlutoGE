#pragma once

#include "PlutoGE/render/ImpostorLayout.h"

namespace PlutoGE::render
{
    class Texture;

    // A mesh's octahedral impostor as producers hand it to the renderer: its layout (ImpostorLayout.h) and its two
    // atlas textures (albedo in sRGB, normal-depth linear). Owned by the producer, which keeps it alive with the mesh.
    struct MeshImpostor
    {
        impostor::Parameters parameters;
        const Texture *albedo = nullptr;
        const Texture *normalDepth = nullptr;
    };
}
