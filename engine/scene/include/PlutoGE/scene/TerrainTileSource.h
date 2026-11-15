#pragma once

#include "PlutoGE/scene/Entity.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace PlutoGE::scene
{
    class Scene;

    // Where a terrain world's tiles come from: something that makes a new tile entity (terrain settings and layers,
    // collider, foliage types, grass, biomes) before the world gives it its heights. New kinds of source (a tile per
    // biome, a remote template) implement this; TerrainWorldComponent picks one.
    class TerrainTileSource
    {
    public:
        virtual ~TerrainTileSource() = default;
        // A new active entity under `world`, or null (with a reason).
        virtual Entity *Create(Scene &scene, Entity &world, std::string *error) const = 0;
        // Brings a live tile up to date with the source in place, leaving what belongs to the tile (heights, holes,
        // weights). False when it cannot (the tile must be rebuilt instead).
        virtual bool Refresh(Entity &tile) const
        {
            (void)tile;
            return false;
        }
        // Changes whenever what tiles are made from changes; 0 for sources that are not watched.
        [[nodiscard]] virtual std::uint64_t Signature() const { return 0; }
    };

    // Tiles as copies of the world's Tile Template: an inactive child entity edited like any other.
    class TemplateTileSource final : public TerrainTileSource
    {
    public:
        explicit TemplateTileSource(const Entity &tileTemplate) : m_template(tileTemplate) {}
        Entity *Create(Scene &scene, Entity &world, std::string *error) const override;
        bool Refresh(Entity &tile) const override;
        [[nodiscard]] std::uint64_t Signature() const override;

    private:
        const Entity &m_template;
    };

    // Tiles as instances of a prefab.
    class PrefabTileSource final : public TerrainTileSource
    {
    public:
        explicit PrefabTileSource(std::string prefab) : m_prefab(std::move(prefab)) {}
        Entity *Create(Scene &scene, Entity &world, std::string *error) const override;

    private:
        std::string m_prefab;
    };

    // Plain tiles: a terrain with a material and a terrain collider.
    class DefaultTileSource final : public TerrainTileSource
    {
    public:
        explicit DefaultTileSource(std::string material) : m_material(std::move(material)) {}
        Entity *Create(Scene &scene, Entity &world, std::string *error) const override;

    private:
        std::string m_material;
    };

    // Properties of a tile's components that belong to the tile rather than its source: the grid, heights, holes and
    // weight files of its terrain, and what the world decides for its procedural generation and foliage instances.
    [[nodiscard]] bool IsTileOwnedProperty(std::string_view componentType, std::string_view property);
}
