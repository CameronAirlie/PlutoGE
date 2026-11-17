#include "PlutoGE/scene/procedural/MeshFracture.h"

#include <LinearMath/btConvexHullComputer.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <unordered_map>

namespace PlutoGE::scene
{
    namespace
    {
        using Vertex = render::MeshVertexData;

        // splitmix64. The standard library's distributions differ between implementations, and the same seed must
        // give the same pieces everywhere (a server and its clients shatter the same rock).
        class Random
        {
        public:
            explicit Random(std::uint32_t seed) : m_state(0x632BE59BD9B4E019ull ^ (static_cast<std::uint64_t>(seed) * 0x9E3779B97F4A7C15ull)) {}

            double Unit()
            {
                std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                z ^= z >> 31;
                return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
            }

        private:
            std::uint64_t m_state;
        };

        struct Plane
        {
            glm::dvec3 normal{0.0};
            double offset = 0.0;

            [[nodiscard]] double Distance(const glm::dvec3 &point) const { return glm::dot(normal, point) - offset; }
        };

        // A face of a convex polytope: a convex polygon wound counter-clockwise seen from outside (along the plane's
        // normal). `neighbour` is the seed whose bisector made it, or -1 for a face of the source's hull.
        struct PolytopeFace
        {
            std::vector<glm::dvec3> points;
            Plane plane;
            int neighbour = -1;
        };
        using Polytope = std::vector<PolytopeFace>;

        glm::dvec3 Position(const Vertex &vertex)
        {
            return {vertex.position[0], vertex.position[1], vertex.position[2]};
        }

