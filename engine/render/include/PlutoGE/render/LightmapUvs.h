#pragma once

#include "PlutoGE/render/Mesh.h"

#include <cstdint>
#include <span>

// Secondary (lightmap) UV generation and checks. A lightmap UV layout gives every triangle of a submesh its own
// texels: charts inside [0,1], not overlapping, with gutters between them so bilinear reads stay inside a chart.
namespace PlutoGE::render
{
    struct LightmapUvSettings
    {
        // Neighbouring triangles join a chart while their normals stay within this angle of the chart's first
        // triangle. Larger charts have fewer seams and more projection stretch.
        float maxChartAngleDegrees = 50.0f;
        // Empty space around each chart, as a share of the packed layout's side.
        float gutter = 0.015f;
        // Unwraps the geometry as if its positions were scaled per axis: charts get the proportions an instance has in
        // the world, so a stretched instance keeps an even texel density (Mesh::CreateScaleFittedLightmapUvs).
        glm::vec3 positionScale{1.0f};
    };

    // One index range unwrapped into its own [0,1] layout (normally a submesh: each submesh is lightmapped as its own
    // draw).
    struct LightmapUvRange
    {
        std::uint32_t indexOffset = 0;
        std::uint32_t indexCount = 0;
    };

    struct LightmapUvResult
    {
        std::size_t charts = 0;
        std::size_t addedVertices = 0;
    };

    // Writes uv2 for the ranges' triangles by planar charts: triangles grow into charts of similar normals, each chart
    // is projected onto its mean plane in world units (so texel density is even), turned to its smallest bounding box
    // and packed. Vertices shared by several charts are duplicated (appended), and the ranges' indices are
    // rewritten; other ranges and LODs referencing those vertices keep the first chart's uv2. Run before LODs exist.
    LightmapUvResult GenerateLightmapUvs(MeshData &data, std::span<const LightmapUvRange> ranges,
                                         const LightmapUvSettings &settings = {});
    // Every submesh as its own range.
    LightmapUvResult GenerateLightmapUvs(MeshData &data, std::span<const Submesh> submeshes,
                                         const LightmapUvSettings &settings = {});

    // What a range's uv2 layout offers a lightmap bake.
    struct LightmapUvCheck
    {
        std::size_t triangles = 0;
        // Triangles with no uv2 area: they receive no texels of their own.
        std::size_t degenerate = 0;
        // Triangles with uv2 outside [0,1]: they land in other draws' texels.
        std::size_t outside = 0;
        glm::vec2 boundsMin{0.0f};
        glm::vec2 boundsMax{0.0f};
        float uvArea = 0.0f; // summed uv2 triangle area
        [[nodiscard]] bool Usable() const { return triangles > 0 && outside == 0 && degenerate * 2 < triangles && uvArea > 0.0f; }
    };
    LightmapUvCheck CheckLightmapUvs(const MeshData &data, LightmapUvRange range);
}
