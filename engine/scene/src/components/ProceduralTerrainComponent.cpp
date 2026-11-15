#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <fstream>
#include <map>
#include <mutex>
#include <set>

namespace PlutoGE::scene
{
    namespace
    {
        static_assert(ProceduralFoliageInstanceIdBit == (1ull << 63), "procedural::Scatter assumes this bit");

        std::string ResolvePath(const std::string &reference)
        {
            auto resolved = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference);
            return resolved.empty() ? reference : resolved;
        }

        bool ReadBytes(const std::string &path, std::vector<std::uint8_t> &bytes)
        {
            if (content::IsMounted(path))
            {
                std::string packed;
                if (!content::ReadFile(path, packed))
                    return false;
                bytes.assign(packed.begin(), packed.end());
                return true;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return false;
            bytes.assign(std::istreambuf_iterator<char>(input), {});
            return true;
        }

        std::mutex &MaskCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::map<std::pair<std::string, int>, std::shared_ptr<const TerrainHeightField>> &MaskCache()
        {
            static std::map<std::pair<std::string, int>, std::shared_ptr<const TerrainHeightField>> cache;
            return cache;
        }

        // Every entity, depth first.
        template <typename Visit>
        void ForEachEntity(const Scene &scene, Visit &&visit)
        {
            const auto walk = [&](auto &&self, const Entity *entity) -> void {
                visit(*entity);
                for (const auto *child : entity->GetChildren())
                    self(self, child);
            };
            for (const auto *root : scene.GetRootEntities())
                walk(walk, root);
        }

        // The splines that are distance sources for `source`: tagged with it, or named it.
        void AddSplineSources(const Scene &scene, const std::string &source, procedural::PolylineDistanceSet &distances)
        {
            ForEachEntity(scene, [&](const Entity &entity) {
                if (!entity.IsActive() || (!entity.HasTag(source) && entity.GetName() != source))
                    return;
                for (const auto *spline : entity.GetComponents<SplineComponent>())
                {
                    const auto samples = spline->SampleControlPoints(std::max(spline->GetSamplesPerSegment(), 4));
                    if (samples.empty())
                        continue;
                    const glm::mat4 world = entity.GetWorldTransform();
                    std::vector<glm::vec2> points;
                    points.reserve(samples.size() + 1);
                    for (const auto &sample : samples)
                    {
                        const glm::vec4 position = world * glm::vec4(sample.position, 1.0f);
                        points.emplace_back(position.x, position.z);
                    }
                    if (spline->IsClosed() && points.size() > 2)
                        points.push_back(points.front());
                    const float scale = glm::length(glm::vec3(world[0]));
                    distances.Add(source, points, 0.5f * spline->GetWidth() * scale);
                }
            });
        }

        void CollectDistanceSources(const procedural::TerrainRuleStack &stack, std::set<std::string> &sources)
        {
            for (const auto &rule : stack.rules)
                if (rule.enabled && rule.input == procedural::RuleInput::Distance && !rule.source.empty())
                    sources.insert(rule.source);
        }
    }

    procedural::ProceduralGenerationResult ProceduralTerrainComponent::PreparedGeneration::Run() const
    {
        procedural::ProceduralGenerationInput input;
        input.definition = &definition;
        input.surface = &surface;
        input.inputs = inputs.get();
        input.layerNames = layerNames;
        input.foliageTypes = foliageTypes;
        input.weightResolution = weightResolution;
        input.texture = texture;
        input.scatter = scatter;
        auto result = procedural::Generate(input);
        result.warnings.insert(result.warnings.begin(), warnings.begin(), warnings.end());
        return result;
    }

    void ProceduralTerrainComponent::Update(float)
    {
        // Generate on load: once, when the terrain beside it is there.
        if (m_generateOnLoad && !m_generated && GetTerrain())
            Generate();
    }

    std::vector<Property> ProceduralTerrainComponent::Serialize() const
    {
        return {
            {"Definition", PropertyType::String, m_definition.Encode()},
            {"GenerateOnLoad", PropertyType::Bool, m_generateOnLoad ? "true" : "false"},
            {"WeightResolution", PropertyType::Int, std::to_string(m_weightResolution)},
            {"Texture", PropertyType::Bool, m_texture ? "true" : "false"},
            {"Scatter", PropertyType::Bool, m_scatter ? "true" : "false"},
        };
    }

    void ProceduralTerrainComponent::Deserialize(const std::vector<Property> &properties)
    {
        const auto flag = [](const Property &property) { return property.value == "true" || property.value == "1"; };
        for (const auto &property : properties)
        {
            if (property.name == "Definition")
            {
                if (auto definition = procedural::ProceduralTerrainDefinition::Decode(property.Text()))
                    m_definition = std::move(*definition);
            }
            else if (property.name == "GenerateOnLoad")
                m_generateOnLoad = flag(property);
            else if (property.name == "WeightResolution")
                SetWeightResolution(std::stoi(property.value));
            else if (property.name == "Texture")
                m_texture = flag(property);
            else if (property.name == "Scatter")
                m_scatter = flag(property);
        }
    }

    TerrainComponent *ProceduralTerrainComponent::GetTerrain() const
    {
        return GetOwner() ? GetOwner()->GetComponent<TerrainComponent>() : nullptr;
    }

    FoliageComponent *ProceduralTerrainComponent::GetFoliage() const
    {
        return GetOwner() ? GetOwner()->GetComponent<FoliageComponent>() : nullptr;
    }

