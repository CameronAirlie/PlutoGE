// Terrain volumes without an engine (docs/dev/TERRAIN_VOLUMES_PLAN.md): the grid, quantized samples, density sources,
// sparse edits and their dirty chunks, the volume file, and the surface nets mesher (closed, consistently wound and
// seamless across chunks).
#include "PlutoGE/scene/volume/TerrainVolume.h"
#include "PlutoGE/scene/volume/TerrainVolumeCoupling.h"
#include "PlutoGE/scene/volume/CaveDensity.h"
#include "PlutoGE/scene/volume/VolumeEditArchive.h"
#include "PlutoGE/scene/volume/VolumeFarProxy.h"
#include "PlutoGE/scene/volume/VolumeFile.h"
#include "PlutoGE/scene/volume/VolumeMesher.h"
#include "PlutoGE/scene/volume/VolumeMeshQueue.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>
#include <atomic>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <tuple>

using namespace PlutoGE;
using namespace PlutoGE::scene::volume;
using PlutoGE::scene::TerrainHoleCut;
using PlutoGE::scene::TerrainHoleMask;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }

    // Small chunks keep the tests quick: 8 samples of 0.5 m, so a chunk is 4 m.
    VolumeGrid SmallGrid()
    {
        VolumeGrid grid;
        grid.voxelSize = 0.5f;
        grid.chunkVoxels = 8;
        return grid;
    }

    std::shared_ptr<HeightfieldDensity> FlatGround(float height, VolumeMaterial material = 1)
    {
        return std::make_shared<HeightfieldDensity>([height](float, float) { return std::optional<float>(height); }, material);
    }

    glm::vec3 Position(const render::MeshVertexData &vertex)
    {
        return {vertex.position[0], vertex.position[1], vertex.position[2]};
    }

    glm::vec3 Normal(const render::MeshVertexData &vertex)
    {
        return {vertex.normal[0], vertex.normal[1], vertex.normal[2]};
    }

    // Triangles in the volume's space, from any number of meshes and their origins.
    struct Soup
    {
        std::vector<std::array<glm::vec3, 3>> triangles;
        std::vector<std::array<glm::vec3, 3>> normals;

        void Add(const VolumeMesh &mesh, const glm::vec3 &origin)
        {
            const auto &vertices = mesh.mesh.vertices;
            for (std::size_t index = 0; index + 2 < mesh.mesh.indices.size(); index += 3)
            {
                std::array<glm::vec3, 3> triangle{}, normal{};
                for (int corner = 0; corner < 3; ++corner)
                {
                    const auto &vertex = vertices[mesh.mesh.indices[index + corner]];
                    triangle[corner] = Position(vertex) + origin;
                    normal[corner] = Normal(vertex);
                }
                triangles.push_back(triangle);
                normals.push_back(normal);
            }
        }
    };

    using Key = std::tuple<long long, long long, long long>;
    Key KeyOf(const glm::vec3 &position)
    {
        const auto snap = [](float value) { return static_cast<long long>(std::llround(static_cast<double>(value) * 1.0e4)); };
        return {snap(position.x), snap(position.y), snap(position.z)};
    }

    // A closed, consistently wound surface uses every directed edge exactly once and its reverse exactly once.
    void RequireClosedAndConsistent(const Soup &soup, const std::string &what)
    {
        std::map<std::pair<Key, Key>, int> directed;
        for (const auto &triangle : soup.triangles)
            for (int corner = 0; corner < 3; ++corner)
                ++directed[{KeyOf(triangle[corner]), KeyOf(triangle[(corner + 1) % 3])}];
        for (const auto &[edge, count] : directed)
        {
            Require(count == 1, what + ": an edge is used twice in one direction (overlap or flipped triangle)");
            const auto reverse = directed.find({edge.second, edge.first});
            Require(reverse != directed.end() && reverse->second == 1, what + ": an edge has no neighbour (a gap)");
        }
    }

    void GridAndQuantization()
    {
        Require(FloorDiv(-1, 8) == -1 && FloorDiv(-8, 8) == -1 && FloorDiv(-9, 8) == -2 && FloorDiv(7, 8) == 0 && FloorDiv(8, 8) == 1,
                "Floor division rounds towards negative infinity");
        const auto grid = SmallGrid();
        Require(grid.ChunkAt({-0.1f, 0.0f, 3.9f}) == VolumeChunkCoord{-1, 0, 0} && grid.ChunkAt({4.0f, -4.0f, 0.0f}) == VolumeChunkCoord{1, -1, 0},
                "Positions find their chunk on both sides of the origin");
        const auto box = grid.SamplesIn({-0.2f, 0.0f, 0.0f}, {1.1f, 0.5f, 0.0f});
        // x: -0.2 m floors to sample -1 (-0.5 m), 1.1 m ceils to sample 3 (1.5 m).
        Require(box.first == glm::ivec3(-1, 0, 0) && box.count == glm::ivec3(5, 2, 1), "SamplesIn rounds outwards");
        Require(box.Intersect({{1, 0, 0}, {10, 10, 10}}) == VolumeSampleBox{{1, 0, 0}, {3, 2, 1}} &&
                    box.Intersect({{5, 0, 0}, {1, 1, 1}}).Empty(),
                "Boxes intersect");
        int chunks = 0;
        grid.ForEachChunk({{-1, 0, 0}, {10, 1, 1}}, [&](VolumeChunkCoord) { ++chunks; });
        Require(chunks == 3, "A box from sample -1 to 8 touches chunks -1, 0 and 1");

        const float step = grid.voxelSize / kDensityStepsPerVoxel;
        for (float distance : {0.0f, 0.3f, -0.71f, 1.2f})
            Require(Near(DequantizeDistance(QuantizeDistance(distance, grid.voxelSize), grid.voxelSize), distance, step * 0.5f + 1e-6f),
                    "Quantized distances are within half a step near the surface");
        Require(Near(DequantizeDistance(QuantizeDistance(100.0f, grid.voxelSize), grid.voxelSize), kDensityRangeVoxels * grid.voxelSize, 1e-5f) &&
                    QuantizeDistance(-100.0f, grid.voxelSize) == -127,
                "Distances clamp at the stored range");
    }

    void Sources()
    {
        const auto grid = SmallGrid();
        const auto ground = FlatGround(10.0f, 2);
        Require(Near(ground->Sample({3.0f, 12.5f, -7.0f}).distance, 2.5f, 1e-6f) && ground->Sample({0.0f, 9.0f, 0.0f}).material == 2,
                "Heightfield distance is height above the ground");
        const HeightfieldDensity offTerrain([](float x, float) { return x < 0.0f ? std::nullopt : std::optional<float>(0.0f); });
        Require(offTerrain.Sample({-1.0f, -50.0f, 0.0f}).distance > 0.0f, "Off the terrain is air");

        // A heightfield's bulk fill matches its per-sample answers.
        const VolumeSampleBox box{{-2, 18, 3}, {3, 4, 2}};
        std::vector<VolumeSample> filled(box.SampleCount());
        ground->Fill(grid, box, filled);
        std::size_t index = 0;
        for (int z = 0; z < box.count.z; ++z)
            for (int y = 0; y < box.count.y; ++y)
                for (int x = 0; x < box.count.x; ++x)
                    Require(filled[index++] == ground->Sample(grid.SamplePosition(box.first + glm::ivec3(x, y, z))), "Fill matches Sample");

        // Caves: a tunnel subtracted from the ground, and a boulder united on top.
        CompositeDensity composite;
        composite.Add(ground, CompositeDensity::Operation::Union);
        composite.Add(std::make_shared<ShapeDensity>(std::make_shared<CapsuleShape>(glm::vec3(0, 8, 0), glm::vec3(10, 8, 0), 1.0f)),
                      CompositeDensity::Operation::Subtract);
        composite.Add(std::make_shared<ShapeDensity>(std::make_shared<SphereShape>(glm::vec3(0, 11, 20), 2.0f), 7),
                      CompositeDensity::Operation::Union);
        Require(composite.Sample({5.0f, 8.0f, 0.0f}).distance > 0.0f && composite.Sample({5.0f, 6.0f, 0.0f}).distance < 0.0f,
                "A subtracted tunnel is air inside rock");
        const auto boulder = composite.Sample({0.0f, 11.0f, 20.0f});
        Require(boulder.distance < 0.0f && boulder.material == 7, "A united boulder is rock of its own material");
        std::vector<VolumeSample> compositeFill(box.SampleCount());
        composite.Fill(grid, box, compositeFill);
        Require(compositeFill[5] == composite.Sample(grid.SamplePosition(box.first + glm::ivec3(2, 1, 0))), "Composite fill matches Sample");

        Require(Near(BoxShape({0, 0, 0}, {1, 2, 3}).Distance({0, 5, 0}), 3.0f, 1e-5f) && BoxShape({0, 0, 0}, {1, 2, 3}).Distance({0, 0, 0}) < 0.0f,
                "Box distances");
        const BoxShape turned({0, 0, 0}, {2, 0.5f, 0.5f}, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
        Require(turned.Distance({0, 0, 1.9f}) < 0.0f && turned.Distance({1.9f, 0, 0}) > 0.0f && Near(turned.BoundsMax().z, 2.0f, 1e-4f),
                "A turned box turns its distance and its bounds");
        Require(Near(CapsuleShape({0, 0, 0}, {4, 0, 0}, 1.0f).Distance({2, 3, 0}), 2.0f, 1e-5f), "Capsule distances");
    }

    void Edits()
    {
        const auto grid = SmallGrid();
        TerrainVolume volume(grid, FlatGround(10.0f, 1));
        Require(Near(volume.Sample({0, 22, 0}).distance, 1.0f, 1e-5f) && volume.GetStoredChunkCount() == 0, "An unedited volume reads its base");

        // An edit that changes nothing stores nothing.
        auto result = volume.Apply(SphereShape({2.0f, 40.0f, 2.0f}, 1.0f), {VolumeEditMode::Subtract});
        Require(!result.Changed() && volume.GetStoredChunkCount() == 0 && volume.GetDirtyChunks().empty() && volume.GetRevision() == 0,
                "Digging air changes nothing and stores nothing");

        // Digging into the ground.
        const glm::vec3 centre(2.0f, 10.0f, 2.0f);
        result = volume.Apply(SphereShape(centre, 1.5f), {VolumeEditMode::Subtract});
        Require(result.Changed() && volume.GetRevision() == 1, "Digging the ground changes it");
        Require(volume.Sample(grid.SampleFloor(centre - glm::vec3(0.0f, 1.0f, 0.0f))).distance > 0.0f, "The dug space is air");
        Require(volume.GetStoredChunkCount() == result.changedChunks.size() && !volume.FindChunk({5, 2, 5}),
                "Only the chunks the edit changed are stored");
        // The sphere reaches from x = 0.5 to 3.5 m: within chunk 0, but its changes come within the mesh apron of
        // chunk -1's mesh on x and z, so those meshes are dirty too, as is the chunk below (y = 10 is sample 20, chunk 2).
        const auto &dirty = volume.GetDirtyChunks();
        Require(dirty.contains({0, 2, 0}) && dirty.contains({-1, 2, 0}) && dirty.contains({0, 2, -1}) && dirty.contains({0, 1, 0}),
                "Edits dirty the meshes that read the samples they changed");
        Require(!dirty.contains({2, 2, 0}) && !dirty.contains({0, 4, 0}), "Edits do not dirty meshes far away");
        const auto dirtyCount = dirty.size();
        Require(volume.TakeDirtyChunks().size() == dirtyCount && volume.GetDirtyChunks().empty(), "Taking the dirty chunks clears them");

        // A stored chunk reads exactly as its base did where nothing changed.
        const auto storedCoord = result.changedChunks.front();
        const auto base = volume.MakeBaseChunk(storedCoord);
        const auto *stored = volume.FindChunk(storedCoord);
        const auto far = glm::ivec3(grid.chunkVoxels - 1);
        Require(stored && stored->GetDensity(far) == base.GetDensity(far) &&
                    volume.Sample(grid.ChunkFirstSample(storedCoord) + far).distance ==
                        DequantizeDistance(base.GetDensity(far), grid.voxelSize),
                "Untouched samples of a stored chunk keep the base's values");

        // Building up, and painting.
        volume.Apply(SphereShape({20.0f, 12.0f, 20.0f}, 1.0f), {VolumeEditMode::Add, 3});
        const auto built = volume.Sample(grid.SampleFloor({20.0f, 12.0f, 20.0f}));
        Require(built.distance < 0.0f && built.material == 3, "Adding makes rock of the edit's material");
        const auto beforePaint = volume.Sample(grid.SampleFloor({-6.0f, 9.75f, -6.0f}));
        result = volume.Apply(BoxShape({-6.0f, 10.0f, -6.0f}, {1.0f, 1.0f, 1.0f}), {VolumeEditMode::Paint, 5});
        const auto painted = volume.Sample(grid.SampleFloor({-6.0f, 9.75f, -6.0f}));
        Require(result.Changed() && painted.material == 5 && painted.distance == beforePaint.distance, "Painting changes material, not shape");
        Require(volume.Sample(grid.SampleFloor({-6.0f, 10.75f, -6.0f})).material == 1, "Painting leaves the air well above the surface alone");

        // Chunks set and removed directly (loading, undo).
        Require(!volume.SetChunk({9, 9, 9}, VolumeChunk(4, 0, 0)), "A chunk of the wrong size is refused");
        Require(volume.SetChunk({9, 9, 9}, VolumeChunk(grid.chunkVoxels, -10, 4)) && volume.Sample(grid.ChunkFirstSample({9, 9, 9})).material == 4,
                "A set chunk is read");
        Require(volume.RemoveChunk({9, 9, 9}) && !volume.RemoveChunk({9, 9, 9}) && volume.Sample(grid.ChunkFirstSample({9, 9, 9})).distance > 0.0f,
                "A removed chunk reads its base again");
    }

    void Files()
    {
        const auto grid = SmallGrid();
        const auto ground = FlatGround(10.0f, 1);
        TerrainVolume volume(grid, ground);
        volume.Apply(CapsuleShape({0, 10, 0}, {12, 8, 3}, 1.25f), {VolumeEditMode::Subtract});
        volume.Apply(SphereShape({5, 11, 5}, 1.0f), {VolumeEditMode::Add, 6});
        const auto bytes = VolumeFile::Encode(volume);
        const std::size_t raw = volume.GetStoredChunkCount() * 2 * static_cast<std::size_t>(grid.chunkVoxels * grid.chunkVoxels * grid.chunkVoxels);
        Require(bytes.size() < raw / 2, "Runs make stored chunks smaller (" + std::to_string(bytes.size()) + " of " + std::to_string(raw) + " bytes)");

        TerrainVolume loaded(grid, ground);
        std::string error;
        Require(VolumeFile::Load(bytes, loaded, &error), "Loading: " + error);
        Require(loaded.GetStoredChunks() == volume.GetStoredChunks(), "Every stored chunk loads");
        for (const auto coord : volume.GetStoredChunks())
            Require(*loaded.FindChunk(coord) == *volume.FindChunk(coord), "Loaded chunks are identical");
        Require(VolumeFile::Encode(loaded) == bytes, "Encoding is stable");

        auto otherGrid = grid;
        otherGrid.voxelSize = 1.0f;
        TerrainVolume other(otherGrid, ground);
        Require(!VolumeFile::Load(bytes, other, &error) && other.GetStoredChunkCount() == 0, "A file for another grid is refused");
        auto truncated = bytes;
        truncated.resize(truncated.size() - 3);
        Require(!VolumeFile::Decode(truncated, &error), "A truncated file is refused");
        auto newer = bytes;
        newer[4] = 2;
        Require(!VolumeFile::Decode(newer, &error) && error.find("newer") != std::string::npos, "A newer file is refused as newer");
        auto trailing = bytes;
        trailing.push_back(0);
        Require(!VolumeFile::Decode(trailing), "Data past the last chunk is refused");
        Require(!VolumeFile::Decode(std::vector<std::uint8_t>{'P', 'L', 'V', 'O', 1, 0, 0, 0}), "A header cut short is refused");
        auto huge = VolumeFile::Encode(grid, {});
        for (std::size_t index = huge.size() - 4; index < huge.size(); ++index)
            huge[index] = 0xff; // claims four billion chunks
        Require(!VolumeFile::Decode(huge), "A chunk count the file cannot hold is refused before allocating");
    }

    void MeshSphere()
    {
        const auto grid = SmallGrid();
        TerrainVolume volume(grid, ConstantDensity::Air());
        const glm::vec3 centre(0.3f, 0.2f, 0.1f);
        const float radius = 3.0f;
        volume.Apply(SphereShape(centre, radius), {VolumeEditMode::Add, 3});
        const SurfaceNetsMesher mesher;

        // Chunk by chunk: the sphere spans chunks -1 and 0 on every axis.
        Soup chunks;
        std::size_t chunkTriangles = 0;
        for (int z = -1; z <= 0; ++z)
            for (int y = -1; y <= 0; ++y)
                for (int x = -1; x <= 0; ++x)
                {
                    const auto mesh = MeshVolumeChunk(volume, {x, y, z}, mesher);
                    Require(!mesh.Empty() && mesh.materials.size() == mesh.mesh.vertices.size(), "Each chunk meshes its part of the sphere");
                    for (const auto material : mesh.materials)
                        Require(material == 3, "Vertices carry the rock's material");
                    chunks.Add(mesh, grid.ChunkOrigin({x, y, z}));
                    chunkTriangles += mesh.mesh.indices.size() / 3;
                }
        Require(MeshVolumeChunk(volume, {2, 0, 0}, mesher).Empty(), "A chunk the surface misses has no mesh");
        RequireClosedAndConsistent(chunks, "The sphere meshed chunk by chunk");
        // The check itself catches a gap and a flipped triangle.
        const auto rejects = [](const Soup &soup) {
            try
            {
                RequireClosedAndConsistent(soup, "control");
            }
            catch (const std::runtime_error &)
            {
                return true;
            }
            return false;
        };
        auto gap = chunks;
        gap.triangles.pop_back();
        auto flipped = chunks;
        std::swap(flipped.triangles.front()[1], flipped.triangles.front()[2]);
        Require(rejects(gap) && rejects(flipped), "The closed-surface check notices a gap and a flipped triangle");

        // In one go: the same number of triangles, so the chunks neither overlap nor miss any.
        const VolumeSampleBox all{grid.ChunkFirstSample({-1, -1, -1}), glm::ivec3(grid.chunkVoxels * 2)};
        const auto whole = mesher.Mesh(volume.Gather(mesher.RequiredSamples(all)), all, grid, glm::vec3(0.0f));
        Require(whole.mesh.indices.size() / 3 == chunkTriangles, "Chunks own disjoint parts of one surface");

        for (std::size_t index = 0; index < chunks.triangles.size(); ++index)
        {
            const auto &triangle = chunks.triangles[index];
            const glm::vec3 faceNormal = glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
            const glm::vec3 middle = (triangle[0] + triangle[1] + triangle[2]) / 3.0f;
            Require(glm::dot(faceNormal, middle - centre) > 0.0f, "Triangles are wound counter-clockwise seen from outside");
            for (int corner = 0; corner < 3; ++corner)
            {
                Require(Near(glm::length(triangle[corner] - centre), radius, grid.voxelSize * 0.25f), "Vertices lie on the sphere");
                Require(glm::dot(chunks.normals[index][corner], glm::normalize(triangle[corner] - centre)) > 0.9f, "Normals point out of the rock");
            }
        }

        // Deterministic, and an under-sized block is refused.
        const auto again = MeshVolumeChunk(volume, {0, 0, 0}, mesher);
        const auto first = MeshVolumeChunk(volume, {0, 0, 0}, mesher);
        Require(again.mesh.indices == first.mesh.indices && again.mesh.vertices.size() == first.mesh.vertices.size() &&
                    Position(again.mesh.vertices.back()) == Position(first.mesh.vertices.back()),
                "Meshing is deterministic");
        const auto region = grid.ChunkSamples({0, 0, 0});
        Require(mesher.Mesh(volume.Gather(region), region, grid, glm::vec3(0.0f)).Empty(), "A block without the apron is refused");
    }

    void MeshGround()
    {
        const auto grid = SmallGrid();
        TerrainVolume volume(grid, FlatGround(10.3f, 2));
        const SurfaceNetsMesher mesher;
        const VolumeChunkCoord chunk{0, 2, 0}; // samples 16..23 on y: 8 m to 11.5 m
        const auto ground = MeshVolumeChunk(volume, chunk, mesher);
        Require(ground.mesh.indices.size() / 3 == 2u * grid.chunkVoxels * grid.chunkVoxels, "Flat ground is two triangles per column");
        const auto origin = grid.ChunkOrigin(chunk);
        for (std::size_t index = 0; index < ground.mesh.vertices.size(); ++index)
        {
            const auto &vertex = ground.mesh.vertices[index];
            Require(Near(Position(vertex).y + origin.y, 10.3f, grid.voxelSize / kDensityStepsPerVoxel), "The volume's ground is the heightfield's");
            Require(Normal(vertex).y > 0.999f && ground.materials[index] == 2, "Flat ground faces up, in the ground's material");
        }

        // A dug pit: the edited chunk and its neighbours still meet without gaps or overlaps.
        volume.Apply(SphereShape({4.0f, 10.3f, 4.0f}, 2.2f), {VolumeEditMode::Subtract});
        Soup soup;
        std::size_t triangles = 0;
        for (int z = 0; z <= 1; ++z)
            for (int x = 0; x <= 1; ++x)
                for (int y = 1; y <= 2; ++y)
                {
                    const auto mesh = MeshVolumeChunk(volume, {x, y, z}, mesher);
                    soup.Add(mesh, grid.ChunkOrigin({x, y, z}));
                    triangles += mesh.mesh.indices.size() / 3;
                }
        const VolumeSampleBox all{grid.ChunkFirstSample({0, 1, 0}), glm::ivec3(grid.chunkVoxels * 2)};
        const auto whole = mesher.Mesh(volume.Gather(mesher.RequiredSamples(all)), all, grid, glm::vec3(0.0f));
        Require(whole.mesh.indices.size() / 3 == triangles, "A pit across four chunks meshes the same chunk by chunk as in one go");
        // The inside of the four chunks is closed and consistent: only the outer border of the 8 x 8 m patch is open.
        std::map<std::pair<Key, Key>, int> directed;
        for (const auto &triangle : soup.triangles)
            for (int corner = 0; corner < 3; ++corner)
                ++directed[{KeyOf(triangle[corner]), KeyOf(triangle[(corner + 1) % 3])}];
        for (const auto &[edge, count] : directed)
            Require(count == 1, "No edge is used twice in one direction across chunk seams");
        Require(volume.Sample(grid.SampleFloor({4.0f, 9.0f, 4.0f})).distance > 0.0f, "The pit is open");
    }
    // A terrain of 33 x 33 samples, 1 m apart, gently sloping, as the volume's base.
    std::shared_ptr<const TerrainSurface> SlopedSurface()
    {
        constexpr int samples = 33;
        std::vector<float> heights(samples * samples);
        for (int z = 0; z < samples; ++z)
            for (int x = 0; x < samples; ++x)
                heights[static_cast<std::size_t>(z) * samples + x] = 6.0f + 0.05f * x + 0.02f * z;
        return std::make_shared<const TerrainSurface>(samples, samples, 1.0f, std::move(heights));
    }

    void SurfaceSnapshot()
    {
        // Two cells with distinct corners: the surface follows the terrain's triangles, not a bilinear patch.
        const TerrainSurface surface(2, 2, 2.0f, {0.0f, 4.0f, 2.0f, 0.0f});
        Require(Near(*surface.Height(1.0f, 0.0f), 2.0f, 1e-5f) && Near(*surface.Height(0.0f, 1.0f), 1.0f, 1e-5f),
                "Heights interpolate along the cell's edges");
        // The centre lies on the (1, 0) - (0, 1) diagonal: the mean of those two corners, not of all four.
        Require(Near(*surface.Height(1.0f, 1.0f), 3.0f, 1e-5f), "Cells split along the terrain's diagonal");
        // (0.75, 0.75) across: h11 + (h01 - h11) * 0.25 + (h10 - h11) * 0.25 = 0 + 0.5 + 1.
        Require(Near(*surface.Height(1.5f, 1.5f), 1.5f, 1e-5f), "The far triangle uses the far corner");
        Require(surface.Height(2.0f, 2.0f) && !surface.Height(-0.1f, 0.0f) && !surface.Height(0.0f, 2.1f), "Only the terrain has a surface");
        Require(surface.CellHeightRange(0, 0) == glm::vec2(0.0f, 4.0f), "A cell's height range spans its corners");
        Require(!TerrainSurface(1, 5, 1.0f, {0, 0, 0, 0, 0}).Valid(), "A terrain needs at least one cell");

        const auto density = MakeTerrainDensity(std::make_shared<const TerrainSurface>(surface), TerrainGroundMaterials::Rock(4, 1.0f));
        const auto sample = density->Sample({1.0f, 5.0f, 1.0f});
        Require(Near(sample.distance, 2.0f, 1e-5f) && sample.material == 0, "The terrain density is height above its surface");
        Require(density->Sample({1.0f, 2.8f, 1.0f}).material == 0 && density->Sample({1.0f, 1.0f, 1.0f}).material == 4,
                "The surface's layer near the top, the rock layer from its depth down");

        // Strata: subsoil from 0.5 m, rock from 2 m, in any order; text round-trips and bad entries are skipped.
        const auto strata = TerrainGroundMaterials::Decode("1@2;5@0.5;bad;7@x");
        Require(strata.strata.size() == 2 && strata.strata[0] == TerrainStratum{5, 0.5f} && strata.strata[1] == TerrainStratum{1, 2.0f},
                "Strata decode, sorted by depth");
        Require(TerrainGroundMaterials::Decode(strata.Encode()) == strata, "Strata round-trip");
        Require(strata.MaterialAt(0, 0.2f) == 0 && strata.MaterialAt(0, 0.5f) == 5 && strata.MaterialAt(0, 1.5f) == 5 &&
                    strata.MaterialAt(0, 3.0f) == 1 && TerrainGroundMaterials{}.MaterialAt(3, 9.0f) == 3,
                "The surface's layer, then subsoil, then rock; without strata the surface's all the way down");
        const auto layered = MakeTerrainDensity(std::make_shared<const TerrainSurface>(surface), strata);
        Require(layered->Sample({1.0f, 2.0f, 1.0f}).material == 5 && layered->Sample({1.0f, -1.0f, 1.0f}).material == 1,
                "The terrain density follows the strata");

        // Painted layers: the ground takes the nearest sample's.
        const TerrainSurface painted(2, 2, 1.0f, {0, 0, 0, 0}, {1, 2, 3, 5});
        Require(painted.Layer(0.1f, 0.1f) == 1 && painted.Layer(0.9f, 0.2f) == 2 && painted.Layer(0.2f, 0.8f) == 3 &&
                    painted.Layer(5.0f, 5.0f) == 5 && TerrainSurface(2, 2, 1.0f, {0, 0, 0, 0}, {1}).Layer(0, 0) == 0,
                "Surface layers by nearest sample (none when the sizes differ)");
        const auto paintedDensity = MakeTerrainDensity(std::make_shared<const TerrainSurface>(painted));
        Require(paintedDensity->Sample({0.9f, -0.5f, 0.1f}).material == 2, "Without a rock layer the surface's layer goes all the way down");
    }

    void CouplingHoles()
    {
        const auto surface = SlopedSurface();
        const TerrainCouplingSettings settings;
        TerrainVolume volume(SmallGrid(), MakeTerrainDensity(surface, TerrainGroundMaterials::Rock(1, 0.0f)));
        Require(ComputeVolumeHoles(volume, *surface, settings).Empty(), "An unedited volume cuts nothing");

        // A cave well under the ground: stored chunks, but the surface is untouched.
        volume.Apply(CapsuleShape({4.0f, 2.0f, 4.0f}, {12.0f, 2.0f, 4.0f}, 1.0f), {VolumeEditMode::Subtract});
        Require(volume.GetStoredChunkCount() > 0 && ComputeVolumeHoles(volume, *surface, settings).Empty(),
                "A cave below the surface does not cut the terrain");

        // A pit through the surface cuts the cells it opens, and only those near it.
        const glm::vec3 pit(20.0f, *surface->Height(20.0f, 20.0f), 20.0f);
        volume.Apply(SphereShape(pit, 1.6f), {VolumeEditMode::Subtract});
        const auto holes = ComputeVolumeHoles(volume, *surface, settings);
        Require(holes.GetCellsX() == 32 && holes.GetCellsZ() == 32, "Holes cover the surface's cells");
        Require(holes.IsHole(19, 19) && holes.IsHole(20, 20) && holes.IsHole(19, 20), "The pit's cells are cut");
        Require(!holes.IsHole(24, 20) && !holes.IsHole(20, 15) && !holes.IsHole(8, 4), "Cells away from the pit are not");
        // So is a mound built on the ground.
        volume.Apply(SphereShape({8.0f, *surface->Height(8.0f, 26.0f), 26.0f}, 1.2f), {VolumeEditMode::Add, 2});
        Require(ComputeVolumeHoles(volume, *surface, settings).IsHole(8, 26), "A mound cuts the cells it covers");
        Require(ChangedCells(holes, ComputeVolumeHoles(volume, *surface, settings)).size() > 0 &&
                    ChangedCells(holes, holes).empty(),
                "Changed cells are where two masks differ");
    }

    // Every triangle of a fitted mesh, in the terrain's space, with how far its corners sit from the surface.
    void CouplingFit()
    {
        const auto surface = SlopedSurface();
        const TerrainCouplingSettings settings;
        const auto grid = SmallGrid();
        TerrainVolume volume(grid, MakeTerrainDensity(surface, TerrainGroundMaterials::Rock(1, 0.0f)));
        const SurfaceNetsMesher mesher;
        const VolumeChunkCoord ground = grid.ChunkAt({20.0f, *surface->Height(20.0f, 20.0f), 20.0f});

        // Unedited ground is the heightfield's to draw: a fitted chunk over it is empty.
        auto plain = MeshVolumeChunk(volume, ground, mesher);
        Require(!plain.Empty(), "The chunk holds the surface before fitting");
        FitVolumeMeshToTerrain(plain, grid.ChunkOrigin(ground), *surface, TerrainHoleMask(32, 32), settings, grid.voxelSize);
        Require(plain.Empty() && plain.mesh.vertices.empty() && plain.materials.empty(), "Fitting drops ground the heightfield draws");

        const glm::vec3 pit(20.0f, *surface->Height(20.0f, 20.0f), 20.0f);
        volume.Apply(SphereShape(pit, 1.6f), {VolumeEditMode::Subtract});
        const auto holes = ComputeVolumeHoles(volume, *surface, settings);
        auto mesh = MeshVolumeChunk(volume, ground, mesher);
        const auto unfitted = mesh.mesh.indices.size();
        FitVolumeMeshToTerrain(mesh, grid.ChunkOrigin(ground), *surface, holes, settings, grid.voxelSize);
        Require(!mesh.Empty() && mesh.mesh.indices.size() < unfitted && mesh.materials.size() == mesh.mesh.vertices.size(),
                "A pit's chunk keeps part of its mesh");

        bool ring = false, pitFloor = false;
        const auto origin = grid.ChunkOrigin(ground);
        for (const auto &vertex : mesh.mesh.vertices)
        {
            const glm::vec3 point = Position(vertex) + origin;
            const int cellX = static_cast<int>(std::floor(point.x)), cellZ = static_cast<int>(std::floor(point.z));
            const float height = *surface->Height(point.x, point.z);
            Require(std::abs(cellX - 20) <= 4 && std::abs(cellZ - 20) <= 4, "Nothing far from the pit survives fitting");
            if (holes.GetCut(cellX, cellZ) == TerrainHoleCut::None)
            {
                // Over uncut cells only the ring is left, at or below the terrain, never more than the sink below it.
                ring = true;
                Require(point.y <= height + settings.surfaceTolerance * grid.voxelSize, "The ring never rises above the terrain");
            }
            pitFloor = pitFloor || point.y < height - 1.0f;
        }
        Require(ring && pitFloor, "The fitted mesh has the pit and the overlap ring around it");
        // With no ring, nothing over uncut cells stays on the surface.
        auto noRing = MeshVolumeChunk(volume, ground, mesher);
        auto ringless = settings;
        ringless.overlapCells = 0;
        FitVolumeMeshToTerrain(noRing, grid.ChunkOrigin(ground), *surface, holes, ringless, grid.voxelSize);
        Require(noRing.mesh.indices.size() < mesh.mesh.indices.size(), "The ring adds triangles around the cut");

        // A cave under the ground keeps its whole interior though nothing is cut.
        TerrainVolume caves(grid, MakeTerrainDensity(surface, TerrainGroundMaterials::Rock(1, 0.0f)));
        caves.Apply(SphereShape({6.0f, 2.0f, 6.0f}, 1.5f), {VolumeEditMode::Subtract});
        const auto caveChunk = grid.ChunkAt({6.0f, 2.0f, 6.0f});
        auto cave = MeshVolumeChunk(caves, caveChunk, mesher);
        const auto caveTriangles = cave.mesh.indices.size();
        FitVolumeMeshToTerrain(cave, grid.ChunkOrigin(caveChunk), *surface, TerrainHoleMask(32, 32), settings, grid.voxelSize);
        Require(caveTriangles > 0 && cave.mesh.indices.size() == caveTriangles, "A cave's interior is never left to the heightfield");

        // The chunks along a cell's surface: the one holding the ground there, and its ring.
        const auto along = ChunksAlongSurface(grid, *surface, {{20, 20}}, settings);
        Require(std::find(along.begin(), along.end(), ground) != along.end(), "Chunks along the surface include the ground's");
        for (const auto &chunk : along)
            Require(std::abs(chunk.y - ground.y) <= 1, "Chunks along the surface stay near it");
    }

    void MeshQueue()
    {
        const auto grid = SmallGrid();
        TerrainVolume volume(grid, ConstantDensity::Air());
        volume.Apply(SphereShape({2.0f, 2.0f, 2.0f}, 1.0f), {VolumeEditMode::Add});
        VolumeMeshQueue queue(std::make_shared<SurfaceNetsMesher>());
        std::atomic<int> finished = 0;
        const auto count = [&finished](VolumeChunkCoord, VolumeMesh &) { ++finished; };
        queue.Enqueue(volume, {0, 0, 0}, count);
        // An edit before the first mesh is collected: only the newer mesh comes out.
        volume.Apply(SphereShape({2.0f, 2.0f, 2.0f}, 1.5f), {VolumeEditMode::Add});
        queue.Enqueue(volume, {0, 0, 0}, count);
        queue.Enqueue(volume, {5, 5, 5}, count);
        Require(queue.GetPendingCount() == 3, "Jobs are pending until collected");
        const auto results = queue.Collect(0, true);
        Require(queue.Idle() && finished == 3, "Waiting finishes every job, and each runs its finishing step");
        Require(results.size() == 2 && results[0].chunk == VolumeChunkCoord{0, 0, 0} && results[1].chunk == VolumeChunkCoord{5, 5, 5},
                "A superseded mesh is dropped");
        const auto fresh = MeshVolumeChunk(volume, {0, 0, 0}, queue.GetMesher());
        Require(results[0].mesh.mesh.indices == fresh.mesh.indices && results[1].mesh.Empty(), "The kept mesh is the newer one");

        queue.Enqueue(volume, {0, 0, 0});
        queue.Clear();
        Require(queue.Idle() && queue.Collect(10, true).empty(), "Clearing drops pending jobs");
    }

    void TransformedShapes()
    {
        // A terrain at (100, 0, 50), scaled 2x: a world-space sphere seen from the terrain's space.
        const glm::mat4 terrainToWorld = glm::scale(glm::translate(glm::mat4(1.0f), {100.0f, 0.0f, 50.0f}), glm::vec3(2.0f));
        const SphereShape worldSphere({110.0f, 4.0f, 60.0f}, 2.0f);
        const TransformedShape local(worldSphere, terrainToWorld);
        Require(Near(local.Distance({5.0f, 2.0f, 5.0f}), -1.0f, 1e-4f), "Distances shrink into the scaled space (2 m world = 1 m local)");
        Require(Near(local.Distance({5.0f, 4.0f, 5.0f}), 1.0f, 1e-4f), "Outside the sphere is positive");
        Require(glm::all(glm::lessThan(glm::abs(local.BoundsMin() - glm::vec3(4.0f, 1.0f, 4.0f)), glm::vec3(1e-4f))) &&
                    glm::all(glm::lessThan(glm::abs(local.BoundsMax() - glm::vec3(6.0f, 3.0f, 6.0f)), glm::vec3(1e-4f))),
                "Bounds come back into the local space");
    }
}

