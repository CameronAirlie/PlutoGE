// Mesh fracture as pure geometry: Voronoi pieces of a cube and a UV sphere are closed, finite, deterministic, add up
// to the source's volume and keep its surface attributes; an impact makes pieces near it smaller; degenerate input
// gives nothing. Also times a 5k-triangle fracture (see docs/FRACTURE.md).
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/procedural/MeshFracture.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    render::MeshData CubeData()
    {
        std::unique_ptr<render::Mesh> cube(render::Mesh::Cube());
        return cube->GetMeshData();
    }

    render::MeshData SphereData(unsigned latitude, unsigned longitude)
    {
        std::unique_ptr<render::Mesh> sphere(render::Mesh::Sphere(latitude, longitude));
        return sphere->GetMeshData();
    }

    glm::vec3 Position(const render::MeshVertexData &vertex)
    {
        return {vertex.position[0], vertex.position[1], vertex.position[2]};
    }

    // The volume a closed triangle mesh encloses (divergence theorem); open meshes give nonsense.
    double ClosedVolume(const render::MeshData &mesh)
    {
        double volume = 0.0;
        for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
        {
            const glm::dvec3 a(Position(mesh.vertices[mesh.indices[index]]));
            const glm::dvec3 b(Position(mesh.vertices[mesh.indices[index + 1]]));
            const glm::dvec3 c(Position(mesh.vertices[mesh.indices[index + 2]]));
            volume += glm::dot(a, glm::cross(b, c)) / 6.0;
        }
        return volume;
    }

    bool Finite(const render::MeshVertexData &vertex)
    {
        for (const float value : vertex.position) if (!std::isfinite(value)) return false;
        for (const float value : vertex.normal) if (!std::isfinite(value)) return false;
        for (const float value : vertex.uv) if (!std::isfinite(value)) return false;
        for (const float value : vertex.tangent) if (!std::isfinite(value)) return false;
        return true;
    }

    void CheckPieces(const std::vector<FracturePiece> &pieces, double sourceVolume, const std::string &what)
    {
        double hullVolume = 0.0;
        double meshVolume = 0.0;
        for (const auto &piece : pieces)
        {
            Require(!piece.mesh.indices.empty() && piece.mesh.indices.size() % 3 == 0, what + ": every piece has triangles");
            Require(render::Mesh::IsValidTriangleList(piece.mesh), what + ": indices address the piece's vertices");
            Require(render::Mesh::AreValidSubmeshes(piece.submeshes, piece.mesh.indices.size()), what + ": submeshes fit");
            std::size_t covered = 0;
            for (const auto &submesh : piece.submeshes)
                covered += submesh.indexCount;
            Require(covered == piece.mesh.indices.size(), what + ": submeshes cover every index");
            Require(std::all_of(piece.mesh.vertices.begin(), piece.mesh.vertices.end(), Finite), what + ": all attributes are finite");
            Require(piece.volume > 0.0f && std::isfinite(piece.volume), what + ": positive volume");
            Require(std::isfinite(piece.centroid.x + piece.centroid.y + piece.centroid.z), what + ": finite centroid");
            // Re-centred: the centre of mass of a piece of a convex source is inside its bounds.
            Require(glm::all(glm::lessThanEqual(piece.boundsMin, glm::vec3(1e-4f))) &&
                        glm::all(glm::greaterThanEqual(piece.boundsMax, glm::vec3(-1e-4f))),
                    what + ": positions are relative to the centroid");
            hullVolume += piece.volume;
            const double closed = ClosedVolume(piece.mesh);
            Require(std::abs(closed - piece.volume) <= 0.03 * piece.volume + 1e-6, what + ": caps close each piece (" +
                                                                                       std::to_string(closed) + " vs " + std::to_string(piece.volume) + ")");
            meshVolume += closed;
        }
        Require(std::abs(hullVolume - sourceVolume) <= 0.02 * sourceVolume,
                what + ": piece volumes add up to the source (" + std::to_string(hullVolume) + " vs " + std::to_string(sourceVolume) + ")");
        Require(std::abs(meshVolume - sourceVolume) <= 0.03 * sourceVolume, what + ": closed piece meshes add up to the source");
    }

    bool SamePieces(const std::vector<FracturePiece> &a, const std::vector<FracturePiece> &b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t piece = 0; piece < a.size(); ++piece)
        {
            if (a[piece].mesh.indices != b[piece].mesh.indices || a[piece].mesh.vertices.size() != b[piece].mesh.vertices.size() ||
                a[piece].centroid != b[piece].centroid)
                return false;
            for (std::size_t vertex = 0; vertex < a[piece].mesh.vertices.size(); ++vertex)
                if (a[piece].mesh.vertices[vertex].position != b[piece].mesh.vertices[vertex].position)
                    return false;
        }
        return true;
    }

    void CubeSplitsIntoClosedPieces()
    {
        const auto cube = CubeData();
        const auto pieces = FractureMesh(cube, {}, {.pieceCount = 8, .seed = 1});
        Require(pieces.size() == 8, "Eight cells of a cube give eight pieces, got " + std::to_string(pieces.size()));
        CheckPieces(pieces, 1.0, "Cube");
        for (const auto &piece : pieces)
        {
            const glm::vec3 lowest = piece.centroid + piece.boundsMin;
            const glm::vec3 highest = piece.centroid + piece.boundsMax;
            Require(glm::all(glm::greaterThanEqual(lowest, glm::vec3(-0.5001f))) && glm::all(glm::lessThanEqual(highest, glm::vec3(0.5001f))),
                    "Pieces stay inside the cube");
        }
    }

    void SphereSplitsIntoClosedPieces()
    {
        const auto sphere = SphereData(24, 32);
        const double volume = ClosedVolume(sphere);
        Require(volume > 0.45 && volume < 0.53, "The test sphere is closed");
        const auto pieces = FractureMesh(sphere, {}, {.pieceCount = 12, .seed = 7});
        Require(pieces.size() >= 10 && pieces.size() <= 12, "A sphere gives about the cells asked for");
        CheckPieces(pieces, volume, "Sphere");
    }

    void DeterministicPerSeed()
    {
        const auto sphere = SphereData(16, 20);
        const auto first = FractureMesh(sphere, {}, {.pieceCount = 9, .seed = 42});
        const auto second = FractureMesh(sphere, {}, {.pieceCount = 9, .seed = 42});
        const auto other = FractureMesh(sphere, {}, {.pieceCount = 9, .seed = 43});
        Require(SamePieces(first, second), "The same seed gives the same pieces");
        Require(!SamePieces(first, other), "Another seed gives other pieces");
    }

    void SurfaceKeepsAttributesAndCapsTakeSlotZero()
    {
        // Every source triangle in slot 1: what lands in slot 0 is cap.
        const auto cube = CubeData();
        const std::vector<render::Submesh> submeshes{{.indexOffset = 0, .indexCount = static_cast<std::uint32_t>(cube.indices.size()), .materialIndex = 1}};
        const auto pieces = FractureMesh(cube, submeshes, {.pieceCount = 6, .seed = 3});
        Require(!pieces.empty(), "The cube breaks");
        double surfaceArea = 0.0;
        for (const auto &piece : pieces)
        {
            Require(piece.submeshes.size() == 2 && piece.submeshes[0].materialIndex == 0 && piece.submeshes[1].materialIndex == 1,
                    "Each piece has caps (slot 0) and surface (slot 1), in slot order");
            const auto &surface = piece.submeshes[1];
            for (std::uint32_t index = surface.indexOffset; index < surface.indexOffset + surface.indexCount; ++index)
            {
                const auto &vertex = piece.mesh.vertices[piece.mesh.indices[index]];
                Require(vertex.uv[0] >= -1e-4f && vertex.uv[0] <= 1.0001f && vertex.uv[1] >= -1e-4f && vertex.uv[1] <= 1.0001f,
                        "Surface UVs are interpolated within the source's range");
                const glm::vec3 normal(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
                Require(std::abs(glm::length(normal) - 1.0f) < 1e-3f, "Surface normals stay unit length");
                // The cube's faces are axis aligned: the normal is the one of the face the vertex lies on.
                const glm::vec3 world = Position(vertex) + piece.centroid;
                const float onFace = glm::dot(world, normal);
                Require(std::abs(onFace - 0.5f) < 1e-3f, "Surface vertices keep their face's normal");
            }
            for (std::uint32_t index = surface.indexOffset; index + 2 < surface.indexOffset + surface.indexCount; index += 3)
            {
                const glm::vec3 a = Position(piece.mesh.vertices[piece.mesh.indices[index]]);
                const glm::vec3 b = Position(piece.mesh.vertices[piece.mesh.indices[index + 1]]);
                const glm::vec3 c = Position(piece.mesh.vertices[piece.mesh.indices[index + 2]]);
                surfaceArea += glm::length(glm::cross(b - a, c - a)) * 0.5;
            }
            const auto &caps = piece.submeshes[0];
            for (std::uint32_t index = caps.indexOffset; index + 2 < caps.indexOffset + caps.indexCount; index += 3)
            {
                const auto &a = piece.mesh.vertices[piece.mesh.indices[index]];
                const auto &b = piece.mesh.vertices[piece.mesh.indices[index + 1]];
                const auto &c = piece.mesh.vertices[piece.mesh.indices[index + 2]];
                const glm::vec3 faceNormal = glm::normalize(glm::cross(Position(b) - Position(a), Position(c) - Position(a)));
                Require(glm::dot(faceNormal, glm::vec3(a.normal[0], a.normal[1], a.normal[2])) > 0.999f,
                        "Caps are flat, wound to face their normal");
            }
        }
        Require(std::abs(surfaceArea - 6.0) < 1e-3, "The pieces' surfaces add up to the cube's, without gaps or overlaps");
    }

    void ImpactMakesNearbyPiecesSmaller()
    {
        const auto cube = CubeData();
        const glm::vec3 impact(0.5f, 0.5f, 0.5f);
        double nearVolume = 0.0, farVolume = 0.0;
        int nearCount = 0, farCount = 0;
        for (std::uint32_t seed = 0; seed < 8; ++seed)
        {
            auto pieces = FractureMesh(cube, {}, {.pieceCount = 16, .seed = seed, .impactPoint = impact, .impactBias = 0.8f});
            std::sort(pieces.begin(), pieces.end(), [&](const auto &a, const auto &b)
                      { return glm::distance(a.centroid, impact) < glm::distance(b.centroid, impact); });
            for (std::size_t index = 0; index < pieces.size(); ++index)
            {
                (index < pieces.size() / 2 ? nearVolume : farVolume) += pieces[index].volume;
                ++(index < pieces.size() / 2 ? nearCount : farCount);
            }
        }
        const double nearMean = nearVolume / nearCount;
        const double farMean = farVolume / farCount;
        std::cout << "  impact bias 0.8: mean volume near " << nearMean << ", far " << farMean << "\n";
        Require(nearMean < farMean * 0.5, "Pieces near the impact are smaller on average");

        // An impact outside the mesh is moved onto it rather than breaking the seeds.
        const auto outside = FractureMesh(cube, {}, {.pieceCount = 8, .seed = 1, .impactPoint = glm::vec3(5.0f, 0.0f, 0.0f), .impactBias = 1.0f});
        CheckPieces(outside, 1.0, "Cube hit from outside");
    }

    void OnePieceIsTheWholeMesh()
    {
        const auto cube = CubeData();
        const auto pieces = FractureMesh(cube, {}, {.pieceCount = 1});
        Require(pieces.size() == 1 && pieces[0].mesh.indices.size() == cube.indices.size(), "One piece is the whole mesh, without caps");
        Require(std::abs(pieces[0].volume - 1.0f) < 1e-4f && glm::length(pieces[0].centroid) < 1e-4f, "Its volume and centroid are the cube's");
    }

    void SmallPiecesAreDropped()
    {
        const auto cube = CubeData();
        Require(FractureMesh(cube, {}, {.pieceCount = 8, .seed = 1, .minPieceSize = 2.0f}).empty(), "Pieces under the minimum size are dropped");
    }

    void DegenerateInputGivesNothing()
    {
        Require(FractureMesh({}, {}, {}).empty(), "An empty mesh gives no pieces");
        std::unique_ptr<render::Mesh> quad(render::Mesh::Quad());
        Require(FractureMesh(quad->GetMeshData(), {}, {}).empty(), "A flat mesh gives no pieces");
        auto broken = CubeData();
        broken.vertices[0].position = {std::nanf(""), 0.0f, 0.0f};
        broken.vertices[5].position = {INFINITY, 0.0f, 0.0f};
        broken.indices.push_back(1000); // Out of range: the triangle is skipped.
        broken.indices.push_back(1);
        broken.indices.push_back(2);
        const auto pieces = FractureMesh(broken, {}, {.pieceCount = 5, .seed = 2});
        for (const auto &piece : pieces)
            Require(std::all_of(piece.mesh.vertices.begin(), piece.mesh.vertices.end(), Finite), "Non-finite input never reaches a piece");
        const std::vector<render::Submesh> outOfRange{{.indexOffset = 30, .indexCount = 300}};
        (void)FractureMesh(CubeData(), outOfRange, {.pieceCount = 4}); // Must not crash.
        render::MeshData coincident;
        for (int index = 0; index < 8; ++index)
            coincident.vertices.push_back({.position = {1.0f, 1.0f, 1.0f}});
        coincident.indices = {0, 1, 2, 3, 4, 5};
        Require(FractureMesh(coincident, {}, {}).empty(), "Coincident points give no pieces");
    }

    // The fastest of several runs: other work on the machine only ever adds time.
    template <class Work>
    double FastestMilliseconds(int runs, const Work &work)
    {
        double fastest = std::numeric_limits<double>::max();
        for (int run = 0; run < runs; ++run)
        {
            const auto start = std::chrono::steady_clock::now();
            work(run);
            fastest = std::min(fastest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        return fastest;
    }

    void FracturesQuickly()
    {
        const auto rock = SphereData(50, 51); // 5,100 triangles, every vertex on the hull
        const auto triangles = rock.indices.size() / 3;
        std::vector<FracturePiece> pieces;
        const double milliseconds = FastestMilliseconds(20, [&](int run)
                                                        { pieces = FractureMesh(rock, {}, {.pieceCount = 10, .seed = static_cast<std::uint32_t>(run)}); });
        std::size_t output = 0;
        for (const auto &piece : pieces)
            output += piece.mesh.indices.size() / 3;
        std::cout << "  " << triangles << " triangles into " << pieces.size() << " pieces (" << output << " triangles): "
                  << milliseconds << " ms\n";
        CheckPieces(pieces, ClosedVolume(rock), "5k-triangle sphere");

        // A single piece costs about what building the convex hull does, which every fracture pays once.
        const double whole = FastestMilliseconds(20, [&](int) { pieces = FractureMesh(rock, {}, {.pieceCount = 1}); });
        std::cout << "  the same mesh as one piece: " << whole << " ms\n";
        const auto smallRock = SphereData(20, 25); // 1,000 triangles
        const double small = FastestMilliseconds(20, [&](int run)
                                                 { pieces = FractureMesh(smallRock, {}, {.pieceCount = 10, .seed = static_cast<std::uint32_t>(run)}); });
        std::cout << "  " << smallRock.indices.size() / 3 << " triangles into " << pieces.size() << " pieces: " << small << " ms\n";
#ifdef NDEBUG
        Require(milliseconds < 30.0, "A 5k-triangle mesh fractures into 10 pieces in well under 30 ms");
#endif
    }
}

int main()
{
    try
    {
        CubeSplitsIntoClosedPieces();
        SphereSplitsIntoClosedPieces();
        DeterministicPerSeed();
        SurfaceKeepsAttributesAndCapsTakeSlotZero();
        ImpactMakesNearbyPiecesSmaller();
        OnePieceIsTheWholeMesh();
        SmallPiecesAreDropped();
        DegenerateInputGivesNothing();
        FracturesQuickly();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "PlutoGEMeshFractureTests passed\n";
    return 0;
}