        bool IsFinite(const glm::dvec3 &value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        // Any unit vector perpendicular to `normal`.
        glm::dvec3 Perpendicular(const glm::dvec3 &normal)
        {
            const glm::dvec3 axis = std::abs(normal.y) < 0.9 ? glm::dvec3(0.0, 1.0, 0.0) : glm::dvec3(1.0, 0.0, 0.0);
            return glm::normalize(glm::cross(axis, normal));
        }

        // The source's convex hull: its corners, its faces as corner indices (counter-clockwise seen from outside) and
        // each edge once. Cells clip it against a few walls each, so distances are computed per corner, not per face.
        struct Hull
        {
            std::vector<glm::dvec3> vertices;
            std::vector<std::vector<int>> faces;
            std::vector<Plane> planes;
            std::vector<std::pair<int, int>> edges;
        };

        Hull BuildHull(const std::vector<glm::dvec3> &points)
        {
            btConvexHullComputer computer;
            computer.compute(&points.front().x, static_cast<int>(sizeof(glm::dvec3)), static_cast<int>(points.size()), 0.0f, 0.0f);
            if (computer.faces.size() < 4 || computer.vertices.size() < 4)
                return {};

            Hull hull;
            glm::dvec3 centre(0.0);
            for (int index = 0; index < computer.vertices.size(); ++index)
            {
                const auto &vertex = computer.vertices[index];
                hull.vertices.emplace_back(vertex.x(), vertex.y(), vertex.z());
                centre += hull.vertices.back();
            }
            centre /= static_cast<double>(hull.vertices.size());
            for (int index = 0; index < computer.edges.size(); ++index)
            {
                const int source = computer.edges[index].getSourceVertex();
                const int target = computer.edges[index].getTargetVertex();
                if (source < target)
                    hull.edges.emplace_back(source, target);
            }

            for (int faceIndex = 0; faceIndex < computer.faces.size(); ++faceIndex)
            {
                std::vector<int> corners;
                const auto *first = &computer.edges[computer.faces[faceIndex]];
                const auto *edge = first;
                do
                {
                    corners.push_back(edge->getSourceVertex());
                    edge = edge->getNextEdgeOfFace();
                } while (edge != first && corners.size() <= hull.vertices.size());
                if (corners.size() < 3)
                    continue;

                // Newell's normal is robust for slightly non-planar polygons; orientation is checked against the
                // hull's centre rather than trusted from the edge order.
                glm::dvec3 normal(0.0);
                glm::dvec3 mean(0.0);
                for (std::size_t index = 0; index < corners.size(); ++index)
                {
                    normal += glm::cross(hull.vertices[corners[index]], hull.vertices[corners[(index + 1) % corners.size()]]);
                    mean += hull.vertices[corners[index]];
                }
                mean /= static_cast<double>(corners.size());
                const double length = glm::length(normal);
                if (!(length > 0.0))
                    continue;
                normal /= length;
                if (glm::dot(normal, mean - centre) < 0.0)
                {
                    std::reverse(corners.begin(), corners.end());
                    normal = -normal;
                }
                hull.planes.push_back({normal, glm::dot(normal, mean)});
                hull.faces.push_back(std::move(corners));
            }
            return hull.faces.size() >= 4 ? hull : Hull{};
        }

        // Adds a convex polygon's fan of tetrahedra, apex at `reference`, to a solid's volume and first moment.
        template <class PointAt>
        void AddFan(std::size_t count, const PointAt &pointAt, const glm::dvec3 &reference, double &volume, glm::dvec3 &moment)
        {
            for (std::size_t index = 1; index + 1 < count; ++index)
            {
                const glm::dvec3 &a = pointAt(0);
                const glm::dvec3 &b = pointAt(index);
                const glm::dvec3 &c = pointAt(index + 1);
                const double tetrahedron = glm::dot(a - reference, glm::cross(b - reference, c - reference)) / 6.0;
                volume += tetrahedron;
                moment += tetrahedron * (reference + a + b + c) * 0.25;
            }
        }

        // Volume and centre of mass of a closed polytope.
        double PolytopeVolume(const Polytope &polytope, glm::dvec3 &centroid)
        {
            centroid = glm::dvec3(0.0);
            if (polytope.empty())
                return 0.0;
            const glm::dvec3 reference = polytope.front().points.front();
            double volume = 0.0;
            glm::dvec3 moment(0.0);
            for (const auto &face : polytope)
                AddFan(face.points.size(), [&](std::size_t index) -> const glm::dvec3 & { return face.points[index]; }, reference, volume, moment);
            centroid = volume > 0.0 ? moment / volume : reference;
            return volume;
        }

        double HullVolume(const Hull &hull, glm::dvec3 &centroid)
        {
            centroid = glm::dvec3(0.0);
            if (hull.faces.empty())
                return 0.0;
            const glm::dvec3 reference = hull.vertices.front();
            double volume = 0.0;
            glm::dvec3 moment(0.0);
            for (const auto &corners : hull.faces)
                AddFan(corners.size(), [&](std::size_t index) -> const glm::dvec3 & { return hull.vertices[corners[index]]; }, reference, volume, moment);
            centroid = volume > 0.0 ? moment / volume : reference;
            return volume;
        }

        double FarthestPoint(const Polytope &polytope, const glm::dvec3 &from)
        {
            double farthest = 0.0;
            for (const auto &face : polytope)
                for (const auto &point : face.points)
                    farthest = std::max(farthest, glm::distance(point, from));
            return farthest;
        }

        void RemoveRepeatedPoints(std::vector<glm::dvec3> &points, double epsilon)
        {
            const double limit = epsilon * epsilon;
            std::vector<glm::dvec3> unique;
            unique.reserve(points.size());
            for (const auto &point : points)
                if (unique.empty() || glm::dot(point - unique.back(), point - unique.back()) > limit)
                    unique.push_back(point);
            while (unique.size() > 1 && glm::dot(unique.front() - unique.back(), unique.front() - unique.back()) <= limit)
                unique.pop_back();
            points = std::move(unique);
        }

        // Orders points on a plane counter-clockwise seen from along its normal (they must be in convex position).
        void SortAroundNormal(std::vector<glm::dvec3> &points, const glm::dvec3 &normal)
        {
            if (points.size() < 3)
                return;
            glm::dvec3 centre(0.0);
            for (const auto &point : points)
                centre += point;
            centre /= static_cast<double>(points.size());
            const glm::dvec3 u = Perpendicular(normal);
            const glm::dvec3 v = glm::cross(normal, u);
            std::vector<std::pair<double, glm::dvec3>> ordered;
            ordered.reserve(points.size());
            for (const auto &point : points)
                ordered.emplace_back(std::atan2(glm::dot(point - centre, v), glm::dot(point - centre, u)), point);
            std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for (std::size_t index = 0; index < points.size(); ++index)
                points[index] = ordered[index].second;
        }

        // Keeps the part of the polytope on the plane's negative side and closes the cut with a face on the plane.
        void ClipPolytope(Polytope &polytope, const Plane &plane, int neighbour, double epsilon)
        {
            bool anyOutside = false;
            bool anyInside = false;
            for (const auto &face : polytope)
                for (const auto &point : face.points)
                {
                    const double distance = plane.Distance(point);
                    anyOutside |= distance > epsilon;
                    anyInside |= distance < -epsilon;
                }
            if (!anyOutside)
                return;
            if (!anyInside)
            {
                polytope.clear();
                return;
            }

            Polytope clipped;
            clipped.reserve(polytope.size() + 1);
            std::vector<glm::dvec3> cut;
            std::vector<double> distances;
            for (auto &face : polytope)
            {
                distances.resize(face.points.size());
                bool faceOutside = false;
                bool faceInside = false;
                for (std::size_t index = 0; index < face.points.size(); ++index)
                {
                    distances[index] = plane.Distance(face.points[index]);
                    faceOutside |= distances[index] > epsilon;
                    faceInside |= distances[index] <= epsilon;
                }
                // Most faces are untouched; keep them without rebuilding.
                if (!faceOutside)
                {
                    for (std::size_t index = 0; index < face.points.size(); ++index)
                        if (distances[index] >= -epsilon)
                            cut.push_back(face.points[index]);
                    clipped.push_back(std::move(face));
                    continue;
                }
                if (!faceInside)
                    continue;

                std::vector<glm::dvec3> kept;
                kept.reserve(face.points.size() + 2);
                for (std::size_t index = 0; index < face.points.size(); ++index)
                {
                    const std::size_t next = (index + 1) % face.points.size();
                    const double da = distances[index];
                    const double db = distances[next];
                    const bool insideA = da <= epsilon;
                    const bool insideB = db <= epsilon;
                    if (insideA)
                    {
                        kept.push_back(face.points[index]);
                        if (da >= -epsilon)
                            cut.push_back(face.points[index]);
                    }
                    if (insideA != insideB)
                    {
                        const double t = std::clamp(da / (da - db), 0.0, 1.0);
                        const glm::dvec3 point = face.points[index] + (face.points[next] - face.points[index]) * t;
                        kept.push_back(point);
                        cut.push_back(point);
                    }
                }
                RemoveRepeatedPoints(kept, epsilon);
                if (kept.size() >= 3)
                    clipped.push_back({std::move(kept), face.plane, face.neighbour});
            }

            // The new face: every point on the plane, ordered counter-clockwise around the plane's normal.
            std::vector<glm::dvec3> unique;
            for (const auto &point : cut)
                if (std::none_of(unique.begin(), unique.end(), [&](const glm::dvec3 &other)
                                 { return glm::dot(point - other, point - other) <= epsilon * epsilon; }))
                    unique.push_back(point);
            if (unique.size() >= 3)
            {
                SortAroundNormal(unique, plane.normal);
                clipped.push_back({std::move(unique), plane, neighbour});
            }
            polytope = std::move(clipped);
        }

        // An axis-aligned box as a polytope; its faces are marked -2 (neither hull nor wall).
        Polytope BoxPolytope(const glm::dvec3 &minimum, const glm::dvec3 &maximum)
        {
            const auto corner = [&](int x, int y, int z)
            { return glm::dvec3(x ? maximum.x : minimum.x, y ? maximum.y : minimum.y, z ? maximum.z : minimum.z); };
            const auto face = [](glm::dvec3 normal, double offset, std::vector<glm::dvec3> points)
            { return PolytopeFace{std::move(points), {normal, offset}, -2}; };
            return {
                face({-1.0, 0.0, 0.0}, -minimum.x, {corner(0, 0, 0), corner(0, 0, 1), corner(0, 1, 1), corner(0, 1, 0)}),
                face({1.0, 0.0, 0.0}, maximum.x, {corner(1, 0, 0), corner(1, 1, 0), corner(1, 1, 1), corner(1, 0, 1)}),
                face({0.0, -1.0, 0.0}, -minimum.y, {corner(0, 0, 0), corner(1, 0, 0), corner(1, 0, 1), corner(0, 0, 1)}),
                face({0.0, 1.0, 0.0}, maximum.y, {corner(0, 1, 0), corner(0, 1, 1), corner(1, 1, 1), corner(1, 1, 0)}),
                face({0.0, 0.0, -1.0}, -minimum.z, {corner(0, 0, 0), corner(0, 1, 0), corner(1, 1, 0), corner(1, 0, 0)}),
                face({0.0, 0.0, 1.0}, maximum.z, {corner(0, 0, 1), corner(1, 0, 1), corner(1, 1, 1), corner(0, 1, 1)}),
            };
        }

        // Keeps the part of a convex polygon on the plane's negative side. False once nothing is left.
        bool ClipPoints(std::vector<glm::dvec3> &points, const Plane &plane, double epsilon, std::vector<glm::dvec3> &scratch)
        {
            bool anyOutside = false;
            for (const auto &point : points)
                anyOutside |= plane.Distance(point) > epsilon;
            if (!anyOutside)
                return points.size() >= 3;
            scratch.clear();
            for (std::size_t index = 0; index < points.size(); ++index)
            {
                const auto &a = points[index];
                const auto &b = points[(index + 1) % points.size()];
                const double da = plane.Distance(a);
                const double db = plane.Distance(b);
                if (da <= epsilon)
                    scratch.push_back(a);
                if ((da <= epsilon) != (db <= epsilon))
                    scratch.push_back(a + (b - a) * std::clamp(da / (da - db), 0.0, 1.0));
            }
            RemoveRepeatedPoints(scratch, epsilon);
            std::swap(points, scratch);
            return points.size() >= 3;
        }

        bool InsideHull(const Hull &hull, const glm::dvec3 &point, double margin)
        {
            return std::all_of(hull.planes.begin(), hull.planes.end(), [&](const Plane &plane) { return plane.Distance(point) <= -margin; });
        }

        Vertex Interpolate(const Vertex &a, const Vertex &b, float t)
        {
            const auto mix = [t](const auto &from, const auto &to)
            {
                auto result = from;
                for (std::size_t index = 0; index < result.size(); ++index)
                    result[index] = from[index] + (to[index] - from[index]) * t;
                return result;
            };
            Vertex result = a;
            result.position = mix(a.position, b.position);
            result.uv = mix(a.uv, b.uv);
            result.uv2 = mix(a.uv2, b.uv2);
            result.weights = mix(a.weights, b.weights);
            result.joints = t < 0.5f ? a.joints : b.joints;

            const glm::vec3 normal = glm::mix(glm::vec3(a.normal[0], a.normal[1], a.normal[2]), glm::vec3(b.normal[0], b.normal[1], b.normal[2]), t);
            if (const float length = glm::length(normal); length > 1e-6f)
                result.normal = {normal.x / length, normal.y / length, normal.z / length};
            const glm::vec3 tangent = glm::mix(glm::vec3(a.tangent[0], a.tangent[1], a.tangent[2]), glm::vec3(b.tangent[0], b.tangent[1], b.tangent[2]), t);
            if (const float length = glm::length(tangent); length > 1e-6f)
                result.tangent = {tangent.x / length, tangent.y / length, tangent.z / length, a.tangent[3]};
            return result;
        }

        // A polygon corner: a source vertex (shared between triangles) or a new one made by a cut.
        struct ClipVertex
        {
            Vertex vertex;
            int source = -1;
        };

        void ClipPolygon(std::vector<ClipVertex> &polygon, const Plane &plane, std::vector<ClipVertex> &scratch)
        {
            scratch.clear();
            for (std::size_t index = 0; index < polygon.size(); ++index)
            {
                const auto &a = polygon[index];
                const auto &b = polygon[(index + 1) % polygon.size()];
                const double da = plane.Distance(Position(a.vertex));
                const double db = plane.Distance(Position(b.vertex));
                if (da <= 0.0)
                    scratch.push_back(a);
                if ((da <= 0.0) != (db <= 0.0))
                    scratch.push_back({Interpolate(a.vertex, b.vertex, static_cast<float>(da / (da - db))), -1});
            }
            std::swap(polygon, scratch);
        }

        struct Cell
        {
            glm::dvec3 seed{0.0};
            Polytope polytope;
            // The walls that bound this cell inside the hull; surface triangles only need clipping against these.
            std::vector<Plane> walls;
            double volume = 0.0;
            glm::dvec3 centroid{0.0};
        };

        struct PieceBuilder
        {
            render::MeshData mesh;
            std::map<std::uint32_t, std::vector<unsigned int>> indicesBySlot;
            std::unordered_map<int, unsigned int> sourceToPiece;

            unsigned int Add(const ClipVertex &corner)
            {
                if (corner.source >= 0)
                {
                    const auto [found, inserted] = sourceToPiece.try_emplace(corner.source, static_cast<unsigned int>(mesh.vertices.size()));
                    if (inserted)
                        mesh.vertices.push_back(corner.vertex);
                    return found->second;
                }
                mesh.vertices.push_back(corner.vertex);
                return static_cast<unsigned int>(mesh.vertices.size() - 1);
            }

            void AddPolygon(const std::vector<ClipVertex> &polygon, std::uint32_t slot, double minimumArea)
            {
                if (polygon.size() < 3)
                    return;
                auto &indices = indicesBySlot[slot];
                for (std::size_t index = 1; index + 1 < polygon.size(); ++index)
                {
                    const glm::dvec3 a = Position(polygon[0].vertex);
                    const glm::dvec3 b = Position(polygon[index].vertex);
                    const glm::dvec3 c = Position(polygon[index + 1].vertex);
                    if (!(glm::length(glm::cross(b - a, c - a)) * 0.5 > minimumArea))
                        continue;
                    indices.push_back(Add(polygon[0]));
                    indices.push_back(Add(polygon[index]));
                    indices.push_back(Add(polygon[index + 1]));
                }
            }
        };

        struct SourceTriangle
        {
            std::array<unsigned int, 3> corners;
            std::uint32_t slot = 0;
        };
    }