namespace
{
    // The layer a vertex weighs fully (VertexLayerEncoding).
    VolumeMaterial OwnLayer(const render::MeshVertexData &vertex)
    {
        const auto layers = VertexLayerEncoding::UnpackLayers(vertex.uv[0]);
        return vertex.uv[1] >= 1.0f ? layers[0] : vertex.uv2[0] >= 1.0f ? layers[1] : layers[2];
    }

    // Chunk meshes name each triangle's terrain layers in its vertices (the shader's vertex layers).
    void TriangleMaterials()
    {
        // Ground of layer 1 with a box of layer 3 pressed into it: the mesh has both materials and vertices where
        // they meet.
        TerrainVolume volume(SmallGrid(), FlatGround(2.0f, 1));
        volume.Apply(BoxShape({2.0f, 2.0f, 2.0f}, {1.0f, 1.0f, 1.0f}), {VolumeEditMode::Paint, 3});
        auto mesh = MeshVolumeChunk(volume, {0, 0, 0}, SurfaceNetsMesher());
        Require(!mesh.Empty(), "The painted ground has a surface");
        const auto triangles = mesh.mesh.indices.size();
        const auto vertices = mesh.mesh.vertices.size();
        bool mixed = false;
        for (std::size_t t = 0; t < triangles; t += 3)
        {
            const auto a = mesh.materials[mesh.mesh.indices[t]], b = mesh.materials[mesh.mesh.indices[t + 1]],
                       c = mesh.materials[mesh.mesh.indices[t + 2]];
            mixed = mixed || a != b || b != c;
        }
        Require(mixed, "Some triangles straddle the two materials before encoding");

        EncodeTriangleMaterials(mesh);
        Require(mesh.mesh.indices.size() == triangles, "Encoding keeps every triangle");
        Require(mesh.mesh.vertices.size() > vertices && mesh.materials.size() == mesh.mesh.vertices.size(),
                "Vertices shared across materials are copied, materials follow the vertices");
        bool sawBlend = false;
        for (std::size_t t = 0; t < triangles; t += 3)
        {
            const float packed = mesh.mesh.vertices[mesh.mesh.indices[t]].uv[0];
            for (int corner = 1; corner < 3; ++corner)
                Require(mesh.mesh.vertices[mesh.mesh.indices[t + corner]].uv[0] == packed, "A triangle's corners name the same layers");
            const auto layers = VertexLayerEncoding::UnpackLayers(packed);
            for (const auto layer : layers)
                Require(layer == 1 || layer == 3, "The layers are the rock's materials");
            sawBlend = sawBlend || layers[0] != layers[1];
        }
        Require(sawBlend, "Triangles where the materials meet blend them");
        for (std::size_t v = 0; v < mesh.mesh.vertices.size(); ++v)
            Require(OwnLayer(mesh.mesh.vertices[v]) == mesh.materials[v], "Each vertex weighs its own material fully");

        // Encoding twice changes nothing more.
        const auto encoded = mesh.mesh.vertices.size();
        EncodeTriangleMaterials(mesh);
        Require(mesh.mesh.vertices.size() == encoded, "An encoded mesh is left as it is");
    }
}

