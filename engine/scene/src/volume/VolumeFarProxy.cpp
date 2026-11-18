#include "PlutoGE/scene/volume/VolumeFarProxy.h"

#include <algorithm>
#include <set>

namespace PlutoGE::scene::volume
{
    VolumeFarProxy BuildVolumeFarProxy(const TerrainVolume &fine, std::shared_ptr<const VolumeDensitySource> farBase,
                                       const TerrainSurface &farSurface, const VolumeMesher &mesher, const VolumeFarProxySettings &settings)
    {
        VolumeFarProxy proxy;
        if (!farBase || !farSurface.Valid())
            return proxy;
        const auto &fineGrid = fine.GetGrid();
        const int step = std::max(settings.step, 1);
        VolumeGrid coarseGrid = fineGrid;
        coarseGrid.voxelSize = fineGrid.voxelSize * static_cast<float>(step);
        TerrainVolume coarse(coarseGrid, farBase);
        coarse.SetMeshApron(mesher.Apron());

        // Coarse chunks over the fine volume's edits, sampled from it where it stored chunks and from the far base
        // elsewhere (coarse sample s is fine sample s * step: the grids share their origin).
        std::set<VolumeChunkCoord> edited;
        for (const auto chunk : fine.GetStoredChunks())
        {
            const auto first = fineGrid.ChunkFirstSample(chunk);
            const VolumeSampleBox coarseBox{glm::ivec3(FloorDiv(first.x, step), FloorDiv(first.y, step), FloorDiv(first.z, step)),
                                            glm::ivec3(fineGrid.chunkVoxels / step + 2)};
            coarseGrid.ForEachChunk(coarseBox, [&](VolumeChunkCoord coarseChunk) { edited.insert(coarseChunk); });
        }
        for (const auto chunk : edited)
        {
            const auto first = coarseGrid.ChunkFirstSample(chunk);
            const int side = coarseGrid.chunkVoxels;
            std::vector<std::int8_t> density(static_cast<std::size_t>(side) * side * side);
            std::vector<VolumeMaterial> material(density.size());
            std::size_t index = 0;
            for (int z = 0; z < side; ++z)
                for (int y = 0; y < side; ++y)
                    for (int x = 0; x < side; ++x, ++index)
                    {
                        const glm::ivec3 coarseSample = first + glm::ivec3(x, y, z);
                        const glm::ivec3 fineSample = coarseSample * step;
                        const auto sample = fine.FindChunk(fineGrid.ChunkOfSample(fineSample))
                            ? fine.Sample(fineSample)
                            : farBase->Sample(coarseGrid.SamplePosition(coarseSample));
                        density[index] = QuantizeDistance(sample.distance, coarseGrid.voxelSize);
                        material[index] = sample.material;
                    }
            VolumeChunk samples;
            if (samples.Assign(side, std::move(density), std::move(material)))
                coarse.SetChunk(chunk, std::move(samples));
        }

        proxy.holes = ComputeVolumeHoles(coarse, farSurface, settings.coupling);
        std::set<VolumeChunkCoord> chunks(edited.begin(), edited.end());
        for (const auto chunk : coarse.GetBaseFeatureChunks())
            chunks.insert(chunk);
        for (const auto chunk : chunks)
        {
            auto mesh = MeshVolumeChunk(coarse, chunk, mesher);
            const auto origin = coarseGrid.ChunkOrigin(chunk);
            FitVolumeMeshToTerrain(mesh, origin, farSurface, proxy.holes, settings.coupling, coarseGrid.voxelSize);
            if (mesh.Empty())
                continue;
            ApplyBurialOcclusion(mesh, origin, farSurface, settings.shading);
            EncodeTriangleMaterials(mesh);
            proxy.pieces.push_back({origin, std::move(mesh)});
        }
        return proxy;
    }
}
