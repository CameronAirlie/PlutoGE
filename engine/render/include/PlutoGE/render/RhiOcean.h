#pragma once
#include "PlutoGE/render/BasicRenderer.h"
namespace PlutoGE::scene { class Scene; class CameraTagFilter; }
namespace PlutoGE::render
{
    std::vector<BasicPostProcessEffect> CollectRhiOceans(const scene::Scene &scene, const BasicLighting &lighting,
                                                         const scene::CameraTagFilter *filter = nullptr);
}
