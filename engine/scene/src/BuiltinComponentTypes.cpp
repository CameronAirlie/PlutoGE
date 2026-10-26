#include "PlutoGE/scene/ComponentRegistry.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ActiveRagdollComponent.h"
#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/AudioEnvironmentVolumeComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/CameraRigComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/DecalComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/IKComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/RagdollTuningComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SequencerComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

namespace PlutoGE::scene
{
    namespace
    {
        constexpr std::string_view kDefaultMaterial = assets::Project::kBuiltinDefaultShadedMaterialReference;

        template <typename Required>
        bool HasSibling(const Entity &entity) { return entity.HasComponent<Required>(); }

        bool SceneHasAnyCamera(const Entity *entity)
        {
            if (!entity)
                return false;
            if (entity->HasComponent<CameraComponent>())
                return true;
            for (const auto *child : entity->GetChildren())
                if (SceneHasAnyCamera(child))
                    return true;
            return false;
        }

        bool SceneHasAnyCamera(const Scene *scene)
        {
            if (!scene)
                return false;
            for (const auto *root : scene->GetRootEntities())
                if (SceneHasAnyCamera(root))
                    return true;
            return false;
        }

        void RegisterRendering(ComponentRegistry &registry)
        {
            auto &mesh = registry.Register<MeshComponent>("MeshComponent", "Mesh Component");
            mesh.create = [] { return std::unique_ptr<Component>(std::make_unique<MeshComponent>(MeshComponentConfig{})); };
            mesh.createForAuthoring = [](Entity &)
            {
                auto component = std::make_unique<MeshComponent>(MeshComponentConfig{
                    .mesh = nullptr,
                    .material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(std::string(kDefaultMaterial)),
                });
                component->SetMaterialAssetForMaterialSlot(0, std::string(kDefaultMaterial));
                return std::unique_ptr<Component>(std::move(component));
            };
            mesh.isAssetPathProperty = [](std::string_view name)
            {
                return name == "SourceMesh" || name == "MeshAssetReference" ||
                       name.ends_with("Path") || name.ends_with("MaterialAsset");
            };

            registry.Register<DecalComponent>("DecalComponent", "Decal Component");

            auto &terrain = registry.Register<TerrainComponent>("TerrainComponent", "Terrain Component");
            terrain.create = [] { return std::unique_ptr<Component>(std::make_unique<TerrainComponent>(TerrainComponentConfig{})); };
            terrain.createForAuthoring = [](Entity &)
            {
                auto component = std::make_unique<TerrainComponent>(TerrainComponentConfig{
                    .material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(std::string(kDefaultMaterial)),
                });
                component->SetMaterialAssetReference(std::string(kDefaultMaterial));
                return std::unique_ptr<Component>(std::move(component));
            };
            terrain.isAssetPathProperty = [](std::string_view name)
            {
                // HeightMap, the layers' Layer.<n>.<Kind>Map textures and the WeightMap<n> files.
                return name == "MaterialAsset" || name == "PaintedAlbedo" ||
                       name.ends_with("Map") || name.starts_with("WeightMap");
            };

            auto &spline = registry.Register<SplineComponent>("SplineComponent", "Spline Track Component");
            spline.create = [] { return std::unique_ptr<Component>(std::make_unique<SplineComponent>(SplineComponentConfig{})); };
            spline.createForAuthoring = [](Entity &)
            {
                auto component = std::make_unique<SplineComponent>(SplineComponentConfig{
                    .material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(std::string(kDefaultMaterial)),
                    .materialAssetReference = std::string(kDefaultMaterial),
                });
                component->Rebuild();
                return std::unique_ptr<Component>(std::move(component));
            };
            spline.isAssetPathProperty = [](std::string_view name) { return name == "MaterialAsset"; };

            registry.Register<OceanComponent>("OceanComponent", "Ocean Component");

            auto &foliage = registry.Register<FoliageComponent>("FoliageComponent", "Foliage Component");
            foliage.createForAuthoring = [](Entity &)
            {
                auto component = std::make_unique<FoliageComponent>();
                component->SetMaterialAssetReference(std::string(kDefaultMaterial));
                return std::unique_ptr<Component>(std::move(component));
            };
            foliage.isAssetPathProperty = [](std::string_view name)
            {
                return name == "SourceMesh" || name == "MaterialAsset" ||
                       name.ends_with(".SourceMesh") || name.ends_with(".MaterialAsset");
            };

            registry.Register<ClothComponent>("ClothComponent", "Cloth Component");

            auto &particles = registry.Register<ParticleSystemComponent>("ParticleSystemComponent", "Particle System Component");
            particles.create = [] { return CreateParticleSystemComponent(); };
            particles.createForAuthoring = [](Entity &) { return std::unique_ptr<Component>(std::make_unique<ParticleSystemComponent>()); };
            particles.isAssetPathProperty = [](std::string_view name) { return name == "MaterialAsset"; };

            auto &camera = registry.Register<CameraComponent>("CameraComponent", "Camera Component");
            camera.create = []
            { return std::unique_ptr<Component>(std::make_unique<CameraComponent>(new render::Camera(render::CameraConfig{}), false)); };
            camera.createForAuthoring = [](Entity &entity)
            {
                const bool sceneAlreadyHasCamera = SceneHasAnyCamera(entity.GetScene());
                auto component = std::make_unique<CameraComponent>(new render::Camera(render::CameraConfig{
                    .fovY = 60.0f,
                    .nearPlane = 0.1f,
                    .farPlane = 100.0f,
                }));
                component->SetMainCamera(!sceneAlreadyHasCamera);
                return std::unique_ptr<Component>(std::move(component));
            };

            auto &cameraRig = registry.Register<CameraRigComponent>("CameraRigComponent", "Camera Rig");
            cameraRig.canAdd = HasSibling<CameraComponent>;
            registry.Register<SequencerComponent>("SequencerComponent", "Sequencer");
            registry.Register<LightComponent>("LightComponent", "Light Component");

            registry.AddMenuItem<MeshComponent>("Rendering/Mesh");
            registry.AddMenuItem<DecalComponent>("Rendering/Decal");
            registry.AddMenuItem<TerrainComponent>("Rendering/Terrain");
            registry.AddMenuItem<SplineComponent>("Rendering/Road / Spline Track");
            registry.AddMenuItem<OceanComponent>("Rendering/Ocean");
            registry.AddMenuItem<FoliageComponent>("Rendering/Foliage");
            registry.AddMenuItem<ClothComponent>("Rendering/Cloth");
            registry.AddMenuItem<ParticleSystemComponent>("Rendering/Particle System");
            registry.AddMenuItem<CameraComponent>("Rendering/Camera");
            registry.AddMenuItem<CameraRigComponent>("Rendering/Camera Rig");
            registry.AddMenuItem<SequencerComponent>("Rendering/Sequencer");
            registry.AddMenuItem<LightComponent>("Rendering/Light");
        }

