#pragma once

#include <string_view>

namespace PlutoGE::render
{
    class Material;
    class Mesh;
}

namespace PlutoGE::scene
{
    class Component;
    class Entity;
    class MeshComponent;

    // These factories deliberately live in the engine binary. Components
    // exposed through a shared PlutoGE build must be constructed on the DLL
    // side so their RTTI, type IDs, and ownership all use the same module.
    Component *AddComponentByTypeName(Entity &entity, std::string_view typeName);
    MeshComponent *AddMeshComponent(Entity &entity, render::Mesh *mesh, render::Material *material);
}
