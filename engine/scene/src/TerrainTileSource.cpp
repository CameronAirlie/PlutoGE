#include "PlutoGE/scene/TerrainTileSource.h"

#include "PlutoGE/scene/ComponentRegistry.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <algorithm>
#include <functional>

namespace PlutoGE::scene
{
    namespace
    {
        std::string TypeName(const Component &component)
        {
            const auto *info = ComponentRegistry::Get().Find(component);
            return info ? info->typeName : std::string();
        }

        std::vector<Property> SourceProperties(const Component &component)
        {
            const auto type = TypeName(component);
            auto properties = component.SerializeForInspector();
            std::erase_if(properties, [&](const Property &property) { return IsTileOwnedProperty(type, property.name); });
            return properties;
        }

        // Components in a stable order: by type bucket, then position within the type.
        std::vector<Component *> ComponentsOf(const Entity &entity)
        {
            std::vector<Component *> components;
            for (const auto &bucket : entity.GetComponentBuckets())
                for (auto *component : bucket)
                    if (component)
                        components.push_back(component);
            return components;
        }
    }

    bool IsTileOwnedProperty(std::string_view componentType, std::string_view property)
    {
        if (componentType == "TerrainComponent")
            return property == "Width" || property == "Depth" || property == "CellSize" || property == "HeightScale" ||
                   property == "SkirtDepth" || property == "Static" || property == "HeightMap" || property == "HeightData" ||
                   property == "HeightSamples" || property == "HoleMask" || property == "WeightResolution" ||
                   property.starts_with("WeightMap");
        if (componentType == "TerrainVolumeComponent")
            return property == "VolumeFile";
        if (componentType == "ProceduralTerrainComponent")
            return property == "GenerateOnLoad" || property == "Texture";
        if (componentType == "FoliageComponent")
            return property.ends_with(".Instances") || property == "Instances";
        return false;
    }

    Entity *TemplateTileSource::Create(Scene &scene, Entity &world, std::string *error) const
    {
        auto *tile = Prefab::DuplicateEntity(scene, m_template, &world, false);
        if (!tile)
        {
            if (error)
                *error = "The tile template could not be copied.";
            return nullptr;
        }
        // The copy is a tile, not another template.
        tile->SetTags({});
        tile->SetActive(true);
        return tile;
    }

    bool TemplateTileSource::Refresh(Entity &tile) const
    {
        const auto source = ComponentsOf(m_template);
        const auto target = ComponentsOf(tile);
        // Components added to or removed from the template need the tile rebuilt.
        if (source.size() != target.size())
            return false;
        for (std::size_t index = 0; index < source.size(); ++index)
            if (source[index]->GetTypeID() != target[index]->GetTypeID())
                return false;
        for (std::size_t index = 0; index < source.size(); ++index)
        {
            target[index]->DeserializeKeepingHeavyData(SourceProperties(*source[index]));
            target[index]->SetEnabled(source[index]->IsEnabled());
        }
        return true;
    }

    std::uint64_t TemplateTileSource::Signature() const
    {
        std::string text;
        for (const auto *component : ComponentsOf(m_template))
        {
            text += TypeName(*component);
            text += component->IsEnabled() ? "+" : "-";
            for (const auto &property : SourceProperties(*component))
            {
                text += property.name;
                text += '=';
                text += property.value;
                text += '\n';
            }
        }
        return std::hash<std::string>{}(text) | 1u; // never 0, which means "not watched"
    }

    Entity *PrefabTileSource::Create(Scene &scene, Entity &world, std::string *error) const
    {
        return Prefab::Instantiate(scene, m_prefab, &world, error);
    }

    Entity *DefaultTileSource::Create(Scene &scene, Entity &world, std::string *) const
    {
        auto *tile = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Tile"}), &world);
        auto *terrain = tile->CreateComponent<TerrainComponent>(TerrainComponentConfig{});
        if (!m_material.empty())
            terrain->Deserialize({{"MaterialAsset", PropertyType::String, m_material}});
        tile->CreateComponent<ColliderComponent>()->SetShape(ColliderShape::Terrain);
        return tile;
    }
}
