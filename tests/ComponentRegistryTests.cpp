#include "PlutoGE/scene/ComponentRegistry.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/ActiveRagdollComponent.h"
#include "PlutoGE/scene/components/DecalComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE::scene;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    const ComponentMenuItem &FindMenuItem(const ComponentRegistry &registry, std::string_view path)
    {
        for (const auto &item : registry.GetMenuItems())
            if (item.path == path)
                return item;
        throw std::runtime_error("Missing menu item " + std::string(path));
    }

    void TestBuiltinTypesAreUnique()
    {
        const auto &registry = ComponentRegistry::Get();
        std::set<std::string> names;
        std::set<ComponentTypeID> ids;
        for (const auto &type : registry.GetTypes())
        {
            Require(names.insert(type->typeName).second, "Duplicate type name " + type->typeName);
            Require(ids.insert(type->typeID).second, "Duplicate type id for " + type->typeName);
            Require(static_cast<bool>(type->create), type->typeName + " cannot be deserialized");
        }
        // Every name the hand-written serializer chain used to know.
        for (const char *name : {
                 "MeshComponent", "DecalComponent", "TerrainComponent", "ClothComponent", "OceanComponent",
                 "SplineComponent", "FoliageComponent", "ParticleSystemComponent", "AnimationComponent",
                 "ActiveRagdollComponent", "RagdollTuningComponent", "SkeletonAttachmentComponent",
                 "SequencerComponent", "IKComponent", "CameraRigComponent", "CameraComponent", "LightComponent",
                 "RigidbodyComponent", "NavAgentComponent", "NavigationMeshComponent", "ColliderComponent",
                 "AmbientLightVolumeComponent", "IblCaptureComponent", "ScriptComponent", "SoundEmitterComponent",
                 "SoundListenerComponent", "AudioEnvironmentVolumeComponent", "CanvasComponent", "RmlWidgetComponent",
                 "RectTransformComponent", "UIImageComponent", "UITextComponent", "UIButtonComponent",
                 "VolumetricCloudComponent", "PhysicalSkyComponent"})
            Require(registry.Find(std::string_view(name)) != nullptr, std::string("Missing built-in type ") + name);
        for (const auto &item : registry.GetMenuItems())
            Require(registry.Find(item.typeID) != nullptr, "Menu item without a type: " + item.path);
    }

    void TestCreateRoundTrips()
    {
        const auto &registry = ComponentRegistry::Get();
        for (const char *name : {"CanvasComponent", "RectTransformComponent", "UIImageComponent", "UITextComponent",
                                 "UIButtonComponent", "DecalComponent", "OceanComponent"})
        {
            const auto component = registry.Create(name);
            Require(component != nullptr, std::string("Could not create ") + name);
            Require(registry.GetTypeName(*component) == name, std::string("Type name mismatch for ") + name);
            Require(SceneSerializer::GetComponentTypeName(*component) == name,
                    std::string("Serializer disagrees with the registry for ") + name);
        }
        Require(registry.Create("NoSuchComponent") == nullptr, "Unknown types must not be created");
        Require(registry.IsAssetPathProperty("UIImageComponent", "TexturePath"), "Image texture is an asset path");
        Require(!registry.IsAssetPathProperty("UIImageComponent", "Color"), "Image color is not an asset path");
    }

    void TestAuthoringRules()
    {
        const auto &registry = ComponentRegistry::Get();
        Entity entity(EntityConfig{.name = "Rules"});

        const auto *ragdoll = registry.Find<ActiveRagdollComponent>();
        Require(ragdoll && !registry.CanAdd(entity, *ragdoll), "Active ragdoll requires an animation component");
        Require(registry.AddToEntity(entity, *registry.Find<AnimationComponent>()) != nullptr, "Animation add failed");
        Require(registry.CanAdd(entity, *ragdoll), "Active ragdoll should be addable next to animation");
        Require(registry.AddToEntity(entity, *registry.Find<AnimationComponent>()) == nullptr,
                "Single-instance components must not be added twice");

        const auto *script = registry.Find<ScriptComponent>();
        Require(registry.AddToEntity(entity, *script) && registry.AddToEntity(entity, *script),
                "Scripts allow multiple instances");

        auto *canvas = registry.AddToEntity(entity, FindMenuItem(registry, "UI/RmlUi Canvas"));
        Require(canvas != nullptr, "Canvas menu item failed");
        Require(static_cast<CanvasComponent *>(canvas)->GetBackend() == UIRenderBackend::RmlUi,
                "Menu configuration was not applied");
        Require(registry.GetDisplayName(*canvas) == "RmlUi Canvas Component", "Dynamic display name failed");
    }
}

int main()
{
    try
    {
        TestBuiltinTypesAreUnique();
        TestCreateRoundTrips();
        TestAuthoringRules();
    }
    catch (const std::exception &error)
    {
        std::cerr << "ComponentRegistryTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ComponentRegistryTests passed\n";
    return 0;
}
