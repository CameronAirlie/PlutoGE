#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/water/WaterSystem.h"
#include <algorithm>

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        // Malformed or non-finite text keeps the current value instead of throwing out of scene loading.
        float ParseFloat(const std::string &text, float fallback)
        {
            try
            {
                const float value = std::stof(text);
                return std::isfinite(value) ? value : fallback;
            }
            catch (const std::exception &)
            {
                return fallback;
            }
        }
        int ParseInt(const std::string &text, int fallback)
        {
            try
            {
                return std::stoi(text);
            }
            catch (const std::exception &)
            {
                return fallback;
            }
        }
        glm::vec2 ParseVec2(const std::string &text, glm::vec2 fallback)
        {
            glm::vec2 value = fallback;
            return std::sscanf(text.c_str(), "%f,%f", &value.x, &value.y) == 2 && std::isfinite(value.x) && std::isfinite(value.y)
                ? value : fallback;
        }
        glm::vec3 ParseColor(const std::string &text, glm::vec3 fallback)
        {
            glm::vec3 value = fallback;
            return std::sscanf(text.c_str(), "%f,%f,%f", &value.x, &value.y, &value.z) == 3 &&
                           std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
                ? value : fallback;
        }
        std::string Text(float value) { return std::to_string(value); }
        std::string Text(glm::vec2 value) { return std::to_string(value.x) + "," + std::to_string(value.y); }
        std::string ColorText(glm::vec3 value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + ",1.0";
        }
        std::string Text(bool value) { return value ? "true" : "false"; }

        // One layer's properties under "Layer.<index>.", read and written by the same table so the two never drift.
        template <class Visitor>
        void VisitLayer(DetailGrassComponent::Layer &layer, Visitor &&visit)
        {
            auto &grass = layer.settings;
            visit.String("Name", grass.name);
            visit.Bool("Enabled", grass.enabled);
            visit.String("DensityLayer", layer.densityLayer);
            visit.String("Material", layer.materialAsset);
            visit.Float("Density", grass.density);
            visit.Float("WeightThreshold", grass.weightThreshold);
            visit.Float("MaxSlope", grass.maxSlopeDegrees);
            visit.Range("Altitude", grass.minAltitude, grass.maxAltitude);
            visit.Float("AltitudeFade", grass.altitudeFade);
            visit.Bool("GrowUnderWater", grass.growUnderWater);
            visit.Float("ShoreMargin", grass.shoreMargin);
            visit.Float("Clustering", grass.clustering);
            visit.Range("Height", grass.minHeight, grass.maxHeight);
            visit.Range("Width", grass.minWidth, grass.maxWidth);
            visit.Float("AlignToGround", grass.alignToGround);
            visit.Unsigned("Seed", grass.seed);
            visit.Float("FullDensityDistance", grass.fullDensityDistance);
            visit.Float("DensityFalloff", grass.densityFalloff);
            visit.Float("DrawDistance", grass.drawDistance);
            visit.Float("WidthCompensation", grass.maxWidthCompensation);
            visit.Range("LodDistances", grass.lod1Distance, grass.lod2Distance);
            visit.Color("Color", grass.color);
            visit.Color("DryColor", grass.dryColor);
            visit.Float("DryAmount", grass.dryAmount);
            visit.Float("PatchSize", grass.patchSize);
            visit.Float("ColorJitter", grass.colorJitter);
            visit.Float("GroundTint", grass.groundTint);
            visit.Float("RootOcclusion", grass.rootOcclusion);
            visit.Float("NormalBend", grass.normalBend);
            visit.Float("Roughness", grass.roughness);
            visit.Float("Translucency", grass.translucency);
            visit.Bool("Wind", grass.wind.enabled);
            visit.Float("WindStiffness", grass.wind.stiffness);
            visit.Float("WindFlutter", grass.wind.flutter);
            visit.Unsigned("Blades", grass.shape.blades);
            visit.Float("ClumpRadius", grass.shape.radius);
            visit.Float("BladeWidth", grass.shape.bladeWidth);
            visit.Float("MinBladeHeight", grass.shape.minBladeHeight);
            visit.Float("Lean", grass.shape.lean);
            visit.Float("Curvature", grass.shape.curvature);
            visit.Unsigned("ShapeSeed", grass.shape.seed);
        }

        struct Writer
        {
            std::vector<Property> &properties;
            std::string prefix;
            void String(const char *name, std::string &value) { properties.push_back({prefix + name, PropertyType::String, value}); }
            void Bool(const char *name, bool &value) { properties.push_back({prefix + name, PropertyType::Bool, Text(value)}); }
            void Float(const char *name, float &value) { properties.push_back({prefix + name, PropertyType::Float, Text(value)}); }
            void Unsigned(const char *name, std::uint32_t &value)
            {
                properties.push_back({prefix + name, PropertyType::Int, std::to_string(value)});
            }
            void Range(const char *name, float &low, float &high)
            {
                properties.push_back({prefix + name, PropertyType::Vec2, Text(glm::vec2(low, high))});
            }
            void Color(const char *name, glm::vec3 &value) { properties.push_back({prefix + name, PropertyType::Color, ColorText(value)}); }
        };

        struct Reader
        {
            const std::string &name;  // the property's name past the layer prefix
            const std::string &value;
            void String(const char *key, std::string &target) { if (name == key) target = value; }
            void Bool(const char *key, bool &target) { if (name == key) target = value == "true" || value == "1"; }
            void Float(const char *key, float &target) { if (name == key) target = ParseFloat(value, target); }
            void Unsigned(const char *key, std::uint32_t &target)
            {
                if (name == key) target = static_cast<std::uint32_t>(std::max(0, ParseInt(value, static_cast<int>(target))));
            }
            void Range(const char *key, float &low, float &high)
            {
                if (name != key) return;
                const auto range = ParseVec2(value, {low, high});
                low = range.x;
                high = range.y;
            }
            void Color(const char *key, glm::vec3 &target) { if (name == key) target = ParseColor(value, target); }
        };
    }

    DetailGrassComponent::Layer DetailGrassComponent::Sanitized(Layer layer)
    {
        layer.settings = layer.settings.Sanitized();
        // The clump mesh is one unit tall: the wind bends over its whole height from the ground.
        layer.settings.wind.bendHeight = 1.0f;
        layer.settings.wind.pivotHeight = 0.0f;
        return layer;
    }

    void DetailGrassComponent::SetLayers(std::vector<Layer> layers)
    {
        if (layers.size() > MaxLayers) layers.resize(MaxLayers);
        for (auto &layer : layers) layer = Sanitized(std::move(layer));
        m_layers = std::move(layers);
    }

    bool DetailGrassComponent::AddLayer(Layer layer)
    {
        if (m_layers.size() >= MaxLayers) return false;
        m_layers.push_back(Sanitized(std::move(layer)));
        return true;
    }

    void DetailGrassComponent::SetLayer(std::size_t index, Layer layer)
    {
        if (index < m_layers.size()) m_layers[index] = Sanitized(std::move(layer));
    }

    void DetailGrassComponent::RemoveLayer(std::size_t index)
    {
        if (index < m_layers.size()) m_layers.erase(m_layers.begin() + static_cast<std::ptrdiff_t>(index));
    }

    std::vector<Property> DetailGrassComponent::Serialize() const
    {
        std::vector<Property> properties{{"LayerCount", PropertyType::Int, std::to_string(m_layers.size())}};
        for (std::size_t index = 0; index < m_layers.size(); ++index)
        {
            auto layer = m_layers[index];
            VisitLayer(layer, Writer{properties, "Layer." + std::to_string(index) + "."});
        }
        return properties;
    }

    void DetailGrassComponent::Deserialize(const std::vector<Property> &properties)
    {
        // A missing count keeps the current layers, as other components keep values a scene does not mention.
        auto layers = m_layers;
        for (const auto &property : properties)
            if (property.name == "LayerCount")
                layers.resize(static_cast<std::size_t>(std::clamp(ParseInt(property.value, int(layers.size())), 0, int(MaxLayers))));
        for (const auto &property : properties)
        {
            constexpr std::string_view prefix = "Layer.";
            if (property.name.rfind(prefix, 0) != 0) continue;
            const auto dot = property.name.find('.', prefix.size());
            if (dot == std::string::npos) continue;
            const int index = ParseInt(property.name.substr(prefix.size(), dot - prefix.size()), -1);
            if (index < 0 || static_cast<std::size_t>(index) >= layers.size()) continue;
            const std::string name = property.name.substr(dot + 1);
            VisitLayer(layers[static_cast<std::size_t>(index)], Reader{name, property.Text()});
        }
        SetLayers(std::move(layers));
    }

    TerrainComponent *DetailGrassComponent::GetTerrain() const
    {
        auto *owner = GetOwner();
        return owner ? owner->GetComponent<TerrainComponent>() : nullptr;
    }

    std::shared_ptr<const render::DetailGrassHeightField> DetailGrassComponent::GetHeightField() const
    {
        const auto *terrain = GetTerrain();
        if (!terrain || terrain->GetWidth() < 2 || terrain->GetDepth() < 2)
            return nullptr;
        // Water depths only when a layer keeps out of the water and there is water: keyed on the water's still surface
        // and where the terrain stands, so waves never rebuild the field.
        const auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        const bool avoidsWater = std::any_of(m_layers.begin(), m_layers.end(), [](const Layer &layer) {
            return layer.settings.enabled && !layer.settings.growUnderWater;
        });
        const water::WaterSurfaceSnapshot *water = avoidsWater && scene ? &scene->GetWaterSystem().GetFrameSnapshot(*scene) : nullptr;
        if (water && water->Empty())
            water = nullptr;
        const glm::mat4 terrainWorld = GetOwner()->GetWorldTransform();
        std::uint64_t waterSignature = 0;
        if (water)
        {
            waterSignature = water->StillSignature();
            const auto *bytes = reinterpret_cast<const unsigned char *>(&terrainWorld);
            for (std::size_t i = 0; i < sizeof(terrainWorld); ++i)
                waterSignature = (waterSignature ^ bytes[i]) * 1099511628211ull;
        }
        if (m_heightField && m_heightTerrain == terrain && m_heightRevision == terrain->GetHeightsRevision() &&
            m_holeRevision == terrain->GetHolesRevision() && m_heightColumns == terrain->GetWidth() &&
            m_heightRows == terrain->GetDepth() && m_heightCellSize == terrain->GetCellSize() &&
            m_heightSmoothing == terrain->GetSurfaceSmoothing() && m_waterSignature == waterSignature)
            return m_heightField;

        auto field = std::make_shared<render::DetailGrassHeightField>();
        field->columns = static_cast<std::uint32_t>(terrain->GetWidth());
        field->rows = static_cast<std::uint32_t>(terrain->GetDepth());
        field->cellSize = terrain->GetCellSize();
        field->heights.resize(std::size_t(field->columns) * field->rows);
        field->holes.assign(field->heights.size(), 0u);
        const auto &holes = terrain->GetHoles();
        for (int z = 0; z < terrain->GetDepth(); ++z)
            for (int x = 0; x < terrain->GetWidth(); ++x)
            {
                const auto index = std::size_t(z) * field->columns + std::size_t(x);
                field->heights[index] = terrain->GetSurfaceSampleHeight(x, z);
                // Any cut, whole or diagonal, keeps grass off the cell: it cannot root on the open half's rim.
                if (x + 1 < terrain->GetWidth() && z + 1 < terrain->GetDepth())
                    field->holes[index] = holes.GetCut(x, z) != TerrainHoleCut::None ? 1u : 0u;
            }
        if (water)
        {
            // Still water above each sample, where the drawn surface stands in the world.
            field->waterDepths.assign(field->heights.size(), render::DetailGrassNoWaterDepth);
            bool any = false;
            for (int z = 0; z < terrain->GetDepth(); ++z)
                for (int x = 0; x < terrain->GetWidth(); ++x)
                {
                    const auto index = std::size_t(z) * field->columns + std::size_t(x);
                    const glm::vec3 world = glm::vec3(terrainWorld * glm::vec4(x * field->cellSize, field->heights[index], z * field->cellSize, 1.0f));
                    if (const auto depth = water->StillDepth({world.x, world.z}, world.y))
                    {
                        field->waterDepths[index] = std::clamp(*depth, render::DetailGrassNoWaterDepth, 100.0f);
                        any = true;
                    }
                }
            if (!any)
                field->waterDepths.clear();
        }
        field->revision = ++m_heightFieldRevision;
        m_heightField = std::move(field);
        m_heightTerrain = terrain;
        m_heightRevision = terrain->GetHeightsRevision();
        m_holeRevision = terrain->GetHolesRevision();
        m_heightColumns = terrain->GetWidth();
        m_heightRows = terrain->GetDepth();
        m_heightCellSize = terrain->GetCellSize();
        m_heightSmoothing = terrain->GetSurfaceSmoothing();
        m_waterSignature = waterSignature;
        return m_heightField;
    }
}
