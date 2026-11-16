#include "PlutoGE/scene/components/RiverComponent.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <algorithm>
#include <limits>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        float ParseFloat(const Property &property, float fallback)
        {
            try
            {
                return std::stof(property.value);
            }
            catch (...)
            {
                return fallback;
            }
        }

        // The ground under a world position, from whichever terrain is there.
        std::optional<float> GroundAt(const Scene &scene, glm::vec2 world)
        {
            for (const auto *terrain : scene.GetTerrainComponents())
            {
                const auto *owner = terrain ? terrain->GetOwner() : nullptr;
                if (!owner || !terrain->IsEnabled() || !owner->IsActive())
                    continue;
                float height = 0.0f;
                glm::vec3 normal{0.0f, 1.0f, 0.0f};
                if (terrain->TrySampleWorldSurface({world.x, 1.0e6f, world.y}, height, normal))
                    return height;
            }
            return std::nullopt;
        }
    }

    RiverComponent::RiverComponent() = default;
    RiverComponent::~RiverComponent()
    {
        // The spline must not keep drawing with a material that is about to go.
        if (auto *spline = GetSpline(); spline && m_waterMaterial && spline->GetMaterial() == m_waterMaterial.get())
            spline->SetMaterial(nullptr);
    }

    SplineComponent *RiverComponent::GetSpline() const
    {
        return GetOwner() ? GetOwner()->GetComponent<SplineComponent>() : nullptr;
    }

    void RiverComponent::Update(float)
    {
        auto *spline = GetSpline();
        // Only splines without a material asset of their own take the water look; choose a material asset (a shader
        // graph with flowing normals, say) to replace it.
        if (!spline || !spline->GetMaterialAssetReference().empty())
            return;
        if (!m_waterMaterial)
        {
            render::MaterialConfig config;
            config.color = {0.08f, 0.24f, 0.27f, 0.72f};
            config.alphaMode = render::AlphaMode::Blend;
            config.roughness = 0.06f;
            config.metallic = 0.0f;
            config.castsShadow = false;
            m_waterMaterial = std::make_unique<render::Material>(config);
        }
        if (spline->GetMaterial() != m_waterMaterial.get())
            spline->SetMaterial(m_waterMaterial.get());
    }

    std::vector<Property> RiverComponent::Serialize() const
    {
        return {
            {"Depth", PropertyType::Float, std::to_string(m_depth)},
            {"BankWidth", PropertyType::Float, std::to_string(m_bankWidth)},
            {"SurfaceDrop", PropertyType::Float, std::to_string(m_surfaceDrop)},
            {"FlowSpeed", PropertyType::Float, std::to_string(m_flowSpeed)},
            {"UseWaterMaterial", PropertyType::Bool, m_useWaterMaterial ? "true" : "false"},
        };
    }

    void RiverComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Depth")
                SetDepth(ParseFloat(property, m_depth));
            else if (property.name == "BankWidth")
                SetBankWidth(ParseFloat(property, m_bankWidth));
            else if (property.name == "SurfaceDrop")
                SetSurfaceDrop(ParseFloat(property, m_surfaceDrop));
            else if (property.name == "FlowSpeed")
                SetFlowSpeed(ParseFloat(property, m_flowSpeed));
            else if (property.name == "UseWaterMaterial")
                m_useWaterMaterial = property.value == "true" || property.value == "1";
        }
    }

    float RiverComponent::GetHalfWidth() const
    {
        const auto *spline = GetSpline();
        if (!spline)
            return 0.0f;
        return 0.5f * spline->GetWidth() * glm::length(glm::vec3(GetOwner()->GetWorldTransform()[0]));
    }

    std::vector<glm::vec3> RiverComponent::GetWorldPath() const
    {
        const auto *spline = GetSpline();
        if (!spline)
            return {};
        const glm::mat4 world = GetOwner()->GetWorldTransform();
        std::vector<glm::vec3> path;
        for (const auto &point : spline->SampleControlPoints(std::max(spline->GetSamplesPerSegment(), 4)))
            path.push_back(glm::vec3(world * glm::vec4(point.position, 1.0f)));
        return path;
    }

    void RiverComponent::ConfigureSpline()
    {
        auto *spline = GetSpline();
        if (!spline)
            return;
        spline->SetClosed(false);
        spline->SetThickness(0.02f);
        spline->SetGuardrailHeight(0.0f);
        // Water has no collision; Deserialize is the only setter for these flags.
        spline->Deserialize({{"GenerateCollision", PropertyType::Bool, "false"}, {"GenerateMesh", PropertyType::Bool, "true"}});
        if (m_useWaterMaterial)
        {
            spline->SetMaterialAssetReference({});
            Update(0.0f);
        }
    }

    bool RiverComponent::ConformToTerrain()
    {
        auto *spline = GetSpline();
        const auto *owner = GetOwner();
        const auto *scene = owner ? owner->GetScene() : nullptr;
        if (!spline || !scene)
            return false;
        const glm::mat4 world = owner->GetWorldTransform();
        const glm::mat4 toLocal = glm::inverse(world);
        auto points = spline->GetPoints();
        std::vector<glm::vec3> worldPoints;
        for (const auto &point : points)
            worldPoints.push_back(glm::vec3(world * glm::vec4(point.position, 1.0f)));
        bool anyGround = false;
        const auto conformed = ConformRiverPath(worldPoints, [&](glm::vec2 position) {
            auto ground = GroundAt(*scene, position);
            anyGround |= ground.has_value();
            return ground;
        }, m_surfaceDrop);
        if (!anyGround)
            return false;
        for (std::size_t i = 0; i < points.size(); ++i)
            points[i].position = glm::vec3(toLocal * glm::vec4(conformed[i], 1.0f));
        spline->SetPoints(std::move(points));
        return true;
    }

    std::vector<RiverComponent::CarvedTerrain> RiverComponent::CarveTerrain()
    {
        std::vector<CarvedTerrain> carved;
        const auto *owner = GetOwner();
        const auto *scene = owner ? owner->GetScene() : nullptr;
        const auto path = GetWorldPath();
        if (!scene || path.size() < 2)
            return carved;
        RiverBedShape shape;
        shape.halfWidth = GetHalfWidth();
        shape.depth = m_depth;
        shape.bankWidth = m_bankWidth;
        for (auto *terrain : scene->GetTerrainComponents())
        {
            auto *terrainOwner = terrain ? terrain->GetOwner() : nullptr;
            if (!terrainOwner || !terrain->IsEnabled() || !terrainOwner->IsActive())
                continue;
            const glm::mat4 toLocal = glm::inverse(terrainOwner->GetWorldTransform());
            std::vector<glm::vec3> local;
            local.reserve(path.size());
            for (const auto &point : path)
                local.push_back(glm::vec3(toLocal * glm::vec4(point, 1.0f)));
            auto heights = terrain->GetHeightData().field;
            const auto rect = CarveRiverBed(heights, terrain->GetCellSize(), local, shape);
            if (rect.Empty())
                continue;
            CarvedTerrain result;
            result.terrain = terrain;
            result.before = terrain->CaptureHeights(rect);
            TerrainHeightRegion region{rect, {}};
            for (int z = rect.z0; z < rect.z1; ++z)
                for (int x = rect.x0; x < rect.x1; ++x)
                    region.samples.push_back(heights.At(x, z));
            terrain->RestoreHeights(region);
            result.after = terrain->CaptureHeights(rect);
            carved.push_back(std::move(result));
        }
        return carved;
    }

    std::optional<WaterSurfaceSample> RiverComponent::SampleSurface(glm::vec3 worldPosition) const
    {
        const auto path = GetWorldPath();
        auto sample = SampleRiverPath(path, GetHalfWidth(), m_flowSpeed, worldPosition);
        if (sample)
        {
            sample->body = this;
            sample->entity = GetOwner() ? GetOwner()->GetID() : 0;
        }
        return sample;
    }
}