namespace
{
    // Phase 5: edits say how much rock they took (resources, debris), volumes clear, and archives hold many volumes.
    void RockAccounting()
    {
        TerrainVolume volume(SmallGrid(), FlatGround(2.0f, 1));
        // A sphere centred on the surface removes about half its volume: (2/3) pi r^3.
        const auto dig = volume.Apply(SphereShape({2.0f, 2.0f, 2.0f}, 1.5f), {VolumeEditMode::Subtract});
        const float expected = 2.0f / 3.0f * 3.14159265f * 1.5f * 1.5f * 1.5f;
        Require(dig.rockRemoved.size() == 1 && dig.rockRemoved.contains(1), "The removed rock is the ground's material");
        Require(Near(dig.RockRemoved(), expected, expected * 0.3f) && dig.rockAdded == 0.0f,
                "Digging removes about the half sphere below the ground, adds nothing");
        const auto again = volume.Apply(SphereShape({2.0f, 2.0f, 2.0f}, 1.5f), {VolumeEditMode::Subtract});
        Require(again.RockRemoved() == 0.0f, "Digging air removes nothing");
        const auto fill = volume.Apply(SphereShape({2.0f, 2.0f, 2.0f}, 1.5f), {VolumeEditMode::Add, 4});
        Require(Near(fill.rockAdded, 2.0f * expected, expected * 0.6f) && fill.RockRemoved() == 0.0f, "Filling adds the whole sphere");
        const auto paint = volume.Apply(SphereShape({2.0f, 2.0f, 2.0f}, 1.0f), {VolumeEditMode::Paint, 2});
        Require(paint.Changed() && paint.RockRemoved() == 0.0f && paint.rockAdded == 0.0f, "Painting moves no rock");

        VolumeEditResult total;
        total.Merge(dig);
        total.Merge(fill);
        Require(total.changedChunks.size() == dig.changedChunks.size() + fill.changedChunks.size() &&
                    Near(total.RockRemoved(), dig.RockRemoved(), 1e-4f) && Near(total.rockAdded, fill.rockAdded, 1e-4f),
                "Results merge");

        volume.TakeDirtyChunks();
        const auto revision = volume.GetRevision();
        volume.ClearChunks();
        Require(volume.GetStoredChunkCount() == 0 && volume.GetRevision() != revision && !volume.GetDirtyChunks().empty(),
                "Clearing drops every stored chunk and dirties their meshes");
    }

