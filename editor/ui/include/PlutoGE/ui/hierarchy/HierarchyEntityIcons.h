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
}
