#pragma once

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::ui
{
    // Glyph that best describes the entity, chosen from an ordered rule table
    // in HierarchyEntityIcons.cpp: the first matching rule wins, so more
    // specific components (cameras, lights, environment) precede generic ones
    // (prefab roots, meshes). Never null; plain entities get a neutral glyph.
    [[nodiscard]] const char *GetHierarchyEntityIcon(const scene::Entity &entity);

    // Whether the editor viewport marks the entity with its icon: cameras,
    // lights, volumes, audio and the like have no geometry of their own to see
    // or click. A visible mesh, terrain or ocean on the entity already does.
    [[nodiscard]] bool HasViewportEntityIcon(const scene::Entity &entity);
}