    std::vector<FracturePiece> FractureMesh(const render::MeshData &source, const std::vector<render::Submesh> &submeshes,
                                            const FractureSettings &settings)
    {
        const auto vertexCount = source.vertices.size();
        std::vector<bool> finite(vertexCount);
        std::vector<glm::dvec3> hullInput;
        hullInput.reserve(vertexCount);
        for (std::size_t index = 0; index < vertexCount; ++index)
        {
            const glm::dvec3 position = Position(source.vertices[index]);
            finite[index] = IsFinite(position);
            if (finite[index])
                hullInput.push_back(position);
        }

        std::vector<SourceTriangle> triangles;
        const auto collect = [&](std::size_t offset, std::size_t count, std::uint32_t slot)
        {
            for (std::size_t index = offset; index + 2 < offset + count && index + 2 < source.indices.size(); index += 3)
            {
                const std::array<unsigned int, 3> corners{source.indices[index], source.indices[index + 1], source.indices[index + 2]};
                if (std::all_of(corners.begin(), corners.end(), [&](unsigned int corner) { return corner < vertexCount && finite[corner]; }))
                    triangles.push_back({corners, slot});
            }
        };
        if (submeshes.empty())
            collect(0, source.indices.size(), 0);
        for (const auto &submesh : submeshes)
            collect(submesh.indexOffset, submesh.indexCount, submesh.materialIndex);
        if (hullInput.size() < 4 || triangles.empty())
            return {};

        // Seams and poles repeat positions; the hull only needs each once.
        std::sort(hullInput.begin(), hullInput.end(), [](const glm::dvec3 &a, const glm::dvec3 &b)
                  { return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z; });
        hullInput.erase(std::unique(hullInput.begin(), hullInput.end()), hullInput.end());
        if (hullInput.size() < 4)
            return {};
        const Hull hull = BuildHull(hullInput);
        glm::dvec3 hullCentroid;
        const double hullVolume = HullVolume(hull, hullCentroid);
        glm::dvec3 boundsMin(std::numeric_limits<double>::max());
        glm::dvec3 boundsMax(std::numeric_limits<double>::lowest());
        for (const auto &point : hull.vertices)
        {
            boundsMin = glm::min(boundsMin, point);
            boundsMax = glm::max(boundsMax, point);
        }
        const double diagonal = hull.faces.empty() ? 0.0 : glm::length(boundsMax - boundsMin);
        // A flat or vanishing hull has no inside to split.
        if (hull.faces.empty() || !(diagonal > 0.0) || !(hullVolume > diagonal * diagonal * diagonal * 1e-9))
            return {};
        const double epsilon = diagonal * 1e-7;
        const double minimumArea = diagonal * diagonal * 1e-12;

        // Seeds: uniform inside the hull, then pulled towards the impact.
        const int pieceCount = std::clamp(settings.pieceCount, 1, kMaxFracturePieces);
        Random random(settings.seed);
        std::optional<glm::dvec3> impact;
        if (settings.impactPoint && IsFinite(glm::dvec3(*settings.impactPoint)))
        {
            glm::dvec3 point(*settings.impactPoint);
            const double margin = diagonal * 1e-4;
            if (!InsideHull(hull, point, margin))
            {
                // Onto the hull, along the line from its centre.
                double inside = 0.0;
                double outside = 1.0;
                for (int step = 0; step < 32; ++step)
                {
                    const double middle = (inside + outside) * 0.5;
                    (InsideHull(hull, hullCentroid + (point - hullCentroid) * middle, margin) ? inside : outside) = middle;
                }
                point = hullCentroid + (point - hullCentroid) * inside;
            }
            impact = point;
        }
        const double bias = std::clamp(static_cast<double>(settings.impactBias), 0.0, 1.0);
        const double separation = diagonal * 1e-3;
        std::vector<Cell> cells;
        cells.reserve(static_cast<std::size_t>(pieceCount));
        for (int attempt = 0; attempt < pieceCount * 200 && static_cast<int>(cells.size()) < pieceCount; ++attempt)
        {
            glm::dvec3 seed = boundsMin + (boundsMax - boundsMin) * glm::dvec3(random.Unit(), random.Unit(), random.Unit());
            if (!InsideHull(hull, seed, separation))
                continue;
            if (impact)
                seed += (*impact - seed) * (bias * (0.35 + 0.6 * random.Unit()));
            if (std::any_of(cells.begin(), cells.end(), [&](const Cell &cell) { return glm::distance(cell.seed, seed) < separation; }))
                continue;
            cells.push_back({.seed = seed});
        }
        if (cells.empty())
            cells.push_back({.seed = hullCentroid});

        // Each cell's solid is hull ∩ region, where the region is the hull's bounds cut by the bisectors with the
        // other seeds, nearest first (a bisector further than the region's farthest corner cannot cut it, and nor can
        // any after it). The region has a handful of faces, so it is cheap to build; the hull can have thousands. The
        // solid's faces are then the hull faces clipped to the region's walls, plus the walls clipped to the hull.
        const Polytope bounds = BoxPolytope(boundsMin - glm::dvec3(diagonal * 0.01), boundsMax + glm::dvec3(diagonal * 0.01));
        std::vector<int> order(cells.size());
        std::vector<glm::dvec3> clipped;
        std::vector<glm::dvec3> scratchPoints;
        std::vector<const PolytopeFace *> regionWalls;
        std::vector<double> distances;
        std::vector<std::size_t> straddled;
        const std::size_t hullVertexCount = hull.vertices.size();
        for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
        {
            auto &cell = cells[cellIndex];
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](int a, int b)
                      {
                          const double da = glm::distance(cells[a].seed, cell.seed);
                          const double db = glm::distance(cells[b].seed, cell.seed);
                          return da != db ? da < db : a < b;
                      });
            Polytope region = bounds;
            double reach = FarthestPoint(region, cell.seed);
            for (const int other : order)
            {
                if (other == static_cast<int>(cellIndex))
                    continue;
                const glm::dvec3 offset = cells[other].seed - cell.seed;
                const double distance = glm::length(offset);
                if (distance * 0.5 > reach + epsilon)
                    break;
                const glm::dvec3 normal = offset / distance;
                ClipPolytope(region, {normal, glm::dot(normal, (cells[other].seed + cell.seed) * 0.5)}, other, epsilon);
                if (region.empty())
                    break;
                reach = FarthestPoint(region, cell.seed);
            }
            if (region.empty())
                continue;

