#include "PlutoGE/scene/components/ComponentFactory.h"

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scripting/ScriptRuntime.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

namespace PlutoGE::scene
{
    MeshComponent *AddMeshComponent(Entity &entity, render::Mesh *mesh, render::Material *material)
    {
        return entity.CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = mesh, .material = material});
    }

    Component *AddComponentByTypeName(Entity &entity, std::string_view typeName)
    {
        if (typeName == "MeshComponent") return entity.CreateComponent<MeshComponent>(MeshComponentConfig{});
        if (typeName == "TerrainComponent") return entity.CreateComponent<TerrainComponent>(TerrainComponentConfig{});
        if (typeName == "FoliageComponent") return entity.CreateComponent<FoliageComponent>();
        if (typeName == "ClothComponent") return entity.CreateComponent<ClothComponent>();
        if (typeName == "ParticleSystemComponent") return entity.CreateComponent<ParticleSystemComponent>();
        if (typeName == "SplineComponent") return entity.CreateComponent<SplineComponent>(SplineComponentConfig{});
        if (typeName == "OceanComponent") return entity.CreateComponent<OceanComponent>();
        if (typeName == "AnimationComponent") return entity.CreateComponent<AnimationComponent>();
        if (typeName == "SkeletonAttachmentComponent") return entity.CreateComponent<SkeletonAttachmentComponent>();
        if (typeName == "CameraComponent") return entity.CreateComponent<CameraComponent>(new render::Camera(render::CameraConfig{}));
        if (typeName == "LightComponent") return entity.CreateComponent<LightComponent>();
        if (typeName == "RigidbodyComponent") return entity.CreateComponent<RigidbodyComponent>();
        if (typeName == "NavAgentComponent") return entity.CreateComponent<NavAgentComponent>();
        if (typeName == "NavigationMeshComponent") return entity.CreateComponent<NavigationMeshComponent>();
        if (typeName == "ColliderComponent") return entity.CreateComponent<ColliderComponent>();
        if (typeName == "IblCaptureComponent") return entity.CreateComponent<IblCaptureComponent>();
        if (typeName == "VolumetricCloudComponent") return entity.CreateComponent<VolumetricCloudComponent>();
        if (typeName == "PhysicalSkyComponent") return entity.CreateComponent<PhysicalSkyComponent>();
        if (typeName == "ScriptComponent") return entity.CreateComponent<ScriptComponent>(ScriptComponentConfig{});
        if (typeName == "SoundEmitterComponent") return entity.CreateComponent<SoundEmitterComponent>();
        if (typeName == "SoundListenerComponent") return entity.CreateComponent<SoundListenerComponent>();
        if (typeName == "CanvasComponent") return entity.CreateComponent<CanvasComponent>();
        if (typeName == "RectTransformComponent") return entity.CreateComponent<RectTransformComponent>();
        if (typeName == "UIImageComponent") return entity.CreateComponent<UIImageComponent>();
        if (typeName == "UITextComponent") return entity.CreateComponent<UITextComponent>();
        if (typeName == "UIButtonComponent") return entity.CreateComponent<UIButtonComponent>();
        return nullptr;
    }
}
