#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/procedural/ProceduralGeneration.h"
#include "PlutoGE/scene/procedural/ProceduralTerrainDefinition.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class TerrainComponent;
    class FoliageComponent;

    // Generates a terrain's material layer weights and foliage from biomes (procedural::ProceduralTerrainDefinition):
    // which goes on the entity of the TerrainComponent it paints, beside the FoliageComponent it fills. Results go
    // where the engine already reads them, the terrain's weight map and ordinary foliage instances (procedural IDs), so
    // rendering, collision, detail grass and GI need nothing new. Hand-placed foliage is never touched.
    //
    // Generation runs in three steps so streamed terrain can do the middle one on a worker thread:
    // Prepare (main thread: copies the heights, masks and distance sources it reads), Run (pure), Apply (main thread).
    class ProceduralTerrainComponent : public TypedComponent<ProceduralTerrainComponent>
    {
    public:
        // What generation reads, copied out of the scene; Run does not touch the scene.
        struct PreparedGeneration
        {
            procedural::ProceduralTerrainDefinition definition;
            procedural::TerrainSurface surface;
            std::shared_ptr<procedural::ProceduralInputs> inputs;
            std::vector<std::string> layerNames;
            std::vector<std::string> foliageTypes;
            std::vector<WaterHabitat> foliageHabitats; // by type index
            int weightResolution = 256;
            bool texture = true;
            bool scatter = true;
            std::vector<std::string> warnings; // found while preparing (masks that did not load)

            [[nodiscard]] procedural::ProceduralGenerationResult Run() const;
        };

        ProceduralTerrainComponent() = default;

        void Update(float deltaTime) override;
        bool RequiresFrameUpdate() const override { return m_generateOnLoad && !m_generated; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const procedural::ProceduralTerrainDefinition &GetDefinition() const { return m_definition; }
        void SetDefinition(procedural::ProceduralTerrainDefinition definition) { m_definition = std::move(definition); }
        // Generates by itself the first time it updates (streamed tiles, runtime-only content). The results are
        // regenerated rather than saved.
        bool GetGenerateOnLoad() const { return m_generateOnLoad; }
        void SetGenerateOnLoad(bool generate) { m_generateOnLoad = generate; }
        // Weight map texels across the terrain; 0 keeps the terrain's current resolution.
        int GetWeightResolution() const { return m_weightResolution; }
        void SetWeightResolution(int resolution) { m_weightResolution = resolution <= 0 ? 0 : std::clamp(resolution, 16, 4096); }
        bool GetTexture() const { return m_texture; }
        void SetTexture(bool texture) { m_texture = texture; }
        bool GetScatter() const { return m_scatter; }
        void SetScatter(bool scatter) { m_scatter = scatter; }

        TerrainComponent *GetTerrain() const;
        FoliageComponent *GetFoliage() const;

        std::optional<PreparedGeneration> Prepare(std::string *error = nullptr) const;
        // Writes the weights into the terrain and replaces the foliage's procedural instances.
        void Apply(const procedural::ProceduralGenerationResult &result);
        // Prepare, Run and Apply now. Nothing changes when preparing fails (the warnings say why).
        procedural::ProceduralGenerationResult Generate();
        bool HasGenerated() const { return m_generated; }
        const std::vector<std::string> &GetLastWarnings() const { return m_lastWarnings; }

        // Mask images decoded once and shared by every component that places them, keyed by resolved path and channel.
        static std::shared_ptr<const TerrainHeightField> LoadMaskImage(const std::string &image, int channel, std::string *error = nullptr);
        static void ClearMaskCache();

    private:
        procedural::ProceduralTerrainDefinition m_definition;
        bool m_generateOnLoad = false;
        int m_weightResolution = 0;
        bool m_texture = true;
        bool m_scatter = true;
        bool m_generated = false;
        std::vector<std::string> m_lastWarnings;
    };
}