            regionWalls.clear();
            for (const auto &face : region)
                if (face.neighbour >= 0) // The bounds' own faces lie outside the hull.
                    regionWalls.push_back(&face);
            distances.resize(regionWalls.size() * hullVertexCount);
            for (std::size_t wall = 0; wall < regionWalls.size(); ++wall)
                for (std::size_t vertex = 0; vertex < hullVertexCount; ++vertex)
                    distances[wall * hullVertexCount + vertex] = regionWalls[wall]->plane.Distance(hull.vertices[vertex]);

            // Hull faces inside the walls, clipped where a wall crosses them.
            for (std::size_t face = 0; face < hull.faces.size(); ++face)
            {
                bool outside = false;
                straddled.clear();
                for (std::size_t wall = 0; wall < regionWalls.size() && !outside; ++wall)
                {
                    bool anyOutside = false;
                    bool anyInside = false;
                    for (const int corner : hull.faces[face])
                    {
                        const double distance = distances[wall * hullVertexCount + static_cast<std::size_t>(corner)];
                        anyOutside |= distance > epsilon;
                        anyInside |= distance <= epsilon;
                    }
                    outside = !anyInside;
                    if (anyOutside)
                        straddled.push_back(wall);
                }
                if (outside)
                    continue;
                clipped.clear();
                for (const int corner : hull.faces[face])
                    clipped.push_back(hull.vertices[corner]);
                for (const auto wall : straddled)
                    if (!ClipPoints(clipped, regionWalls[wall]->plane, epsilon, scratchPoints))
                        break;
                if (clipped.size() >= 3)
                    cell.polytope.push_back({clipped, hull.planes[face], -1});
            }

