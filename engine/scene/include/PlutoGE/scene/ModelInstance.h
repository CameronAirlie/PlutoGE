#pragma once
#include "PlutoGE/assets/ModelInstanceState.h"
#include <memory>

namespace PlutoGE::assets { class AssetManager; class Project; struct StaticModelGenerationSnapshot; }
namespace PlutoGE::scene
{
    class Scene;
    class Entity;
    struct StaticModelSceneInstance
    {
        assets::StaticModelInstanceState state;
        // Private generation reader outlives every entity borrowing its resources.
        std::shared_ptr<assets::AssetManager> resources;
    };
    // Resource-owning thread. Builds a complete linked tree from verified source
    // evidence and shares its private reader with the scene's existing generation.
    // The project capability must already be active; failures remove the new tree.
    Entity *CreateStaticModelInstance(Scene &scene, const assets::Project &project,
        const assets::StaticModelGenerationSnapshot &generation, assets::AssetManager &sharedAssets, const std::string &name,
        Entity *parent = nullptr, std::string *errorMessage = nullptr);
    // Transfer complete instance mappings between equivalent cloned subtrees.
    // Partial generated-node clones are rejected; callers discard the clone on failure.
    bool CopyStaticModelInstanceLinks(const Entity &source, Entity &clone, std::string *errorMessage = nullptr);
    // Capture current scene edits against the accepted source baseline without
    // changing either. Missing generated entities fail instead of losing linkage.
    bool CaptureStaticModelInstance(const Scene &scene, const StaticModelSceneInstance &instance,
        assets::StaticModelInstanceState &output, std::string *errorMessage = nullptr);
}
