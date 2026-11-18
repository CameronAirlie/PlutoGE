#include "PlutoGE/scene/TerrainDebris.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/TerrainLayers.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        // One material per distinct layer look, kept for the process: debris pieces hold raw material pointers and
        // may outlive the terrain (a streamed tile) that made them. Bounded by the layers a project has.
        render::Material *DebrisMaterial(const render::Material &chunkMaterial, volume::VolumeMaterial layer)
        {
            const auto &config = chunkMaterial.ReadConfig();
            const auto *set = config.terrainLayers.get();
            if (!set || set->layers.empty())
                return const_cast<render::Material *>(&chunkMaterial);
            const auto &surface = set->layers[std::min<std::size_t>(layer, set->layers.size() - 1)];
            char key[256];
            std::snprintf(key, sizeof(key), "%p|%p|%p|%p|%g,%g,%g|%g|%g|%g|%g|%d", static_cast<const void *>(surface.albedoMap),
                          static_cast<const void *>(surface.normalMap), static_cast<const void *>(surface.roughnessMap),
                          static_cast<const void *>(surface.heightMap), surface.tint.x, surface.tint.y, surface.tint.z, surface.tileSize,
                          surface.normalStrength, surface.roughness, surface.metallic, surface.antiTiling ? 1 : 0);
            static std::map<std::string, std::unique_ptr<render::Material>> materials;
            auto &material = materials[key];
            if (!material)
            {
                auto single = std::make_shared<render::TerrainLayerSet>(*set);
                single->layers = {surface};
                single->weightMaps = {};
                single->weights = render::TerrainLayerWeights::VertexLayer;
                single->projection = render::TerrainLayerProjection::Triplanar;
                auto debrisConfig = config;
                debrisConfig.terrainLayers = std::move(single);
                debrisConfig.albedoTexture = nullptr;
                material = std::make_unique<render::Material>(debrisConfig);
            }
            return material.get();
        }

        // Every vertex weighs layer 0 fully and is open: the material has the one layer, and fracture caps (planar UVs)
        // must not read as other layers or as occluded.
        void SingleLayer(render::MeshData &data)
        {
            for (auto &vertex : data.vertices)
            {
                vertex.uv = {0.0f, 1.0f};
                vertex.uv2 = {0.0f, 1.0f};
            }
        }
    }

    std::vector<Entity *> SpawnTerrainDebris(Scene &scene, TerrainVolumeComponent &volume, volume::VolumeMesh removed,
                                             const glm::vec3 &localOrigin, const glm::vec3 &worldImpact, const TerrainDebrisSettings &settings)
    {
        auto *owner = volume.GetOwner();
        auto *chunkMaterial = volume.GetChunkMaterial();
        if (!settings.enabled || !owner || removed.Empty() || !chunkMaterial)
            return {};

        // The rock most of it was.
        std::map<volume::VolumeMaterial, int> counts;
        for (const auto material : removed.materials)
            ++counts[material];
        const auto main = counts.empty() ? volume::VolumeMaterial{0}
                                         : std::max_element(counts.begin(), counts.end(), [](const auto &a, const auto &b) { return a.second < b.second; })->first;
        SingleLayer(removed.mesh);

        auto source = std::make_unique<Entity>(EntityConfig{.name = "Terrain Debris"});
        source->SetTransient(true);
        const glm::mat4 world = owner->GetWorldTransform() * glm::translate(glm::mat4(1.0f), localOrigin);
        Transform transform;
        transform.position = glm::vec3(world[3]);
        source->SetLocalTransform(transform);
        auto *mesh = source->CreateComponent<MeshComponent>(MeshComponentConfig{});
        auto *material = DebrisMaterial(*chunkMaterial, main);
        mesh->SetMaterial(material);
        if (!mesh->SetRuntimeGeometry(std::move(removed.mesh)))
            return {};
        auto *root = scene.AddEntity(std::move(source));

        auto shatter = settings.shatter;
        shatter.impactPoint = worldImpact;
        auto pieces = Shatter(scene, *root, shatter);
        for (auto *piece : pieces)
            if (auto *pieceMesh = piece ? piece->GetComponent<MeshComponent>() : nullptr; pieceMesh && pieceMesh->GetMesh())
            {
                const auto *geometry = pieceMesh->GetMesh();
                auto data = geometry->GetMeshData();
                std::vector<render::Submesh> submeshes;
                for (std::size_t index = 0; index < geometry->GetSubmeshCount(); ++index)
                    submeshes.push_back(geometry->GetSubmesh(index));
                const std::size_t slots = std::max<std::size_t>(submeshes.size(), 1);
                SingleLayer(data);
                pieceMesh->SetRuntimeGeometry(std::move(data), std::move(submeshes));
                for (std::size_t slot = 0; slot < slots; ++slot)
                    pieceMesh->SetMaterialForMaterialSlot(slot, material);
            }
        scene.DestroyEntity(root->GetID());
        return pieces;
    }
}