    // The base continues past the terrain's edges, so unedited chunks there have no wall at the edge, and fitting
    // leaves nothing of them (a neighbouring tile's volume owns the ground beyond).
    void TerrainEdges()
    {
        auto surface = std::make_shared<const TerrainSurface>(9, 9, 1.0f, std::vector<float>(81, 2.0f));
        Require(!surface->Height(-1.0f, 3.0f) && surface->ExtendedHeight(-1.0f, 3.0f) == 2.0f &&
                    surface->ExtendedHeight(20.0f, 20.0f) == 2.0f,
                "The extended height carries the edge outwards");
        TerrainVolume volume(SmallGrid(), MakeTerrainDensity(surface));
        Require(volume.Sample({-4, 4, 2}).distance == 0.0f && volume.Sample({-4, 6, 2}).distance > 0.0f,
                "Past the edge the base is the continued ground, not air");
        const TerrainCouplingSettings settings;
        for (const VolumeChunkCoord chunk : {VolumeChunkCoord{0, 0, 0}, VolumeChunkCoord{-1, 0, 0}, VolumeChunkCoord{0, 0, -1}})
        {
            auto mesh = MeshVolumeChunk(volume, chunk, SurfaceNetsMesher());
            FitVolumeMeshToTerrain(mesh, volume.GetGrid().ChunkOrigin(chunk), *surface, TerrainHoleMask(8, 8), settings,
                                   volume.GetGrid().voxelSize);
            Require(mesh.Empty(), "An unedited chunk at the terrain's edge draws nothing");
        }
        // Dug at the edge, the pit is drawn over the terrain only.
        volume.Apply(SphereShape({0.5f, 2.0f, 2.0f}, 1.5f), {VolumeEditMode::Subtract});
        auto holes = ComputeVolumeHoles(volume, *surface, settings);
        for (const VolumeChunkCoord chunk : {VolumeChunkCoord{0, 0, 0}, VolumeChunkCoord{-1, 0, 0}})
        {
            auto mesh = MeshVolumeChunk(volume, chunk, SurfaceNetsMesher());
            FitVolumeMeshToTerrain(mesh, volume.GetGrid().ChunkOrigin(chunk), *surface, holes, settings, volume.GetGrid().voxelSize);
            Require(chunk.x == 0 ? !mesh.Empty() : mesh.Empty(), "The pit's chunk on the terrain draws it; the one past the edge does not");
            for (const auto &vertex : mesh.mesh.vertices)
                Require(volume.GetGrid().ChunkOrigin(chunk).x + vertex.position[0] > -volume.GetGrid().voxelSize,
                        "Nothing is drawn more than a voxel past the edge");
        }
    }