            // Walls inside the hull: the hull's section on each wall's plane (where its edges cross it), cut to the
            // other walls. Every edge counts: one beyond the other walls still shapes the section's outline.
            for (std::size_t wall = 0; wall < regionWalls.size(); ++wall)
            {
                clipped.clear();
                const double *wallDistances = &distances[wall * hullVertexCount];
                for (const auto &[a, b] : hull.edges)
                {
                    const double da = wallDistances[a];
                    const double db = wallDistances[b];
                    if ((da <= 0.0) != (db <= 0.0))
                        clipped.push_back(hull.vertices[a] + (hull.vertices[b] - hull.vertices[a]) * (da / (da - db)));
                }
                SortAroundNormal(clipped, regionWalls[wall]->plane.normal);
                RemoveRepeatedPoints(clipped, epsilon);
                for (std::size_t other = 0; other < regionWalls.size(); ++other)
                    if (other != wall && !ClipPoints(clipped, regionWalls[other]->plane, epsilon, scratchPoints))
                        break;
                if (clipped.size() < 3)
                    continue;
                cell.polytope.push_back({clipped, regionWalls[wall]->plane, regionWalls[wall]->neighbour});
                cell.walls.push_back(regionWalls[wall]->plane);
            }
            cell.volume = PolytopeVolume(cell.polytope, cell.centroid);
        }

