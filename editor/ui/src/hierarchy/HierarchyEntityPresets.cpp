#include "PlutoGE/ui/hierarchy/HierarchyEntityPresets.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

#include <array>
#include <memory>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        scene::Entity *AddEntity(scene::Scene &scene, scene::Entity *parent, std::string name,
                                 const glm::vec3 &position = glm::vec3(0.0f))
        {
            auto entity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = std::move(name)});
            entity->SetPosition(position);
            return scene.AddEntity(std::move(entity), parent);
        }

        // Adds `T` to a freshly created entity; returns the entity either way.
        template <typename T>
        scene::Entity *AddEntityWith(const EntityPresetContext &context, const char *name)
        {
            auto *entity = AddEntity(context.scene, context.parent, name);
            if (entity)
            {
                entity->CreateComponent<T>();
            }
            return entity;
        }

        scene::Entity *CreateEmpty(const EntityPresetContext &context)
        {
            return AddEntity(context.scene, context.parent, "New Entity");
        }

        scene::Entity *CreateCube(const EntityPresetContext &context)
        {
            auto *entity = AddEntity(context.scene, context.parent, "Cube");
            if (!entity)
            {
                return nullptr;
            }
            const std::string meshReference(assets::Project::kBuiltinCubeMeshReference);
            const std::string materialReference(assets::Project::kBuiltinDefaultShadedMaterialReference);
            if (auto *meshComponent = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                    .mesh = context.assets.LoadMeshAsset(meshReference),
                    .material = context.assets.LoadMaterialAsset(materialReference),
                }))
            {
                meshComponent->SetSourceMeshPath(meshReference);
                meshComponent->SetMaterialAssetForMaterialSlot(0, materialReference);
            }
            return entity;
        }

        scene::Entity *CreateCarHierarchy(const EntityPresetContext &context)
        {
            auto *car = AddEntity(context.scene, context.parent, "Car");
            if (!car)
            {
                return nullptr;
            }
            AddEntity(context.scene, car, "Chassis", glm::vec3(0.0f, 0.6f, 0.0f));
            // Match car_scene: forward is -Z and left is +X.
            constexpr std::array anchorNames = {"W_FL", "W_FR", "W_RL", "W_RR"};
            constexpr std::array visualNames = {"WV_FL", "WV_FR", "WV_RL", "WV_RR"};
            const std::array positions = {glm::vec3(1.5f, 0.0f, -2.7f), glm::vec3(-1.5f, 0.0f, -2.7f),
                                          glm::vec3(1.5f, 0.0f, 2.5f), glm::vec3(-1.5f, 0.0f, 2.5f)};
            for (std::size_t i = 0; i < anchorNames.size(); ++i)
            {
                if (auto *anchor = AddEntity(context.scene, car, anchorNames[i], positions[i]))
                {
                    AddEntity(context.scene, anchor, visualNames[i]);
                }
            }
            AddEntity(context.scene, car, "CentreOfMass", glm::vec3(0.0f, -0.35f, -1.25f));
            AddEntity(context.scene, car, "CameraTarget");
            return car;
        }

        scene::Entity *CreateCamera(const EntityPresetContext &context)
        {
            auto *entity = AddEntity(context.scene, context.parent, "Camera", glm::vec3(0.0f, 2.0f, 5.0f));
            if (!entity)
            {
                return nullptr;
            }
            entity->SetRotation(glm::vec3(-15.0f, 0.0f, 0.0f));
            if (auto *cameraComponent = entity->CreateComponent<scene::CameraComponent>(new render::Camera(render::CameraConfig{
                    .fovY = 60.0f,
                    .nearPlane = 0.1f,
                    .farPlane = 100.0f,
                })))
            {
                cameraComponent->SetMainCamera(!context.sceneHasCamera);
            }
            return entity;
        }

        scene::Entity *CreateDirectionalLight(const EntityPresetContext &context)
        {
            auto *entity = AddEntity(context.scene, context.parent, "Directional Light");
            if (auto *light = entity ? entity->CreateComponent<scene::LightComponent>() : nullptr)
            {
                light->SetLightType(scene::LightType::Directional);
                light->SetIntensity(4.0f);
                light->SetDirection(glm::normalize(glm::vec3(-0.45f, -0.85f, -0.25f)));
                light->SetCastsShadows(true);
            }
            return entity;
        }

        scene::Entity *CreatePointLight(const EntityPresetContext &context)
        {
            auto *entity = AddEntity(context.scene, context.parent, "Point Light", glm::vec3(0.0f, 2.0f, 0.0f));
            if (auto *light = entity ? entity->CreateComponent<scene::LightComponent>() : nullptr)
            {
                light->SetLightType(scene::LightType::Point);
                light->SetIntensity(8.0f);
            }
            return entity;
        }

        scene::Entity *CreateSky(const EntityPresetContext &context)
        {
            auto *entity = AddEntityWith<scene::PhysicalSkyComponent>(context, "Sky");
            if (entity)
            {
                entity->CreateComponent<scene::VolumetricCloudComponent>();
            }
            return entity;
        }

        scene::Entity *CreateTerrain(const EntityPresetContext &context)
        {
            auto *entity = AddEntity(context.scene, context.parent, "Terrain");
            if (!entity)
            {
                return nullptr;
            }
            const std::string materialReference(assets::Project::kBuiltinDefaultShadedMaterialReference);
            if (auto *terrain = entity->CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{
                    .material = context.assets.LoadMaterialAsset(materialReference),
                }))
            {
                terrain->SetMaterialAssetReference(materialReference);
            }
            return entity;
        }

        constexpr std::array kPresets{
            EntityPresetDefinition{"Empty Entity", "Create Empty Entity", false, &CreateEmpty},
            EntityPresetDefinition{"Cube", "Create Cube", false, &CreateCube},
            EntityPresetDefinition{"Car Hierarchy", "Create Car Hierarchy", true, &CreateCarHierarchy},
            EntityPresetDefinition{"Camera", "Create Camera", false, &CreateCamera},
            EntityPresetDefinition{"Directional Light", "Create Directional Light", false, &CreateDirectionalLight},
            EntityPresetDefinition{"Point Light", "Create Point Light", false, &CreatePointLight},
            EntityPresetDefinition{"Sky", "Create Sky", true, &CreateSky},
            EntityPresetDefinition{"Ocean", "Create Ocean", false,
                                   [](const EntityPresetContext &c) { return AddEntityWith<scene::OceanComponent>(c, "Ocean"); }},
            EntityPresetDefinition{"Terrain", "Create Terrain", false, &CreateTerrain},
            EntityPresetDefinition{"Cloth", "Create Cloth", false,
                                   [](const EntityPresetContext &c) { return AddEntityWith<scene::ClothComponent>(c, "Cloth"); }},
            EntityPresetDefinition{"Particle System", "Create Particle System", false,
                                   [](const EntityPresetContext &c) { return AddEntityWith<scene::ParticleSystemComponent>(c, "Particle System"); }},
            EntityPresetDefinition{"Ambient Light Volume", "Create Ambient Light Volume", false,
                                   [](const EntityPresetContext &c) { return AddEntityWith<scene::AmbientLightVolumeComponent>(c, "Ambient Light Volume"); }},
            EntityPresetDefinition{"IBL Capture", "Create IBL Capture", false,
                                   [](const EntityPresetContext &c) { return AddEntityWith<scene::IblCaptureComponent>(c, "IBL Capture"); }},
        };

        bool HasCamera(const scene::Entity &entity)
        {
            if (entity.HasComponent<scene::CameraComponent>())
            {
                return true;
            }
            for (const auto *child : entity.GetChildren())
            {
                if (child && HasCamera(*child))
                {
                    return true;
                }
            }
            return false;
        }
    }

    std::span<const EntityPresetDefinition> GetEntityPresets()
    {
        return kPresets;
    }

    bool SceneHasAnyCamera(const scene::Scene &scene)
    {
        for (const auto *root : scene.GetRootEntities())
        {
            if (root && HasCamera(*root))
            {
                return true;
            }
        }
        return false;
    }
}
