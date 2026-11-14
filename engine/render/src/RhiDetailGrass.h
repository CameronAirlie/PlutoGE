#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <functional>
#include <memory>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
    class CameraTagFilter;
}

namespace PlutoGE::render
{
    class Material;
    class Texture;

    // What turning a scene's grass into fields needs from the renderer's caches.
    struct RhiDetailGrassResolvers
    {
        // A texture's GPU handle (null while it is not uploaded yet): sRGB colour or linear data.
        std::function<rhi::TextureHandle(const Texture *, bool srgb)> texture;
        // A material's surface as a draw's material fields.
        std::function<std::shared_ptr<const BasicDraw>(const Material *)> material;
    };

    // One field per enabled layer of every enabled DetailGrassComponent on an active entity (accepted by `filter`)
    // whose entity has a terrain: the terrain's drawn surface, its painted layer as the density and its macro albedo
    // as the ground colour (docs/DETAIL_GRASS.md). A layer whose density layer is missing, or whose maps are not
    // uploaded yet, grows nothing this frame rather than everywhere.
    std::vector<BasicDetailGrassField> CollectRhiDetailGrass(const scene::Scene &scene, const scene::CameraTagFilter *filter,
                                                             const RhiDetailGrassResolvers &resolvers);
}
