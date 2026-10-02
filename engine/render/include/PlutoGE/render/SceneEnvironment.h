#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"

#include <span>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
    struct Light;
}

namespace PlutoGE::render
{
    // Shared scene lighting and atmosphere preparation for editor and built games.
    BasicLighting BuildSceneLighting(const CameraData &cameraData, const scene::Scene *scene);
    // As above, but only `lights` illuminate the view (for tag-filtered cameras).
    // The scene still supplies sky and atmosphere.
    BasicLighting BuildSceneLighting(const CameraData &cameraData, const scene::Scene *scene,
                                     std::span<scene::Light *const> lights);
    std::vector<BasicPostProcessEffect> BuildSceneAtmosphere(const scene::Scene *scene,
                                                           const BasicLighting &lighting);
}