        // Surface triangles. A triangle whose corners are all nearest one seed lies wholly in that cell (cells are
        // convex); the rest are clipped against the walls of every cell they touch.
        std::vector<PieceBuilder> pieces(cells.size());
        std::vector<int> owner(vertexCount, -1);
        for (std::size_t index = 0; index < vertexCount; ++index)
        {
            if (!finite[index])
                continue;
            const glm::dvec3 position = Position(source.vertices[index]);
            double nearest = std::numeric_limits<double>::max();
            for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
            {
                const glm::dvec3 offset = position - cells[cellIndex].seed;
                if (const double distance = glm::dot(offset, offset); distance < nearest)
                {
                    nearest = distance;
                    owner[index] = static_cast<int>(cellIndex);
                }
            }
        }
        std::vector<ClipVertex> polygon;
        std::vector<ClipVertex> scratch;
        for (const auto &triangle : triangles)
        {
            const int cellA = owner[triangle.corners[0]];
            const auto corner = [&](int index) { return ClipVertex{source.vertices[triangle.corners[index]], static_cast<int>(triangle.corners[index])}; };
            if (cellA >= 0 && cellA == owner[triangle.corners[1]] && cellA == owner[triangle.corners[2]])
            {
                polygon = {corner(0), corner(1), corner(2)};
                pieces[static_cast<std::size_t>(cellA)].AddPolygon(polygon, triangle.slot, minimumArea);
                continue;
            }
            for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
            {
                const auto &walls = cells[cellIndex].walls;
                if (cells[cellIndex].polytope.empty())
                    continue;
                const bool outside = std::any_of(walls.begin(), walls.end(), [&](const Plane &wall)
                                                 {
                                                     return wall.Distance(Position(source.vertices[triangle.corners[0]])) > 0.0 &&
                                                            wall.Distance(Position(source.vertices[triangle.corners[1]])) > 0.0 &&
                                                            wall.Distance(Position(source.vertices[triangle.corners[2]])) > 0.0;
                                                 });
                if (outside)
                    continue;
                polygon = {corner(0), corner(1), corner(2)};
                for (const auto &wall : walls)
                {
                    ClipPolygon(polygon, wall, scratch);
                    if (polygon.size() < 3)
                        break;
                }
                pieces[cellIndex].AddPolygon(polygon, triangle.slot, minimumArea);
            }
        }

