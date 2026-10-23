#include "PlutoGE/assets/AssetManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

// Offline: merges a native mesh's submeshes into fewer submeshes so a character draws a few
// commands per pass instead of one per material. Each source submesh's UVs are moved into its
// rectangle of a shared texture atlas (built by the caller), and its generated LODs are kept:
// merged LOD k concatenates each source's LOD k, falling back to its full range as the
// renderer does for sources with fewer levels.
//
//   MergeMeshSubmeshes project-root input-reference --report
//     prints every submesh: material, LOD count, triangles and UV range.
//   MergeMeshSubmeshes project-root input-reference plan-file output-reference
//     plan lines: MATERIAL <reference>          (output material slots, in order)
//                 SUBMESH <material slot>       (starts an output submesh)
//                 SOURCE <submesh> <u0> <v0> <uScale> <vScale> <uMin> <vMin> <uMax> <vMax>
//                   uv is clamped to the packed range (a texture tiled twice packs as two copies),
//                   then uv' = (u0, v0) + (uv - min) * scale. Identity (0 0 1 1 0 0 1 1) keeps UVs.
namespace
{
    using PlutoGE::render::MeshConfig;
    using PlutoGE::render::Submesh;

    struct Source
    {
        std::size_t submesh = 0;
        float u0 = 0, v0 = 0, uScale = 1, vScale = 1, uMin = 0, vMin = 0, uMax = 1, vMax = 1;
    };
    struct Output
    {
        std::uint32_t material = 0;
        std::vector<Source> sources;
    };
    struct Plan
    {
        std::vector<std::string> materials;
        std::vector<Output> outputs;
    };

    Submesh::LodRange LodRange(const Submesh &submesh, std::size_t lod)
    {
        if (lod < submesh.lods.size() && submesh.lods[lod].indexCount > 0) return submesh.lods[lod];
        return {submesh.indexOffset, submesh.indexCount, 0.0f, std::numeric_limits<float>::max()};
    }

    std::size_t LodCount(const Submesh &submesh) { return std::max<std::size_t>(submesh.lods.size(), 1); }

    // Every vertex any LOD of the submesh reads.
    std::vector<std::uint32_t> UsedVertices(const MeshConfig &config, const Submesh &submesh)
    {
        std::vector<std::uint32_t> used;
        for (std::size_t lod = 0; lod < LodCount(submesh); ++lod)
        {
            const auto range = LodRange(submesh, lod);
            used.insert(used.end(), config.data.indices.begin() + range.indexOffset,
                        config.data.indices.begin() + range.indexOffset + range.indexCount);
        }
        std::sort(used.begin(), used.end());
        used.erase(std::unique(used.begin(), used.end()), used.end());
        return used;
    }

    void Report(const MeshConfig &config, const std::vector<std::string> &materials)
    {
        for (std::size_t index = 0; index < config.submeshes.size(); ++index)
        {
            const auto &submesh = config.submeshes[index];
            float minU = std::numeric_limits<float>::max(), minV = minU, maxU = -minU, maxV = -minU;
            for (const auto vertex : UsedVertices(config, submesh))
            {
                const auto &uv = config.data.vertices[vertex].uv;
                minU = std::min(minU, uv[0]); maxU = std::max(maxU, uv[0]);
                minV = std::min(minV, uv[1]); maxV = std::max(maxV, uv[1]);
            }
            std::cout << "SUBMESH " << index << " material=" << submesh.materialIndex << ' '
                      << (submesh.materialIndex < materials.size() ? materials[submesh.materialIndex] : "-")
                      << " lods=" << LodCount(submesh) << " triangles=" << submesh.indexCount / 3
                      << " uv=" << minU << ',' << minV << ',' << maxU << ',' << maxV << '\n';
        }
    }

