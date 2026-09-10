#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"

#include <vector>

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::render
{
    // Shared scene lighting and atmosphere preparation for editor and built games.
    BasicLighting BuildSceneLighting(const CameraData &cameraData, const scene::Scene *scene);
    std::vector<BasicPostProcessEffect> BuildSceneAtmosphere(const scene::Scene *scene,
                                                           const BasicLighting &lighting);
}