        void RegisterAnimation(ComponentRegistry &registry)
        {
            auto &animation = registry.Register<AnimationComponent>("AnimationComponent", "Animation Component");
            animation.isAssetPathProperty = [](std::string_view name)
            { return name == "SourceAnimation" || name == "AnimationGraph"; };
            registry.Register<ActiveRagdollComponent>("ActiveRagdollComponent", "Active Ragdoll Component").canAdd =
                HasSibling<AnimationComponent>;
            registry.Register<RagdollTuningComponent>("RagdollTuningComponent", "Ragdoll Tuning Component").canAdd =
                HasSibling<AnimationComponent>;
            registry.Register<IKComponent>("IKComponent", "Two Bone IK Rig").canAdd = HasSibling<AnimationComponent>;
            registry.Register<SkeletonAttachmentComponent>("SkeletonAttachmentComponent", "Skeleton Attachment Component");

            registry.AddMenuItem<AnimationComponent>("Animation/Animation");
            registry.AddMenuItem<ActiveRagdollComponent>("Animation/Active Ragdoll");
            registry.AddMenuItem<RagdollTuningComponent>("Animation/Ragdoll Tuning");
            registry.AddMenuItem<IKComponent>("Animation/Two Bone IK Rig");
            registry.AddMenuItem<SkeletonAttachmentComponent>("Animation/Skeleton Attachment");
        }