    CaveNetworkSettings TestCaves()
    {
        CaveNetworkSettings caves;
        caves.enabled = true;
        caves.cellSize = 32.0f;
        caves.tunnelsPerCell = 1;
        caves.tunnelLength = 40.0f;
        caves.radius = {1.5f, 1.5f};
        caves.altitude = {-6.0f, 3.0f};
        caves.chamberChance = 0.0f;
        caves.wobble = 0.0f;
        return caves;
    }

    // Phase 6: generated caves are deterministic, agree across neighbouring volumes, stay in their band, and reach the
    // terrain's holes through their mouths.
    void GeneratedCaves()
    {
        const auto caves = TestCaves();
        const auto all = GenerateCaveNetwork(caves, 11, {0, -20, 0}, {64, 20, 64});
        Require(!all.empty(), "Caves are generated");
        const auto again = GenerateCaveNetwork(caves, 11, {0, -20, 0}, {64, 20, 64});
        Require(all.size() == again.size() && std::equal(all.begin(), all.end(), again.begin(), [](const CavePrimitive &a, const CavePrimitive &b) {
                    return a.a == b.a && a.b == b.b && a.radius == b.radius;
                }),
                "The same seed makes the same caves");
        Require(GenerateCaveNetwork(caves, 12, {0, -20, 0}, {64, 20, 64}).front().a != all.front().a, "Another seed makes others");
        auto off = caves;
        off.enabled = false;
        Require(GenerateCaveNetwork(off, 11, {0, -20, 0}, {64, 20, 64}).empty(), "Disabled caves make nothing");
        for (const auto &piece : all)
            Require(piece.a.y >= caves.altitude.x - 1e-3f && piece.a.y <= caves.altitude.y + 1e-3f && piece.b.y >= caves.altitude.x - 1e-3f &&
                        piece.b.y <= caves.altitude.y + 1e-3f,
                    "Tunnels stay inside their band of heights");

        // Two neighbouring volumes (a world's tiles at x 0 and 32) see the same caves where they meet.
        const CaveDensity west(caves, 11, {0, 0, 0}, {0, -20, 0}, {32, 20, 64});
        const CaveDensity east(caves, 11, {32, 0, 0}, {0, -20, 0}, {32, 20, 64});
        float worst = 0.0f;
        int inside = 0;
        for (float y = -6.0f; y <= 3.0f; y += 0.5f)
            for (float z = 0.0f; z <= 64.0f; z += 0.5f)
            {
                const float a = west.Sample({32.0f, y, z}).distance, b = east.Sample({0.0f, y, z}).distance;
                if (a < 2.0f || b < 2.0f)
                    worst = std::max(worst, std::abs(a - b));
                inside += a < 0.0f ? 1 : 0;
            }
        Require(worst < 1e-3f, "Neighbouring volumes agree on the caves at their shared edge");

        // Fill matches Sample; features cover the pieces.
        VolumeGrid grid = SmallGrid();
        const VolumeSampleBox box{{0, -12, 0}, {16, 24, 16}};
        std::vector<VolumeSample> filled(box.SampleCount());
        west.Fill(grid, box, filled);
        std::size_t index = 0, mismatches = 0;
        for (int z = 0; z < box.count.z; ++z)
            for (int y = 0; y < box.count.y; ++y)
                for (int x = 0; x < box.count.x; ++x, ++index)
                {
                    const float sampled = west.Sample(grid.SamplePosition(box.first + glm::ivec3(x, y, z))).distance;
                    if (sampled < 2.0f && std::abs(sampled - filled[index].distance) > 1e-4f)
                        ++mismatches;
                }
        Require(mismatches == 0, "Fill gives what Sample gives near the caves");
        std::vector<VolumeBounds> features;
        west.AppendFeatureBounds(features);
        Require(features.size() == west.GetPrimitives().size(), "Every piece is a feature");

        // Carved from flat ground at 2 m (inside the band): the volume has feature chunks and cave mouths, unstored.
        auto shallow = caves;
        shallow.altitude = {1.0f, 3.0f};
        auto surface = std::make_shared<const TerrainSurface>(33, 33, 1.0f, std::vector<float>(33 * 33, 2.0f));
        const auto carved = std::make_shared<CompositeDensity>(std::vector<CompositeDensity::Layer>{
            {MakeTerrainDensity(surface), CompositeDensity::Operation::Union},
            {std::make_shared<CaveDensity>(shallow, 11, glm::vec3(0.0f), glm::vec3(0, -20, 0), glm::vec3(32, 20, 32)),
             CompositeDensity::Operation::Subtract},
        });
        TerrainVolume volume(grid, carved);
        const auto featureChunks = volume.GetBaseFeatureChunks();
        Require(!featureChunks.empty() && volume.GetStoredChunkCount() == 0, "Generated caves give feature chunks without storing any");
        const auto mouths = ComputeVolumeHoles(volume, *surface, TerrainCouplingSettings{});
        Require(mouths.GetHoleCount() > 0, "Caves crossing the surface cut mouths");
        // Deep caves cut nothing.
        auto deep = caves;
        deep.altitude = {-14.0f, -8.0f};
        TerrainVolume deepVolume(grid, std::make_shared<CompositeDensity>(std::vector<CompositeDensity::Layer>{
                                           {MakeTerrainDensity(surface), CompositeDensity::Operation::Union},
                                           {std::make_shared<CaveDensity>(deep, 11, glm::vec3(0.0f), glm::vec3(0, -20, 0), glm::vec3(32, 20, 32)),
                                            CompositeDensity::Operation::Subtract},
                                       }));
        Require(!deepVolume.GetBaseFeatureChunks().empty() && ComputeVolumeHoles(deepVolume, *surface, TerrainCouplingSettings{}).Empty(),
                "Caves under the ground cut nothing");
        // A mouth's chunk meshes the cave.
        bool meshed = false;
        for (const auto chunk : featureChunks)
        {
            auto mesh = MeshVolumeChunk(volume, chunk, SurfaceNetsMesher());
            FitVolumeMeshToTerrain(mesh, grid.ChunkOrigin(chunk), *surface, mouths, TerrainCouplingSettings{}, grid.voxelSize);
            meshed = meshed || !mesh.Empty();
        }
        Require(meshed, "The caves are meshed from the base alone");
    }