    Plan ReadPlan(const std::filesystem::path &path)
    {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("Cannot read plan " + path.string());
        Plan plan;
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream words(line);
            std::string kind;
            if (!(words >> kind) || kind.starts_with('#')) continue;
            if (kind == "MATERIAL")
            {
                std::string reference;
                if (!(words >> reference)) throw std::runtime_error("MATERIAL needs a reference");
                plan.materials.push_back(reference);
            }
            else if (kind == "SUBMESH")
            {
                Output output;
                if (!(words >> output.material)) throw std::runtime_error("SUBMESH needs a material slot");
                plan.outputs.push_back(output);
            }
            else if (kind == "SOURCE")
            {
                Source source;
                if (plan.outputs.empty() || !(words >> source.submesh >> source.u0 >> source.v0 >> source.uScale >> source.vScale >>
                                                        source.uMin >> source.vMin >> source.uMax >> source.vMax))
                    throw std::runtime_error("SOURCE needs a SUBMESH and <submesh> <u0> <v0> <uScale> <vScale> <uMin> <vMin> <uMax> <vMax>");
                plan.outputs.back().sources.push_back(source);
            }
            else throw std::runtime_error("Unknown plan line: " + line);
        }
        return plan;
    }

    MeshConfig Merge(const MeshConfig &input, const Plan &plan)
    {
        std::vector<bool> taken(input.submeshes.size());
        for (const auto &output : plan.outputs)
        {
            if (output.material >= plan.materials.size()) throw std::runtime_error("SUBMESH material slot out of range");
            for (const auto &source : output.sources)
            {
                if (source.submesh >= input.submeshes.size() || taken[source.submesh])
                    throw std::runtime_error("SOURCE " + std::to_string(source.submesh) + " is out of range or used twice");
                taken[source.submesh] = true;
                if (input.submeshes[source.submesh].animatedNodeIndex != input.submeshes[output.sources.front().submesh].animatedNodeIndex)
                    throw std::runtime_error("Merged submeshes must follow the same animated node");
            }
        }
        if (std::find(taken.begin(), taken.end(), false) != taken.end())
            throw std::runtime_error("Every source submesh must be listed once (dropping geometry is not supported)");

        MeshConfig merged = input;
        merged.data.vertices.clear();
        merged.data.indices.clear();
        merged.submeshes.clear();
        for (const auto &output : plan.outputs)
        {
            // Vertices are copied per source: a vertex shared by two sources gets each one's atlas UVs.
            std::vector<std::map<std::uint32_t, std::uint32_t>> remap(output.sources.size());
            std::size_t lodCount = 1, reference = 0;
            for (std::size_t s = 0; s < output.sources.size(); ++s)
            {
                const auto &source = output.sources[s];
                const auto &submesh = input.submeshes[source.submesh];
                if (LodCount(submesh) > lodCount) { lodCount = LodCount(submesh); reference = s; }
                for (const auto vertex : UsedVertices(input, submesh))
                {
                    auto copy = input.data.vertices[vertex];
                    // A few hundredths past the packed range is an exporter seam, not tiling.
                    constexpr float tolerance = 0.05f;
                    if (copy.uv[0] < source.uMin - tolerance || copy.uv[0] > source.uMax + tolerance ||
                        copy.uv[1] < source.vMin - tolerance || copy.uv[1] > source.vMax + tolerance)
                        throw std::runtime_error("Submesh " + std::to_string(source.submesh) + " has UVs outside its packed range");
                    copy.uv[0] = source.u0 + (std::clamp(copy.uv[0], source.uMin, source.uMax) - source.uMin) * source.uScale;
                    copy.uv[1] = source.v0 + (std::clamp(copy.uv[1], source.vMin, source.vMax) - source.vMin) * source.vScale;
                    remap[s][vertex] = static_cast<std::uint32_t>(merged.data.vertices.size());
                    merged.data.vertices.push_back(copy);
                }
            }
            Submesh submesh;
            const auto &first = input.submeshes[output.sources.front().submesh];
            submesh.materialIndex = output.material;
            submesh.animatedNodeIndex = first.animatedNodeIndex;
            submesh.name = "Merged " + std::to_string(merged.submeshes.size());
            for (std::size_t lod = 0; lod < lodCount; ++lod)
            {
                const auto offset = static_cast<std::uint32_t>(merged.data.indices.size());
                for (std::size_t s = 0; s < output.sources.size(); ++s)
                {
                    const auto range = LodRange(input.submeshes[output.sources[s].submesh], lod);
                    for (std::uint32_t i = 0; i < range.indexCount; ++i)
                        merged.data.indices.push_back(remap[s].at(input.data.indices[range.indexOffset + i]));
                }
                const auto thresholds = LodRange(input.submeshes[output.sources[reference].submesh], lod);
                submesh.lods.push_back({offset, static_cast<std::uint32_t>(merged.data.indices.size()) - offset,
                                        thresholds.minDistanceFactor, thresholds.maxScreenRadiusPixels});
            }
            submesh.indexOffset = submesh.lods.front().indexOffset;
            submesh.indexCount = submesh.lods.front().indexCount;
            if (lodCount == 1) submesh.lods.clear();
            glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
            for (std::uint32_t i = 0; i < submesh.indexCount; ++i)
            {
                const auto &p = merged.data.vertices[merged.data.indices[submesh.indexOffset + i]].position;
                low = glm::min(low, glm::vec3(p[0], p[1], p[2]));
                high = glm::max(high, glm::vec3(p[0], p[1], p[2]));
            }
            submesh.boundsMin = low;
            submesh.boundsMax = high;
            submesh.hasBoundsExtents = true;
            submesh.bounds.center = (low + high) * 0.5f;
            submesh.bounds.radius = glm::length(high - low) * 0.5f;
            merged.submeshes.push_back(submesh);
        }
        return merged;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const bool report = argc == 4 && std::string_view(argv[3]) == "--report";
        if (!report && argc != 5)
            throw std::invalid_argument("Usage: MergeMeshSubmeshes project-root input-reference (--report | plan-file output-reference)");
        PlutoGE::assets::AssetManager manager;
        manager.SetProjectContext(argv[1]);
        MeshConfig config;
        std::vector<std::string> materials;
        PlutoGE::assets::MeshAssetMetadata metadata;
        std::string error;
        if (!manager.LoadMeshAssetData(argv[2], config, materials, metadata, &error))
            throw std::runtime_error("Could not load mesh: " + error);
        if (report)
        {
            Report(config, materials);
            return 0;
        }
        const auto plan = ReadPlan(argv[3]);
        const auto merged = Merge(config, plan);
        if (!manager.SaveMeshAsset(argv[4], merged, plan.materials, &error, metadata))
            throw std::runtime_error(error);
        std::cout << config.submeshes.size() << " submeshes merged into " << merged.submeshes.size() << "; "
                  << config.data.vertices.size() << " -> " << merged.data.vertices.size() << " vertices\n";
        Report(merged, plan.materials);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