    std::shared_ptr<const TerrainHeightField> ProceduralTerrainComponent::LoadMaskImage(const std::string &image, int channel, std::string *error)
    {
        const auto path = ResolvePath(image);
        const auto key = std::make_pair(path, channel);
        {
            std::lock_guard lock(MaskCacheMutex());
            if (const auto found = MaskCache().find(key); found != MaskCache().end())
                return found->second;
        }
        std::vector<std::uint8_t> bytes;
        if (!ReadBytes(path, bytes))
        {
            if (error)
                *error = "Could not read the mask image " + image + ".";
            return nullptr;
        }
        auto field = procedural::DecodeMaskImage(bytes, channel, error);
        if (!field)
            return nullptr;
        auto shared = std::make_shared<const TerrainHeightField>(std::move(*field));
        std::lock_guard lock(MaskCacheMutex());
        MaskCache()[key] = shared;
        return shared;
    }

    void ProceduralTerrainComponent::ClearMaskCache()
    {
        std::lock_guard lock(MaskCacheMutex());
        MaskCache().clear();
    }

    std::optional<ProceduralTerrainComponent::PreparedGeneration> ProceduralTerrainComponent::Prepare(std::string *error) const
    {
        auto *terrain = GetTerrain();
        auto *owner = GetOwner();
        if (!terrain || !owner)
        {
            if (error)
                *error = "A procedural terrain needs a Terrain Component on its entity.";
            return std::nullopt;
        }

        PreparedGeneration prepared;
        prepared.definition = m_definition;
        const glm::mat4 world = owner->GetWorldTransform();
        prepared.surface = procedural::TerrainSurface(terrain->GetWidth(), terrain->GetDepth(), terrain->GetCellSize(), world,
                                                      [terrain](int x, int z) { return terrain->GetSurfaceSampleHeight(x, z); });
        prepared.inputs = std::make_shared<procedural::ProceduralInputs>();
        prepared.inputs->SetSeed(m_definition.seed);

        // Masks: a zero size spans this terrain.
        const glm::vec2 extent = prepared.surface.Extent();
        const glm::vec2 corner0 = prepared.surface.LocalToWorldXZ({0.0f, 0.0f});
        const glm::vec2 corner1 = prepared.surface.LocalToWorldXZ(extent);
        for (const auto &source : m_definition.masks)
        {
            std::string maskError;
            auto values = LoadMaskImage(source.image, source.channel, &maskError);
            if (!values)
            {
                prepared.warnings.push_back("Mask \"" + source.name + "\": " + maskError);
                continue;
            }
            auto mask = std::make_shared<procedural::ProceduralMask>();
            mask->values = std::move(values);
            const bool spansTerrain = source.worldSize.x <= 0.0f || source.worldSize.y <= 0.0f;
            mask->worldMin = spansTerrain ? glm::min(corner0, corner1) : source.worldMin;
            mask->worldSize = spansTerrain ? glm::abs(corner1 - corner0) : source.worldSize;
            prepared.inputs->SetMask(source.name, std::move(mask));
        }

        // Distance sources named by any rule.
        std::set<std::string> sources;
        for (const auto &biome : m_definition.biomes)
        {
            CollectDistanceSources(biome.weight, sources);
            for (const auto &layer : biome.layers)
                CollectDistanceSources(layer.coverage, sources);
            for (const auto &scatter : biome.scatter)
                CollectDistanceSources(scatter.density, sources);
        }
        if (const auto *scene = owner->GetScene())
            for (const auto &source : sources)
                AddSplineSources(*scene, source, prepared.inputs->Distances());

        for (const auto &layer : terrain->GetLayers().GetLayers())
            prepared.layerNames.push_back(layer.name);
        if (const auto *foliage = GetFoliage())
            for (std::size_t type = 0; type < foliage->GetTypeCount(); ++type)
                prepared.foliageTypes.push_back(foliage->GetType(type)->name);
        prepared.weightResolution = m_weightResolution > 0 ? m_weightResolution : terrain->GetLayers().GetWeights().GetResolution();
        prepared.texture = m_texture;
        prepared.scatter = m_scatter && GetFoliage() != nullptr;
        return prepared;
    }

    void ProceduralTerrainComponent::Apply(const procedural::ProceduralGenerationResult &result)
    {
        m_generated = true;
        m_lastWarnings = result.warnings;
        if (auto *terrain = GetTerrain(); terrain && result.weights)
            terrain->GetLayers().SetWeights(*result.weights);

        auto *foliage = GetFoliage();
        if (!foliage || !m_scatter)
            return;
        auto snapshot = foliage->CaptureInstanceSnapshot();
        bool changed = false;
        for (std::size_t type = 0; type < snapshot.size(); ++type)
        {
            auto &instances = snapshot[type];
            const auto before = instances.size();
            std::erase_if(instances, [](const FoliageInstance &instance) { return IsProceduralFoliageInstanceId(instance.id); });
            changed |= instances.size() != before;
            if (type < result.instances.size())
            {
                for (const auto &generated : result.instances[type])
                    instances.push_back({generated.id, generated.position, generated.rotationDegrees, generated.scale});
                changed |= !result.instances[type].empty();
            }
        }
        if (changed)
            foliage->RestoreInstanceSnapshot(snapshot);
    }

    procedural::ProceduralGenerationResult ProceduralTerrainComponent::Generate()
    {
        std::string error;
        auto prepared = Prepare(&error);
        if (!prepared)
        {
            procedural::ProceduralGenerationResult failed;
            failed.warnings.push_back(error);
            m_lastWarnings = failed.warnings;
            m_generated = true; // generate-on-load does not retry every frame
            return failed;
        }
        auto result = prepared->Run();
        Apply(result);
        return result;
    }
}