    // Phase 7: stamps rely on shared transformed shapes and the Paint layer of composites.
    void StampPieces()
    {
        const glm::mat4 toShape = glm::translate(glm::mat4(1.0f), {-10.0f, 0.0f, 0.0f});
        const TransformedShape owning(std::make_shared<SphereShape>(glm::vec3(0.0f), 2.0f), toShape);
        Require(Near(owning.Distance({10.0f, 0.0f, 0.0f}), -2.0f, 1e-5f) && Near(owning.BoundsMin().x, 8.0f, 1e-4f), "An owning transformed shape");

        CompositeDensity painted({{FlatGround(2.0f, 1), CompositeDensity::Operation::Union}});
        painted.Add(std::make_shared<ShapeDensity>(std::make_shared<SphereShape>(glm::vec3(0.0f, 2.0f, 0.0f), 1.0f), 5),
                    CompositeDensity::Operation::Paint);
        const auto inside = painted.Sample({0.0f, 1.5f, 0.0f});
        Require(inside.material == 5 && Near(inside.distance, -0.5f, 1e-5f), "Paint changes the material, not the distance");
        Require(painted.Sample({3.0f, 1.5f, 0.0f}).material == 1, "Outside the shape the ground keeps its material");
        std::vector<VolumeBounds> features;
        painted.AppendFeatureBounds(features);
        Require(features.size() == 1 && features[0].min == glm::vec3(-1.0f, 1.0f, -1.0f), "A shape layer is a feature");
    }

