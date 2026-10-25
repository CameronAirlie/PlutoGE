#pragma once

#include <span>

namespace PlutoGE::assets
{
    class AssetManager;
}

namespace PlutoGE::scene
{
    class Entity;
    class Scene;
}

namespace PlutoGE::ui
{
    struct EntityPresetContext
    {
        scene::Scene &scene;
        scene::Entity *parent = nullptr; // Null creates a scene root.
        assets::AssetManager &assets;
        bool sceneHasCamera = false; // The first camera becomes the main camera.
    };

    // An entry in the hierarchy's Create menu. `create` runs inside an undoable
    // scene edit and returns the new root entity (null on failure).
    struct EntityPresetDefinition
    {
        const char *menuLabel;
        const char *editLabel;
        bool separatorBefore;
        scene::Entity *(*create)(const EntityPresetContext &context);
    };

    // Menu order. Add a preset by appending a definition in HierarchyEntityPresets.cpp.
    [[nodiscard]] std::span<const EntityPresetDefinition> GetEntityPresets();

    [[nodiscard]] bool SceneHasAnyCamera(const scene::Scene &scene);
}
