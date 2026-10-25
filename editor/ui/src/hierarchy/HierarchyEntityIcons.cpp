#include "PlutoGE/ui/hierarchy/HierarchyEntityIcons.h"

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/components/AudioEnvironmentVolumeComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/DecalComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/ui/EditorIcons.h"

#include <array>

namespace PlutoGE::ui
{
    namespace
    {
        struct IconRule
        {
            bool (*matches)(const scene::Entity &);
            const char *icon;
        };

        template <typename T>
        bool Has(const scene::Entity &entity)
        {
            return entity.HasComponent<T>();
        }

        bool IsDirectionalLight(const scene::Entity &entity)
        {
            const auto *light = entity.GetComponent<scene::LightComponent>();
            return light && light->GetLight().type == scene::LightType::Directional;
        }

        bool IsSkinnedMesh(const scene::Entity &entity)
        {
            const auto *mesh = entity.GetComponent<scene::MeshComponent>();
            return mesh && mesh->GetMesh() && mesh->GetMesh()->HasSkeleton();
        }

        bool IsPrefabRoot(const scene::Entity &entity)
        {
            return entity.IsPrefabInstanceRoot();
        }

        // Ordered most specific first; the first match wins.
        constexpr std::array kRules{
            IconRule{&Has<scene::CameraComponent>, icons::EntityCamera},
            IconRule{&IsDirectionalLight, icons::EntityDirectionalLight},
            IconRule{&Has<scene::LightComponent>, icons::EntityPointLight},
            IconRule{&Has<scene::PhysicalSkyComponent>, icons::EntitySky},
            IconRule{&Has<scene::OceanComponent>, icons::EntityOcean},
            IconRule{&Has<scene::TerrainComponent>, icons::EntityTerrain},
            IconRule{&Has<scene::FoliageComponent>, icons::EntityFoliage},
            IconRule{&Has<scene::ClothComponent>, icons::EntityCloth},
            IconRule{&Has<scene::ParticleSystemComponent>, icons::ParticleSystemAsset},
            IconRule{&Has<scene::IblCaptureComponent>, icons::Contrast},
            IconRule{&Has<scene::AmbientLightVolumeComponent>, icons::EntityVolume},
            IconRule{&Has<scene::AudioEnvironmentVolumeComponent>, icons::EntityVolume},
            IconRule{&Has<scene::SoundEmitterComponent>, icons::AudioAsset},
            IconRule{&Has<scene::SoundListenerComponent>, icons::EntityListener},
            IconRule{&Has<scene::CanvasComponent>, icons::RmlDocumentAsset},
            IconRule{&Has<scene::DecalComponent>, icons::EntityDecal},
            IconRule{&Has<scene::SplineComponent>, icons::EntitySpline},
            IconRule{&Has<scene::NavigationMeshComponent>, icons::EntityNavMesh},
            IconRule{&Has<scene::NavAgentComponent>, icons::EntityNavAgent},
            IconRule{&IsPrefabRoot, icons::PrefabAsset},
            IconRule{&IsSkinnedMesh, icons::EntitySkinnedMesh},
            IconRule{&Has<scene::MeshComponent>, icons::MeshAsset},
            IconRule{&Has<scene::ScriptComponent>, icons::ScriptAsset},
        };
    }

    const char *GetHierarchyEntityIcon(const scene::Entity &entity)
    {
        for (const auto &rule : kRules)
        {
            if (rule.matches(entity))
            {
                return rule.icon;
            }
        }
        return icons::EntityEmpty;
    }
}