    // Phase 8: dual contouring keeps a box's corners that surface nets round off, closes surfaces as surface nets do,
    // and both carry occlusion: open ground reads open, a crevice reads darker, depth under the terrain darker still.
    void DualContouring()
    {
        const auto grid = SmallGrid();
        TerrainVolume volume(grid, ConstantDensity::Air());
        const BoxShape box({0.2f, 0.15f, 0.1f}, {1.6f, 1.3f, 1.45f});
        volume.Apply(box, {VolumeEditMode::Add, 2});
        const auto worst = [&](const VolumeMesher &mesher) {
            float error = 0.0f;
            Soup soup;
            for (int z = -1; z <= 0; ++z)
                for (int y = -1; y <= 0; ++y)
                    for (int x = -1; x <= 0; ++x)
                    {
                        const auto mesh = MeshVolumeChunk(volume, {x, y, z}, mesher);
                        for (const auto &vertex : mesh.mesh.vertices)
                            error = std::max(error, std::abs(box.Distance(Position(vertex) + grid.ChunkOrigin({x, y, z}))));
                        soup.Add(mesh, grid.ChunkOrigin({x, y, z}));
                    }
            RequireClosedAndConsistent(soup, "The box meshed chunk by chunk");
            return error;
        };
        const float nets = worst(SurfaceNetsMesher()), contoured = worst(DualContouringMesher());
        Require(contoured < nets * 0.6f, "Dual contouring keeps the corners: " + std::to_string(contoured) + " vs " +
                                                                  std::to_string(nets));
        Require(MakeVolumeMesher(VolumeMesherKind::DualContouring)->Apron() == 2 &&
                    dynamic_cast<const DualContouringMesher *>(MakeVolumeMesher(VolumeMesherKind::DualContouring).get()),
                "Meshers by kind");

        // Occlusion: flat ground is open; the floor of a narrow slot is not.
        TerrainVolume ground(grid, FlatGround(2.0f));
        ground.Apply(BoxShape({2.0f, 1.25f, 2.0f}, {0.3f, 0.75f, 2.0f}), {VolumeEditMode::Subtract});
        const auto mesh = MeshVolumeChunk(ground, {0, 0, 0}, SurfaceNetsMesher());
        Require(mesh.occlusion.size() == mesh.mesh.vertices.size(), "Every vertex has an occlusion");
        float open = 0.0f, slot = 1.0f;
        for (std::size_t index = 0; index < mesh.mesh.vertices.size(); ++index)
        {
            const auto position = Position(mesh.mesh.vertices[index]);
            if (position.y > 1.9f && std::abs(position.x - 2.0f) > 1.5f)
                open = std::max(open, mesh.occlusion[index]);
            if (position.y < 1.0f && std::abs(position.x - 2.0f) < 0.5f)
                slot = std::min(slot, mesh.occlusion[index]);
        }
        Require(open > 0.95f && slot < 0.8f, "Open ground reads open, a slot's floor darker");

        auto buried = mesh;
        auto surface = std::make_shared<const TerrainSurface>(9, 9, 1.0f, std::vector<float>(81, 6.0f)); // 4 m above the ground
        ApplyBurialOcclusion(buried, glm::vec3(0.0f), *surface, {.burialDepth = 4.0f, .buriedOpenness = 0.1f});
        float before = 0.0f, after = 0.0f;
        for (std::size_t index = 0; index < mesh.occlusion.size(); ++index)
            before += mesh.occlusion[index], after += buried.occlusion[index];
        Require(after < before * 0.5f, "Ground deep under the surface darkens");
        EncodeTriangleMaterials(buried);
        Require(buried.mesh.vertices.front().uv2[1] == buried.occlusion.front(), "Occlusion reaches uv2.y");
    }