        void RegisterPhysicsAndAI(ComponentRegistry &registry)
        {
            registry.Register<RigidbodyComponent>("RigidbodyComponent", "Rigidbody Component");
            auto &collider = registry.Register<ColliderComponent>("ColliderComponent", "Collider Component");
            collider.createForAuthoring = [](Entity &entity)
            {
                const auto shape = entity.HasComponent<TerrainComponent>()  ? ColliderShape::Terrain
                                   : entity.HasComponent<SplineComponent>() ? ColliderShape::Mesh
                                                                            : ColliderShape::Box;
                return std::unique_ptr<Component>(std::make_unique<ColliderComponent>(ColliderComponentConfig{.shape = shape}));
            };
            registry.Register<NavAgentComponent>("NavAgentComponent", "Navigation Agent Component");
            registry.Register<NavigationMeshComponent>("NavigationMeshComponent", "Navigation Mesh Component");

            registry.AddMenuItem<RigidbodyComponent>("Physics/Rigidbody");
            registry.AddMenuItem<ColliderComponent>("Physics/Collider");
            registry.AddMenuItem<NavAgentComponent>("AI/Navigation Agent");
            registry.AddMenuItem<NavigationMeshComponent>("AI/Navigation Mesh");
        }

        void RegisterEnvironment(ComponentRegistry &registry)
        {
            registry.Register<AmbientLightVolumeComponent>("AmbientLightVolumeComponent", "Ambient Light Volume");
            registry.Register<IblCaptureComponent>("IblCaptureComponent", "IBL Capture Component");
            registry.Register<PhysicalSkyComponent>("PhysicalSkyComponent", "Physical Sky Component");
            registry.Register<VolumetricCloudComponent>("VolumetricCloudComponent", "Volumetric Cloud Component");

            registry.AddMenuItem<AmbientLightVolumeComponent>("Environment/Ambient Light Volume");
            registry.AddMenuItem<IblCaptureComponent>("Environment/IBL Capture");
            registry.AddMenuItem<PhysicalSkyComponent>("Environment/Physical Sky");
            registry.AddMenuItem<VolumetricCloudComponent>("Environment/Volumetric Cloud");
        }

        void RegisterScriptingAndAudio(ComponentRegistry &registry)
        {
            auto &script = registry.Register<ScriptComponent>("ScriptComponent", "Script Component");
            script.create = [] { return std::unique_ptr<Component>(std::make_unique<ScriptComponent>(ScriptComponentConfig{})); };
            script.allowMultiple = true;

            registry.Register<SoundEmitterComponent>("SoundEmitterComponent", "Sound Emitter Component");
            registry.Register<SoundListenerComponent>("SoundListenerComponent", "Sound Listener Component");
            registry.Register<AudioEnvironmentVolumeComponent>("AudioEnvironmentVolumeComponent", "Audio Environment Volume Component");

            registry.AddMenuItem<ScriptComponent>("Scripting/Script");
            registry.AddMenuItem<SoundEmitterComponent>("Audio/Sound Emitter");
            registry.AddMenuItem<SoundListenerComponent>("Audio/Sound Listener");
            registry.AddMenuItem<AudioEnvironmentVolumeComponent>("Audio/Environment Volume");
        }
    }

    void RegisterBuiltinComponentTypes(ComponentRegistry &registry)
    {
        RegisterRendering(registry);
        RegisterAnimation(registry);
        RegisterPhysicsAndAI(registry);
        RegisterEnvironment(registry);
        RegisterScriptingAndAudio(registry);
        RegisterUIComponentTypes(registry);
    }
}
