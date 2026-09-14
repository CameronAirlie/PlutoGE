#pragma once
#include "PlutoGE/render/BasicRenderer.h"
namespace PlutoGE::scene { class Scene; }
namespace PlutoGE::render
{
    std::vector<BasicPostProcessEffect> CollectRhiOceans(const scene::Scene &scene, const BasicLighting &lighting);
}
