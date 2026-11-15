#pragma once

#include "PlutoGE/scene/TerrainHeightData.h"
#include "PlutoGE/scene/WaterBodies.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace PlutoGE::render
{
    class Material;
}

namespace PlutoGE::scene
{
    class SplineComponent;
    class TerrainComponent;

    // A river: water along the SplineComponent on the same entity, which draws it as a thin ribbon (its width is the
    // water's width; it flows from the first point to the last). The river keeps its spline flowing downhill on the
    // terrain, carves its bed into the terrain, answers water queries (WaterBodies.h), and is a distance source for
    // procedural rules (tag the entity, e.g. "River", and use a Distance rule).
    class RiverComponent : public TypedComponent<RiverComponent>
    {
    public:
        RiverComponent();
        ~RiverComponent() override;

        // Gives a spline still drawn with the default material a water look (once it has one).
        void Update(float deltaTime) override;
        bool RequiresFrameUpdate() const override { return m_useWaterMaterial; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        SplineComponent *GetSpline() const;

        // Depth of the bed below the water surface at the centre, metres.
        float GetDepth() const { return m_depth; }
        void SetDepth(float depth) { m_depth = std::max(depth, 0.1f); }
        // Distance beyond the water over which a carved bed rises back to the terrain.
        float GetBankWidth() const { return m_bankWidth; }
        void SetBankWidth(float width) { m_bankWidth = std::max(width, 0.0f); }
        // How far below the ground the water surface sits when the river is conformed.
        float GetSurfaceDrop() const { return m_surfaceDrop; }
        void SetSurfaceDrop(float drop) { m_surfaceDrop = drop; }
        float GetFlowSpeed() const { return m_flowSpeed; }
        void SetFlowSpeed(float speed) { m_flowSpeed = std::max(speed, 0.0f); }
        bool GetUseWaterMaterial() const { return m_useWaterMaterial; }
        void SetUseWaterMaterial(bool use) { m_useWaterMaterial = use; }

        // Half the water's width in world units.
        float GetHalfWidth() const;
        // The water surface along the river, in world space (the spline sampled from source to mouth).
        std::vector<glm::vec3> GetWorldPath() const;
        // Makes the spline a water ribbon: open, thin, no collision.
        void ConfigureSpline();
        // Moves the spline's control points onto the terrain under them (minus the surface drop), never uphill.
        // False without a spline or a terrain under any point.
        bool ConformToTerrain();

        struct CarvedTerrain
        {
            TerrainComponent *terrain = nullptr;
            TerrainHeightRegion before; // for undo
            TerrainHeightRegion after;
        };
        // Lowers every terrain the river crosses into its bed. Terrains must be axis aligned and unscaled.
        std::vector<CarvedTerrain> CarveTerrain();

        std::optional<WaterSurfaceSample> SampleSurface(glm::vec3 worldPosition) const;

    private:
        float m_depth = 1.5f;
        float m_bankWidth = 4.0f;
        float m_surfaceDrop = 0.3f;
        float m_flowSpeed = 1.0f;
        bool m_useWaterMaterial = true;
        std::unique_ptr<render::Material> m_waterMaterial;
    };
}