    // Debris: the rock a dig would remove is one closed mesh; air removes nothing.
    void RemovedRock()
    {
        const auto grid = SmallGrid();
        TerrainVolume volume(grid, FlatGround(2.0f, 4));
        const SphereShape dig({1.2f, 2.0f, 1.3f}, 1.5f);
        const auto removed = MeshRemovedRock(volume, dig, SurfaceNetsMesher(), {1.2f, 2.0f, 1.3f});
        Require(!removed.Empty() && removed.materials.size() == removed.mesh.vertices.size(), "A dig into the ground removes rock");
        for (const auto material : removed.materials)
            Require(material == 4, "The removed rock keeps its material");
        Soup soup;
        soup.Add(removed, {1.2f, 2.0f, 1.3f});
        RequireClosedAndConsistent(soup, "The removed rock");
        Require(MeshRemovedRock(volume, SphereShape({1.0f, 8.0f, 1.0f}, 1.0f), SurfaceNetsMesher(), {}).Empty(), "Air removes nothing");
        Require(volume.GetStoredChunkCount() == 0, "Measuring the removed rock edits nothing");
    }

    // Phase 9: a far proxy cuts the coarse surface where the volume moved the ground and draws coarse ground there.
    void FarProxies()
    {
        VolumeGrid grid;
        grid.voxelSize = 0.5f;
        grid.chunkVoxels = 16;
        auto fineSurface = std::make_shared<const TerrainSurface>(33, 33, 1.0f, std::vector<float>(33 * 33, 2.0f));
        auto farSurface = std::make_shared<const TerrainSurface>(5, 5, 8.0f, std::vector<float>(25, 2.0f));
        TerrainVolume volume(grid, MakeTerrainDensity(fineSurface));
        VolumeFarProxySettings settings;
        settings.step = 4;
        const auto plain = BuildVolumeFarProxy(volume, MakeTerrainDensity(farSurface), *farSurface, SurfaceNetsMesher(), settings);
        Require(plain.Empty(), "Untouched ground needs no far proxy");

        volume.Apply(SphereShape({20.0f, 2.0f, 12.0f}, 3.0f), {VolumeEditMode::Subtract});
        const auto proxy = BuildVolumeFarProxy(volume, MakeTerrainDensity(farSurface), *farSurface, SurfaceNetsMesher(), settings);
        Require(proxy.holes.GetCellsX() == 4 && proxy.holes.IsHole(2, 1), "The pit's coarse cell is cut");
        Require(proxy.holes.GetHoleCount() <= 4, "Only the cells around the pit");
        Require(!proxy.pieces.empty(), "Coarse ground fills the cut cell");
        float lowest = 1.0e9f;
        for (const auto &piece : proxy.pieces)
            for (const auto &vertex : piece.mesh.mesh.vertices)
                lowest = std::min(lowest, piece.origin.y + vertex.position[1]);
        Require(lowest < 0.5f, "The coarse pit goes down");
    }

    void EditArchives()
    {
        VolumeEditArchive::Entries entries;
        entries["0,0"] = {1, 2, 3};
        entries["-1,4"] = {};
        entries["world:A/B[1]"] = std::vector<std::uint8_t>(300, 7);
        const auto bytes = VolumeEditArchive::Encode(entries);
        std::string error;
        const auto decoded = VolumeEditArchive::Decode(bytes, &error);
        Require(decoded && *decoded == entries, "An archive round-trips: " + error);
        Require(VolumeEditArchive::Decode(VolumeEditArchive::Encode({}))->empty(), "An empty archive round-trips");

        auto cut = bytes;
        cut.pop_back();
        Require(!VolumeEditArchive::Decode(cut, &error) && !error.empty(), "A cut archive is refused");
        auto trailing = bytes;
        trailing.push_back(0);
        Require(!VolumeEditArchive::Decode(trailing), "Trailing data is refused");
        auto newer = bytes;
        newer[4] = 9;
        Require(!VolumeEditArchive::Decode(newer, &error) && error.find("newer") != std::string::npos, "A newer version is refused");
        auto huge = bytes;
        huge[8] = huge[9] = huge[10] = 0xff;
        Require(!VolumeEditArchive::Decode(huge), "A damaged count is refused");
        const std::vector<std::uint8_t> other{'P', 'L', 'V', 'O', 1, 0, 0, 0};
        Require(!VolumeEditArchive::Decode(other), "A volume file is not an archive");
    }
}

int main()
{
    try
    {
        GridAndQuantization();
        Sources();
        Edits();
        Files();
        MeshSphere();
        MeshGround();
        SurfaceSnapshot();
        CouplingHoles();
        CouplingFit();
        MeshQueue();
        TransformedShapes();
        TriangleMaterials();
        RockAccounting();
        EditArchives();
        TerrainEdges();
        GeneratedCaves();
        StampPieces();
        DualContouring();
        RemovedRock();
        FarProxies();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Terrain volume tests passed.\n";
    return 0;
}
