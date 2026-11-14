#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/DetailGrass.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // Detail grass on the GPU (docs/DETAIL_GRASS.md). Each frame, for every field (a grass layer over a terrain):
    //   Prepare lays out the layer's lattice around the camera (DetailGrassLattice), uploads the terrain's samples when
    //   they change, and the parameters and empty indirect draws of the frame; and builds one BasicDraw per field whose
    //   mesh is the layer's clump and whose BasicDraw::detailGrass names where its clumps will be.
    //   Record dispatches the generation pass (DetailGrassGenerate.slang): one thread per candidate of every ring,
    //   appending the clumps that grow and are in view to their LOD's region and counting them into its draw.
    // The geometry passes then draw each field as one indirect draw per LOD through the BasicLitGrass variant
    // (BasicRenderer::GeometryPipeline). Nothing is read back to decide anything: counts reach the CPU only as
    // diagnostics (Stats), frames later.
    class DetailGrassRenderer
    {
    public:
        using Shaders = BasicDetailGrassShaderPackage;

        struct View
        {
            glm::mat4 viewProjection{1.0f};
            glm::vec3 cameraPosition{0.0f};
            // This frame's wind: swaying clumps' bounds are padded by its reach (by any wind's without it).
            const FoliageWindFrame *wind = nullptr;
        };

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept { return static_cast<bool>(m_generate); }

        // Uploads the frame's data and builds its draws, on the calling thread, outside rendering. `renderer` creates
        // the clump meshes (kept while layers use them). Fields without valid heights, or disabled, are skipped.
        void Prepare(BasicRenderer &renderer, std::span<const BasicDetailGrassField> fields, const View &view);
        // Records the generation dispatches into `commands`, outside rendering and before any draw of Draws().
        void Record(rhi::ICommandContext &commands);

        // This frame's draws (after Prepare), for the coverage depth and geometry passes.
        [[nodiscard]] std::span<const BasicDraw> Draws() const noexcept { return m_draws; }
        // Candidates this frame's dispatches cover, every field together.
        [[nodiscard]] std::size_t Candidates() const noexcept { return m_candidates; }
        [[nodiscard]] DetailGrassStats Stats() const { return *m_stats; }

    private:
        struct ClumpMesh
        {
            DetailGrassClumpShape shape;
            DetailGrassClumpMesh source;
            BasicMesh mesh;
            std::uint64_t lastFrame = 0;
        };
        struct TerrainSamples
        {
            std::weak_ptr<const DetailGrassHeightField> source;
            const DetailGrassHeightField *key = nullptr;
            std::uint64_t revision = 0;
            rhi::Buffer buffer;
            std::uint64_t lastFrame = 0;
        };
        struct Dispatch
        {
            rhi::BufferHandle samples;
            rhi::TextureHandle density, groundAlbedo;
            glm::uvec3 groups{0u};
        };

        const ClumpMesh &Clump(BasicRenderer &renderer, const DetailGrassClumpShape &shape);
        rhi::BufferHandle Samples(const std::shared_ptr<const DetailGrassHeightField> &heights);
        void Collect();

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_generate; // compute
        rhi::Sampler m_sampler;
        rhi::Texture m_fallback;
        rhi::Buffer m_instances, m_arguments;
        std::size_t m_instanceCapacity = 0, m_argumentCapacity = 0;
        std::vector<rhi::Buffer> m_parameters; // one uniform block per field, reused across frames
        std::vector<std::unique_ptr<ClumpMesh>> m_clumps;
        std::vector<TerrainSamples> m_samples;
        std::vector<Dispatch> m_dispatches;
        std::vector<BasicDraw> m_draws;
        std::size_t m_argumentBytes = 0, m_candidates = 0;
        std::uint64_t m_frame = 0;
        std::shared_ptr<DetailGrassStats> m_stats = std::make_shared<DetailGrassStats>();
    };
}
