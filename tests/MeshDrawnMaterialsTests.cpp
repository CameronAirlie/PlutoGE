// MeshComponent::GetDrawnMaterials: a per-instance edit (the scripting Color/Emission tint) must reach every material
// a mesh draws with, including submesh overrides and asset-backed slots, without touching shared materials.
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;

namespace
{
    void Check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    // Three triangles: submesh 0 and 2 use slot 0, submesh 1 uses slot 1.
    render::MeshConfig ThreeSubmeshes()
    {
        render::MeshConfig config;
        for (int i = 0; i < 9; ++i)
        {
            render::MeshVertexData vertex{};
            vertex.position = {static_cast<float>(i % 3), static_cast<float>(i / 3), 0.0f};
            vertex.normal = {0.0f, 0.0f, 1.0f};
            config.data.vertices.push_back(vertex);
            config.data.indices.push_back(static_cast<uint32_t>(i));
        }
        config.submeshes = {
            render::Submesh{.indexOffset = 0, .indexCount = 3, .materialIndex = 0},
            render::Submesh{.indexOffset = 3, .indexCount = 3, .materialIndex = 1},
            render::Submesh{.indexOffset = 6, .indexCount = 3, .materialIndex = 0},
        };
        return config;
    }

    bool Contains(const std::vector<render::Material *> &materials, const render::Material *material)
    {
        return std::find(materials.begin(), materials.end(), material) != materials.end();
    }
}

int main()
try
{
    render::Mesh mesh(ThreeSubmeshes());
    render::Material inlineSlot0(render::MaterialConfig{.color = {1, 1, 1, 1}});
    render::Material sharedAsset(render::MaterialConfig{.color = {0.5f, 0.5f, 0.5f, 0.25f}});
    render::Material sharedOverride(render::MaterialConfig{.color = {0.1f, 0.2f, 0.3f, 1}});

    scene::MeshComponent component(scene::MeshComponentConfig{.mesh = &mesh, .materials = {&inlineSlot0, &sharedAsset}});
    component.SetMaterialAssetForMaterialSlot(1, "project://Shared.plutomaterial");
    component.SetMaterialForSubmesh(2, &sharedOverride);
    component.SetMaterialAssetForSubmesh(2, "project://Override.plutomaterial");

    const auto before = component.GetDrawnMaterials(false);
    Check(before.size() == 3, "Three distinct materials are drawn (slot 0, slot 1, submesh 2's override)");
    Check(Contains(before, &inlineSlot0) && Contains(before, &sharedAsset) && Contains(before, &sharedOverride),
          "Without ownership the drawn materials are the assigned ones");

    const auto owned = component.GetDrawnMaterials(true);
    Check(owned.size() == 3, "Owning keeps one material per drawn source");
    Check(Contains(owned, &inlineSlot0), "An inline slot material is already the instance's own and is kept");
    Check(!Contains(owned, &sharedAsset) && !Contains(owned, &sharedOverride),
          "Asset-backed slot and submesh-override materials are replaced by instance copies");
    Check(component.GetMaterialAssetForMaterialSlot(1).empty() && component.GetMaterialAssetForSubmesh(2).empty(),
          "The copies no longer claim to be the shared assets");
    Check(component.GetMaterialForSubmesh(2) != &sharedOverride && Contains(owned, component.GetMaterialForSubmesh(2)),
          "Submesh 2 draws with its own copy of the override");

    for (auto *material : owned)
        material->SetColor({1.0f, 0.0f, 0.0f, material->GetConfig().color.a});
    for (std::size_t submesh = 0; submesh < 3; ++submesh)
        Check(component.GetMaterialForSubmesh(submesh)->GetConfig().color.r == 1.0f &&
                  component.GetMaterialForSubmesh(submesh)->GetConfig().color.g == 0.0f,
              "A tint written to the owned materials reaches every drawn submesh");
    Check(sharedAsset.GetConfig().color.r == 0.5f && sharedOverride.GetConfig().color.r == 0.1f,
          "Shared materials are untouched by the instance's tint");
    Check(component.GetMaterialForMaterialSlot(1)->GetConfig().color.a == 0.25f, "The copy keeps the source's alpha");

    const auto again = component.GetDrawnMaterials(true);
    Check(again == owned, "Owning a second time clones nothing new");

    std::cout << "MeshDrawnMaterialsTests passed\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << "MeshDrawnMaterialsTests failed: " << exception.what() << '\n';
    return 1;
}