        // Caps: the cell's walls, flat shaded, with UVs projected onto each wall's plane. They take material slot 0.
        const float uvScale = std::isfinite(settings.capUvScale) ? settings.capUvScale : 1.0f;
        for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
            for (const auto &face : cells[cellIndex].polytope)
            {
                if (face.neighbour < 0)
                    continue;
                const glm::dvec3 tangent = Perpendicular(face.plane.normal);
                const glm::dvec3 bitangent = glm::cross(face.plane.normal, tangent);
                polygon.clear();
                for (const auto &point : face.points)
                {
                    Vertex vertex{};
                    vertex.position = {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
                    vertex.normal = {static_cast<float>(face.plane.normal.x), static_cast<float>(face.plane.normal.y), static_cast<float>(face.plane.normal.z)};
                    vertex.uv = {static_cast<float>(glm::dot(point, tangent)) * uvScale, static_cast<float>(glm::dot(point, bitangent)) * uvScale};
                    vertex.uv2 = vertex.uv;
                    vertex.tangent = {static_cast<float>(tangent.x), static_cast<float>(tangent.y), static_cast<float>(tangent.z), 1.0f};
                    polygon.push_back({vertex, -1});
                }
                pieces[cellIndex].AddPolygon(polygon, 0, minimumArea);
            }

        std::vector<FracturePiece> result;
        for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
        {
            auto &builder = pieces[cellIndex];
            const auto &cell = cells[cellIndex];
            FracturePiece piece;
            for (auto &[slot, indices] : builder.indicesBySlot)
            {
                if (indices.empty())
                    continue;
                piece.submeshes.push_back(render::Submesh{
                    .indexOffset = static_cast<std::uint32_t>(piece.mesh.indices.size()),
                    .indexCount = static_cast<std::uint32_t>(indices.size()),
                    .materialIndex = slot,
                });
                piece.mesh.indices.insert(piece.mesh.indices.end(), indices.begin(), indices.end());
            }
            if (piece.mesh.indices.empty())
                continue;

            piece.centroid = glm::vec3(cell.volume > 0.0 ? cell.centroid : cell.seed);
            piece.volume = static_cast<float>(std::max(cell.volume, 0.0));
            piece.mesh.vertices = std::move(builder.mesh.vertices);
            piece.boundsMin = glm::vec3(std::numeric_limits<float>::max());
            piece.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
            for (auto &vertex : piece.mesh.vertices)
            {
                const glm::vec3 position = glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]) - piece.centroid;
                vertex.position = {position.x, position.y, position.z};
                piece.boundsMin = glm::min(piece.boundsMin, position);
                piece.boundsMax = glm::max(piece.boundsMax, position);
            }
            const glm::vec3 size = piece.boundsMax - piece.boundsMin;
            if (std::max({size.x, size.y, size.z}) < settings.minPieceSize)
                continue;
            result.push_back(std::move(piece));
        }
        return result;
    }
}
