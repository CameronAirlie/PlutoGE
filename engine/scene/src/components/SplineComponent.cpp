#include "PlutoGE/scene/components/SplineComponent.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/quaternion.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback = glm::vec3(0.0f))
        {
            glm::vec3 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "True" || value == "1";
        }

        glm::vec3 CatmullRom(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t)
        {
            const float t2 = t * t;
            const float t3 = t2 * t;
            return 0.5f * ((2.0f * p1) +
                           (-p0 + p2) * t +
                           (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                           (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        }

        glm::vec3 CatmullRomDerivative(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t)
        {
            const float t2 = t * t;
            return 0.5f * ((-p0 + p2) +
                           2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t +
                           3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t2);
        }

        glm::vec3 SafeNormalize(const glm::vec3 &value, const glm::vec3 &fallback)
        {
            return glm::dot(value, value) > 0.000001f ? glm::normalize(value) : fallback;
        }

        float DistanceToLine(const glm::vec3 &point, const glm::vec3 &lineStart, const glm::vec3 &lineEnd)
        {
            const glm::vec3 line = lineEnd - lineStart;
            const float lengthSquared = glm::dot(line, line);
            if (lengthSquared <= 0.000001f)
            {
                return glm::length(point - lineStart);
            }
            const float t = glm::clamp(glm::dot(point - lineStart, line) / lengthSquared, 0.0f, 1.0f);
            return glm::length(point - (lineStart + line * t));
        }

        struct AdaptiveInterval
        {
            float start = 0.0f;
            float end = 1.0f;
            float error = 0.0f;
        };

        float CalculateIntervalError(const glm::vec3 &p0,
                                     const glm::vec3 &p1,
                                     const glm::vec3 &p2,
                                     const glm::vec3 &p3,
                                     const glm::quat &startRotation,
                                     const glm::quat &endRotation,
                                     float start,
                                     float end,
                                     float maxChordError,
                                     float maxTangentAngleRadians)
        {
            const float quarter = glm::mix(start, end, 0.25f);
            const float middle = glm::mix(start, end, 0.5f);
            const float threeQuarter = glm::mix(start, end, 0.75f);
            const glm::vec3 startPoint = CatmullRom(p0, p1, p2, p3, start);
            const glm::vec3 endPoint = CatmullRom(p0, p1, p2, p3, end);

            float chordError = 0.0f;
            chordError = std::max(chordError, DistanceToLine(CatmullRom(p0, p1, p2, p3, quarter), startPoint, endPoint));
            chordError = std::max(chordError, DistanceToLine(CatmullRom(p0, p1, p2, p3, middle), startPoint, endPoint));
            chordError = std::max(chordError, DistanceToLine(CatmullRom(p0, p1, p2, p3, threeQuarter), startPoint, endPoint));

            const float sampleParameters[] = {start, quarter, middle, threeQuarter, end};
            float tangentAngle = 0.0f;
            glm::vec3 previousTangent = SafeNormalize(CatmullRomDerivative(p0, p1, p2, p3, sampleParameters[0]), glm::vec3(0.0f, 0.0f, 1.0f));
            for (std::size_t sampleIndex = 1; sampleIndex < std::size(sampleParameters); ++sampleIndex)
            {
                const glm::vec3 tangent = SafeNormalize(CatmullRomDerivative(p0, p1, p2, p3, sampleParameters[sampleIndex]), previousTangent);
                tangentAngle = std::max(tangentAngle, std::acos(glm::clamp(glm::dot(previousTangent, tangent), -1.0f, 1.0f)));
                previousTangent = tangent;
            }

            const float rotationAngle = 2.0f * std::acos(glm::clamp(std::abs(glm::dot(startRotation, endRotation)), 0.0f, 1.0f));
            const float intervalRotationAngle = rotationAngle * (end - start);
            return std::max({chordError / maxChordError,
                             tangentAngle / maxTangentAngleRadians,
                             intervalRotationAngle / maxTangentAngleRadians});
        }

        glm::vec3 GetWrappedPoint(const std::vector<SplineControlPoint> &points, int index, bool closed)
        {
            const int count = static_cast<int>(points.size());
            if (count == 0)
            {
                return glm::vec3(0.0f);
            }

            if (closed)
            {
                index %= count;
                if (index < 0)
                {
                    index += count;
                }
                return points[static_cast<std::size_t>(index)].position;
            }

            return points[static_cast<std::size_t>(std::clamp(index, 0, count - 1))].position;
        }

        const SplineControlPoint &GetWrappedControlPoint(const std::vector<SplineControlPoint> &points, int index, bool closed)
        {
            const int count = static_cast<int>(points.size());
            if (closed)
            {
                index %= count;
                if (index < 0)
                {
                    index += count;
                }
            }
            else
            {
                index = std::clamp(index, 0, count - 1);
            }
            return points[static_cast<std::size_t>(index)];
        }

        glm::quat RotationQuaternion(const glm::vec3 &rotationDegrees)
        {
            const glm::vec3 radians = glm::radians(rotationDegrees);
            return glm::quat_cast(glm::eulerAngleXYZ(radians.x, radians.y, radians.z));
        }

        void AddVertex(render::MeshData &meshData,
                       const glm::vec3 &position,
                       const glm::vec3 &normal,
                       const glm::vec2 &uv,
                       const glm::vec3 &tangent)
        {
            meshData.vertices.push_back(render::MeshVertexData{
                {position.x, position.y, position.z},
                {normal.x, normal.y, normal.z},
                {uv.x, uv.y},
                {tangent.x, tangent.y, tangent.z, 1.0f},
            });
        }

        void AddQuad(render::MeshData &meshData, unsigned int a, unsigned int b, unsigned int c, unsigned int d)
        {
            meshData.indices.push_back(a);
            meshData.indices.push_back(b);
            meshData.indices.push_back(c);
            meshData.indices.push_back(c);
            meshData.indices.push_back(b);
            meshData.indices.push_back(d);
        }

        std::vector<glm::vec3> BuildSplineCenters(const std::vector<SplineControlPoint> &points,
                                                  bool closed,
                                                  int samplesPerSegment, std::vector<glm::quat> *rotations = nullptr,
                                                  int onlySegment = -1, std::vector<glm::vec3> *tangents = nullptr)
        {
            if (rotations) rotations->clear();
            std::vector<glm::vec3> centers;
            if (points.size() < 2)
            {
                return centers;
            }

            const int segmentCount = closed ? static_cast<int>(points.size()) : static_cast<int>(points.size()) - 1;
            if (segmentCount <= 0)
            {
                return centers;
            }

            centers.reserve(static_cast<std::size_t>((onlySegment < 0 ? segmentCount : 1) * samplesPerSegment + 1));
            for (int segment = onlySegment < 0 ? 0 : onlySegment; segment < (onlySegment < 0 ? segmentCount : onlySegment + 1); ++segment)
            {
                for (int sample = 0; sample < samplesPerSegment; ++sample)
                {
                    const float t = static_cast<float>(sample) / static_cast<float>(samplesPerSegment);
                    if (tangents) tangents->push_back(CatmullRomDerivative(GetWrappedPoint(points, segment - 1, closed),
                        GetWrappedPoint(points, segment, closed), GetWrappedPoint(points, segment + 1, closed), GetWrappedPoint(points, segment + 2, closed), t));
                    if (rotations) rotations->push_back(glm::slerp(
                        RotationQuaternion(GetWrappedControlPoint(points, segment, closed).rotation),
                        RotationQuaternion(GetWrappedControlPoint(points, segment + 1, closed).rotation), t));
                    centers.push_back(CatmullRom(
                        GetWrappedPoint(points, segment - 1, closed),
                        GetWrappedPoint(points, segment, closed),
                        GetWrappedPoint(points, segment + 1, closed),
                        GetWrappedPoint(points, segment + 2, closed),
                        t));
                }
            }

            if (!closed || onlySegment >= 0)
            {
                const int end = onlySegment < 0 ? segmentCount : onlySegment + 1;
                centers.push_back(GetWrappedPoint(points, end, closed));
                if (rotations) rotations->push_back(RotationQuaternion(GetWrappedControlPoint(points, end, closed).rotation));
                if (tangents) tangents->push_back(0.5f * (GetWrappedPoint(points, end + 1, closed) - GetWrappedPoint(points, end - 1, closed)));
            }

            return centers;
        }

        void BuildAdaptiveSplineCenters(const std::vector<SplineControlPoint> &points,
                                        bool closed,
                                        int maxSamplesPerSegment,
                                        float maxChordError,
                                        float maxTangentAngleDegrees,
                                        std::vector<glm::vec3> &centers,
                                        std::vector<glm::quat> &rotations, int onlySegment = -1,
                                        std::vector<glm::vec3> *tangents = nullptr)
        {
            centers.clear();
            rotations.clear();
            if (points.size() < 2)
            {
                return;
            }

            const int segmentCount = closed ? static_cast<int>(points.size()) : static_cast<int>(points.size()) - 1;
            if (segmentCount <= 0)
            {
                return;
            }

            centers.reserve(static_cast<std::size_t>((onlySegment < 0 ? segmentCount : 1) * maxSamplesPerSegment + 1));
            rotations.reserve(centers.capacity());
            const float maxTangentAngleRadians = glm::radians(std::clamp(maxTangentAngleDegrees, 0.1f, 90.0f));
            for (int segment = onlySegment < 0 ? 0 : onlySegment; segment < (onlySegment < 0 ? segmentCount : onlySegment + 1); ++segment)
            {
                const glm::vec3 p0 = GetWrappedPoint(points, segment - 1, closed);
                const glm::vec3 p1 = GetWrappedPoint(points, segment, closed);
                const glm::vec3 p2 = GetWrappedPoint(points, segment + 1, closed);
                const glm::vec3 p3 = GetWrappedPoint(points, segment + 2, closed);
                const glm::quat startRotation = RotationQuaternion(GetWrappedControlPoint(points, segment, closed).rotation);
                const glm::quat endRotation = RotationQuaternion(GetWrappedControlPoint(points, segment + 1, closed).rotation);
                std::vector<AdaptiveInterval> intervals = {{0.0f, 1.0f, CalculateIntervalError(p0, p1, p2, p3, startRotation, endRotation, 0.0f, 1.0f, std::max(maxChordError, 0.001f), maxTangentAngleRadians)}};
                while (static_cast<int>(intervals.size()) < maxSamplesPerSegment)
                {
                    const auto worst = std::max_element(intervals.begin(), intervals.end(), [](const AdaptiveInterval &left, const AdaptiveInterval &right)
                                                        { return left.error < right.error; });
                    if (worst == intervals.end() || worst->error <= 1.0f)
                    {
                        break;
                    }

                    const std::size_t intervalIndex = static_cast<std::size_t>(std::distance(intervals.begin(), worst));
                    const float start = worst->start;
                    const float end = worst->end;
                    const float middle = glm::mix(start, end, 0.5f);
                    intervals[intervalIndex] = {start, middle, CalculateIntervalError(p0, p1, p2, p3, startRotation, endRotation, start, middle, std::max(maxChordError, 0.001f), maxTangentAngleRadians)};
                    intervals.insert(intervals.begin() + static_cast<std::ptrdiff_t>(intervalIndex + 1),
                                     {middle, end, CalculateIntervalError(p0, p1, p2, p3, startRotation, endRotation, middle, end, std::max(maxChordError, 0.001f), maxTangentAngleRadians)});
                }

                for (const AdaptiveInterval &interval : intervals)
                {
                    const float t = interval.start;
                    if (tangents) tangents->push_back(CatmullRomDerivative(p0, p1, p2, p3, t));
                    centers.push_back(CatmullRom(p0, p1, p2, p3, t));
                    rotations.push_back(glm::normalize(glm::slerp(startRotation, endRotation, t)));
                }
            }

            if (!closed || onlySegment >= 0)
            {
                const int end = onlySegment < 0 ? segmentCount : onlySegment + 1;
                centers.push_back(GetWrappedPoint(points, end, closed));
                rotations.push_back(RotationQuaternion(GetWrappedControlPoint(points, end, closed).rotation));
                if (tangents) tangents->push_back(0.5f * (GetWrappedPoint(points, end + 1, closed) - GetWrappedPoint(points, end - 1, closed)));
            }
        }

        std::unique_ptr<render::Mesh> BuildSplineMesh(const std::vector<glm::vec3> &centers,
                                                      const std::vector<glm::quat> *rotations,
                                                      float width,
                                                      float thickness,
                                                      float uvMetersPerTile,
                                                      bool closed,
                                                      bool includeSideFaces = true, float guardrailHeight = 0, bool initializeGraphics = true, int lodCount = 1,
                                                      const std::vector<glm::vec3> *tangents = nullptr)
        {
            if (centers.size() < 2)
            {
                return nullptr;
            }

            render::MeshData meshData;
            const float halfWidth = width * 0.5f;
            const float bottomOffset = std::max(thickness, 0.0f);
            float distance = 0.0f;
            std::vector<float> distances(centers.size(), 0.0f);
            for (std::size_t index = 1; index < centers.size(); ++index)
            {
                distance += glm::length(centers[index] - centers[index - 1]);
                distances[index] = distance;
            }

            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            for (std::size_t index = 0; index < centers.size(); ++index)
            {
                const glm::vec3 previous = index == 0 ? (closed ? centers[centers.size() - 1] : centers[index]) : centers[index - 1];
                const glm::vec3 next = index + 1 < centers.size() ? centers[index + 1] : (closed ? centers[0] : centers[index]);
                glm::vec3 tangent = tangents && tangents->size() == centers.size() ? (*tangents)[index] : next - previous;
                if (glm::dot(tangent, tangent) <= 0.000001f)
                {
                    tangent = glm::vec3(0.0f, 0.0f, 1.0f);
                }
                tangent = glm::normalize(tangent);

                glm::vec3 defaultRight = glm::cross(tangent, up);
                if (glm::dot(defaultRight, defaultRight) <= 0.000001f)
                {
                    defaultRight = glm::cross(tangent, glm::vec3(0.0f, 0.0f, 1.0f));
                }
                defaultRight = SafeNormalize(defaultRight, glm::vec3(1.0f, 0.0f, 0.0f));

                glm::vec3 right = defaultRight;
                if (rotations && rotations->size() == centers.size())
                {
                    const glm::vec3 baseX = -defaultRight;
                    const glm::vec3 baseY = SafeNormalize(glm::cross(tangent, baseX), glm::vec3(0.0f, 1.0f, 0.0f));
                    const glm::mat3 splineFrame(baseX, baseY, tangent);
                    const glm::vec3 preferredRight = splineFrame * ((*rotations)[index] * glm::vec3(-1.0f, 0.0f, 0.0f));
                    right = preferredRight - tangent * glm::dot(preferredRight, tangent);
                    right = SafeNormalize(right, defaultRight);
                }

                const glm::vec3 surfaceNormal = glm::normalize(glm::cross(right, tangent));

                const float v = distances[index] / uvMetersPerTile;
                AddVertex(meshData, centers[index] - right * halfWidth, surfaceNormal, glm::vec2(0.0f, v), right);
                AddVertex(meshData, centers[index] + right * halfWidth, surfaceNormal, glm::vec2(1.0f, v), right);
                AddVertex(meshData, centers[index] - right * halfWidth - surfaceNormal * bottomOffset, -surfaceNormal, glm::vec2(0.0f, v), right);
                AddVertex(meshData, centers[index] + right * halfWidth - surfaceNormal * bottomOffset, -surfaceNormal, glm::vec2(1.0f, v), right);
            }

            const auto sideBase = static_cast<unsigned>(centers.size() * (guardrailHeight > 0 ? 8 : 4));
            const std::size_t edgeCount = closed ? centers.size() : centers.size() - 1;
            for (std::size_t index = 0; index < edgeCount; ++index)
            {
                const std::size_t nextIndex = (index + 1) % centers.size();
                const auto base = static_cast<unsigned int>(index * 4);
                const auto nextBase = static_cast<unsigned int>(nextIndex * 4);
                AddQuad(meshData, base + 0, base + 1, nextBase + 0, nextBase + 1);
                if (includeSideFaces && bottomOffset > 0.0f)
                {
                    AddQuad(meshData, sideBase + base, sideBase + base + 1, sideBase + nextBase, sideBase + nextBase + 1);
                    AddQuad(meshData, sideBase + base + 2, sideBase + base + 3, sideBase + nextBase + 2, sideBase + nextBase + 3);
                    AddQuad(meshData, base + 3, base + 2, nextBase + 3, nextBase + 2);
                }
            }

            if (guardrailHeight > 0)
            {
                // Continuous edge ribbons use the same banked cross-section as
                // the road surface. Collision and visible geometry share this path.
                for (unsigned side = 0; side < 2; ++side)
                {
                    const auto railBase = static_cast<unsigned>(meshData.vertices.size());
                    for (std::size_t row = 0; row < centers.size(); ++row)
                    {
                        const auto edge = meshData.vertices[row * 4 + side];
                        const glm::vec3 position(edge.position[0], edge.position[1], edge.position[2]);
                        const glm::vec3 up(edge.normal[0], edge.normal[1], edge.normal[2]);
                        const glm::vec3 right(edge.tangent[0], edge.tangent[1], edge.tangent[2]);
                        const glm::vec3 normal = side ? right : -right;
                        AddVertex(meshData, position, normal, {distances[row] / uvMetersPerTile, 0}, up);
                        AddVertex(meshData, position + up * guardrailHeight, normal, {distances[row] / uvMetersPerTile, 1}, up);
                    }
                    for (std::size_t row = 0; row < edgeCount; ++row)
                    {
                        const auto a = railBase + static_cast<unsigned>(row * 2);
                        const auto b = railBase + static_cast<unsigned>(((row + 1) % centers.size()) * 2);
                        // Both faces are present so ribbons remain visible from
                        // inside and outside the road without material changes.
                        AddQuad(meshData, a, a + 1, b, b + 1);
                        AddQuad(meshData, a + 1, a, b + 1, b);
                    }
                }
            }

            // Hard edges require separate side vertices; sharing top/bottom
            // normals across vertical faces produces diagonal shading artifacts.
            constexpr std::array<unsigned, 4> sideCorners{2, 0, 1, 3};
            if (includeSideFaces && bottomOffset > 0)
                for (std::size_t row = 0; row < centers.size(); ++row)
                    for (unsigned corner : sideCorners)
                    {
                        auto vertex = meshData.vertices[row * 4 + corner];
                        const auto &edge = meshData.vertices[row * 4];
                        const float sign = (corner == 0 || corner == 2) ? -1.0f : 1.0f;
                        for (unsigned axis = 0; axis < 3; ++axis)
                            vertex.normal[axis] = sign * edge.tangent[axis];
                        vertex.uv[0] = (corner >= 2 ? bottomOffset / uvMetersPerTile : 0.0f);
                        vertex.tangent = {0, 0, 0, 1}; // Recompute from face UVs.
                        meshData.vertices.push_back(vertex);
                    }

            render::MeshConfig config;
            config.data = std::move(meshData);
            config.submeshes.push_back(render::Submesh{
                .indexOffset = 0,
                .indexCount = static_cast<uint32_t>(config.data.indices.size()),
                .materialIndex = 0,
                .name = "Spline Track",
            });

            // LODs reuse the original cross-sections: bank, UVs and endpoints
            // cannot drift as the renderer changes detail. Collision stays at
            // its independently configured sampling resolution.
            auto &submesh = config.submeshes.front();
            submesh.lods.push_back({0, submesh.indexCount});
            for (int level = 1; level < lodCount; ++level)
            {
                const std::size_t stride = std::size_t{1} << level;
                std::vector<std::size_t> rows;
                for (std::size_t row = 0; row < centers.size(); row += stride) rows.push_back(row);
                if (!closed && rows.back() != centers.size() - 1) rows.push_back(centers.size() - 1);
                if (rows.size() < (closed ? 3u : 2u)) break;
                const auto offset = static_cast<uint32_t>(config.data.indices.size());
                const auto edges = closed ? rows.size() : rows.size() - 1;
                for (std::size_t edge = 0; edge < edges; ++edge)
                {
                    const auto row = rows[edge], next = rows[(edge + 1) % rows.size()];
                    const auto a = static_cast<unsigned>(row * 4), b = static_cast<unsigned>(next * 4);
                    AddQuad(config.data, a, a + 1, b, b + 1);
                    if (includeSideFaces && bottomOffset > 0)
                    {
                        AddQuad(config.data, sideBase + a, sideBase + a + 1, sideBase + b, sideBase + b + 1);
                        AddQuad(config.data, sideBase + a + 2, sideBase + a + 3, sideBase + b + 2, sideBase + b + 3);
                        AddQuad(config.data, a + 3, a + 2, b + 3, b + 2);
                    }
                    if (guardrailHeight > 0)
                        for (unsigned side = 0; side < 2; ++side)
                        {
                            const auto rail = static_cast<unsigned>(centers.size() * (4 + side * 2));
                            const auto c = rail + static_cast<unsigned>(row * 2), d = rail + static_cast<unsigned>(next * 2);
                            AddQuad(config.data, c, c + 1, d, d + 1);
                            AddQuad(config.data, c + 1, c, d + 1, d);
                        }
                }
                const auto count = static_cast<uint32_t>(config.data.indices.size()) - offset;
                if (count >= submesh.lods.back().indexCount)
                {
                    config.data.indices.resize(offset);
                    break;
                }
                submesh.lods.push_back({offset, count, 0.0f, 256.0f / static_cast<float>(stride)});
            }
            return initializeGraphics ? std::unique_ptr<render::Mesh>(render::Mesh::CreateInitialized(config))
                                      : std::make_unique<render::Mesh>(config);
        }

    }

    struct SplineBuildCache
    {
        struct Segment
        {
            std::array<SplineControlPoint, 4> controlPoints;
            bool valid = false;
            std::unique_ptr<render::Mesh> visual, collision;
            std::vector<glm::vec3> collisionPath;
            std::size_t visualRows = 0;
            float visualLength = 0, collisionLength = 0;
        };
        std::vector<double> settings;
        std::vector<Segment> segments;
    };

    SplineComponent::~SplineComponent() = default;

    SplineComponent::SplineComponent(const SplineComponentConfig &config)
        : m_points(config.points),
          m_width(std::max(config.width, 0.05f)),
          m_thickness(std::max(config.thickness, 0.0f)),
          m_samplesPerSegment(std::max(1, static_cast<int>(std::round(config.samplesPerSegment)))),
          m_collisionSamplesPerSegment(std::max(1, static_cast<int>(std::round(config.collisionSamplesPerSegment)))),
          m_maxChordError(std::max(config.maxChordError, 0.001f)),
          m_maxTangentAngleDegrees(std::clamp(config.maxTangentAngleDegrees, 0.1f, 90.0f)),
          m_uvMetersPerTile(std::max(config.uvMetersPerTile, 0.01f)),
          m_closed(config.closed),
          m_generateMesh(config.generateMesh),
          m_generateCollision(config.generateCollision),
          m_material(config.material),
          m_materialAssetReference(config.materialAssetReference)
    {
        SetGuardrailHeight(config.guardrailHeight);
        SetLodCount(config.lodCount);
        EnsureDefaultPoints();
        RebuildMaterialFromReference();
    }

    void SplineComponent::EnsureDefaultPoints()
    {
        if (!m_points.empty())
        {
            return;
        }

        m_points = {
            {{-20.0f, 0.0f, -20.0f}},
            {{20.0f, 0.0f, -20.0f}},
            {{24.0f, 0.0f, 16.0f}},
            {{-18.0f, 0.0f, 20.0f}},
        };
    }

    void SplineComponent::Update(float deltaTime)
    {
        (void)deltaTime;
        if (m_dirty)
        {
            Rebuild();
        }
    }

    void SplineComponent::SetPoints(std::vector<SplineControlPoint> points)
    {
        m_points = std::move(points);
        EnsureDefaultPoints();
        MarkDirty();
    }

    std::vector<SplineControlPoint> SplineComponent::SampleControlPoints(int samplesPerSegment) const
    {
        std::vector<SplineControlPoint> sampledPoints;
        if (m_points.size() < 2)
        {
            return m_points;
        }

        samplesPerSegment = std::clamp(samplesPerSegment, 1, 128);
        const int segmentCount = m_closed ? static_cast<int>(m_points.size()) : static_cast<int>(m_points.size()) - 1;
        sampledPoints.reserve(static_cast<std::size_t>(segmentCount * samplesPerSegment + (m_closed ? 0 : 1)));
        for (int segment = 0; segment < segmentCount; ++segment)
        {
            const glm::vec3 p0 = GetWrappedPoint(m_points, segment - 1, m_closed);
            const glm::vec3 p1 = GetWrappedPoint(m_points, segment, m_closed);
            const glm::vec3 p2 = GetWrappedPoint(m_points, segment + 1, m_closed);
            const glm::vec3 p3 = GetWrappedPoint(m_points, segment + 2, m_closed);
            const glm::quat startRotation = RotationQuaternion(GetWrappedControlPoint(m_points, segment, m_closed).rotation);
            const glm::quat endRotation = RotationQuaternion(GetWrappedControlPoint(m_points, segment + 1, m_closed).rotation);
            for (int sample = 0; sample < samplesPerSegment; ++sample)
            {
                const float t = static_cast<float>(sample) / static_cast<float>(samplesPerSegment);
                    const glm::quat rotation = glm::normalize(glm::slerp(startRotation, endRotation, t));
                glm::vec3 euler;
                glm::extractEulerAngleXYZ(glm::mat4_cast(rotation), euler.x, euler.y, euler.z);
                sampledPoints.push_back({CatmullRom(p0, p1, p2, p3, t), glm::degrees(euler)});
            }
        }

        if (!m_closed)
        {
            sampledPoints.push_back(m_points.back());
        }
        return sampledPoints;
    }

    void SplineComponent::AddPoint(const glm::vec3 &position)
    {
        m_points.push_back({position});
        MarkDirty();
    }

    void SplineComponent::InsertPoint(std::size_t index, const glm::vec3 &position)
    {
        index = std::min(index, m_points.size());
        m_points.insert(m_points.begin() + static_cast<std::ptrdiff_t>(index), {position});
        MarkDirty();
    }

    void SplineComponent::RemovePoint(std::size_t index)
    {
        if (index >= m_points.size() || m_points.size() <= 2)
        {
            return;
        }

        m_points.erase(m_points.begin() + static_cast<std::ptrdiff_t>(index));
        MarkDirty();
    }

    void SplineComponent::SetPointPosition(std::size_t index, const glm::vec3 &position)
    {
        if (index >= m_points.size())
        {
            return;
        }

        m_points[index].position = position;
        MarkDirty();
    }

    void SplineComponent::SetPointRotation(std::size_t index, const glm::vec3 &rotation)
    {
        if (index >= m_points.size())
        {
            return;
        }

        m_points[index].rotation = rotation;
        MarkDirty();
    }

    void SplineComponent::SetWidth(float width)
    {
        m_width = std::max(width, 0.05f);
        MarkDirty();
    }

    void SplineComponent::SetGuardrailHeight(float height)
    {
        if (!std::isfinite(height)) return;
        m_guardrailHeight = std::clamp(height, 0.0f, 10.0f);
        MarkDirty();
    }

    void SplineComponent::SetLodCount(int count)
    {
        m_lodCount = std::clamp(count, 1, 4);
        MarkDirty();
    }

    void SplineComponent::SetThickness(float thickness)
    {
        m_thickness = std::max(thickness, 0.0f);
        MarkDirty();
    }

    void SplineComponent::SetSamplesPerSegment(int samplesPerSegment)
    {
        m_samplesPerSegment = std::clamp(samplesPerSegment, 1, 128);
        MarkDirty();
    }

    void SplineComponent::SetCollisionSamplesPerSegment(int collisionSamplesPerSegment)
    {
        m_collisionSamplesPerSegment = std::clamp(collisionSamplesPerSegment, 1, 128);
        MarkDirty();
    }

    void SplineComponent::SetMaxChordError(float maxChordError)
    {
        m_maxChordError = std::max(maxChordError, 0.001f);
        MarkDirty();
    }

    void SplineComponent::SetMaxTangentAngleDegrees(float maxTangentAngleDegrees)
    {
        m_maxTangentAngleDegrees = std::clamp(maxTangentAngleDegrees, 0.1f, 90.0f);
        MarkDirty();
    }

    void SplineComponent::SetUvMetersPerTile(float uvMetersPerTile)
    {
        m_uvMetersPerTile = std::max(uvMetersPerTile, 0.01f);
        MarkDirty();
    }

    void SplineComponent::SetClosed(bool closed)
    {
        m_closed = closed;
        MarkDirty();
    }

    void SplineComponent::SetGenerateMesh(bool generateMesh)
    {
        m_generateMesh = generateMesh;
        MarkDirty();
    }

    void SplineComponent::SetGenerateCollision(bool generateCollision)
    {
        m_generateCollision = generateCollision;
        MarkDirty();
    }

    void SplineComponent::SetMaterial(render::Material *material)
    {
        m_material = material;
        ApplyGeneratedComponents();
    }

    void SplineComponent::SetMaterialAssetReference(const std::string &materialAssetReference)
    {
        m_materialAssetReference = materialAssetReference;
        RebuildMaterialFromReference();
        ApplyGeneratedComponents();
    }

    void SplineComponent::RebuildMaterialFromReference()
    {
        if (m_materialAssetReference.empty())
        {
            return;
        }

        m_material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(m_materialAssetReference);
    }

    void SplineComponent::Rebuild()
    {
        EnsureDefaultPoints();
        m_lastRebuiltSegmentCount = 0;
        if (!m_buildCache) m_buildCache = std::make_unique<SplineBuildCache>();
        const std::vector<double> settings = {m_width, m_thickness, m_guardrailHeight,
            double(m_lodCount), double(m_samplesPerSegment), double(m_collisionSamplesPerSegment),
            m_maxChordError, m_maxTangentAngleDegrees, m_uvMetersPerTile,
            double(m_closed), double(m_generateMesh), double(m_generateCollision)};
        if (m_buildCache->settings != settings)
        {
            m_buildCache->segments.clear();
            m_buildCache->settings = settings;
        }
        const auto count = m_points.size() < 2 ? 0 : m_points.size() - (m_closed ? 0 : 1);
        m_buildCache->segments.resize(count);
        render::MeshConfig visual, collision;
        m_collisionPathPoints.clear();
        float visualOffset = 0, collisionOffset = 0;
        const auto pathLength = [](const std::vector<glm::vec3> &path)
        {
            float length = 0;
            for (std::size_t i = 1; i < path.size(); ++i) length += glm::length(path[i] - path[i - 1]);
            return length;
        };
        const auto append = [&](const render::Mesh &mesh, std::size_t rows, float distance, render::MeshConfig &output)
        {
            const auto vertexOffset = static_cast<unsigned>(output.data.vertices.size());
            const auto indexOffset = static_cast<uint32_t>(output.data.indices.size());
            const auto &data = mesh.GetMeshData();
            for (std::size_t index = 0; index < data.vertices.size(); ++index)
            {
                auto vertex = data.vertices[index];
                // Longitudinal road UV is V; guardrails use U. Applying the
                // prefix distance at assembly keeps cached geometry local.
                vertex.uv[index < rows * 4 || index >= rows * (m_guardrailHeight > 0 ? 8 : 4) ? 1 : 0] += distance / m_uvMetersPerTile;
                output.data.vertices.push_back(vertex);
            }
            for (auto index : data.indices) output.data.indices.push_back(index + vertexOffset);
            for (std::size_t index = 0; index < mesh.GetSubmeshCount(); ++index)
            {
                auto submesh = mesh.GetSubmesh(index);
                submesh.indexOffset += indexOffset;
                for (auto &lod : submesh.lods) lod.indexOffset += indexOffset;
                output.submeshes.push_back(std::move(submesh));
            }
        };
        for (std::size_t index = 0; index < count; ++index)
        {
            auto &segment = m_buildCache->segments[index];
            std::array<SplineControlPoint, 4> points;
            for (int offset = -1; offset <= 2; ++offset)
                points[offset + 1] = GetWrappedControlPoint(m_points, static_cast<int>(index) + offset, m_closed);
            const bool same = segment.valid && std::equal(points.begin(), points.end(), segment.controlPoints.begin(),
                [](const auto &a, const auto &b) { return a.position == b.position && a.rotation == b.rotation; });
            if (!same)
            {
                segment.valid = false;
                segment.visual.reset(); segment.collision.reset(); segment.collisionPath.clear();
                std::vector<glm::vec3> centers, tangents;
                std::vector<glm::quat> rotations;
                if (m_generateMesh)
                {
                    BuildAdaptiveSplineCenters(m_points, m_closed, m_samplesPerSegment, m_maxChordError,
                        m_maxTangentAngleDegrees, centers, rotations, static_cast<int>(index), &tangents);
                    segment.visual = BuildSplineMesh(centers, &rotations, m_width, m_thickness, m_uvMetersPerTile,
                        false, true, m_guardrailHeight, false, m_lodCount, &tangents);
                    segment.visualRows = centers.size();
                    segment.visualLength = pathLength(centers);
                }
                if (m_generateCollision)
                {
                    tangents.clear(); rotations.clear();
                    segment.collisionPath = BuildSplineCenters(m_points, m_closed,
                        std::min(m_samplesPerSegment, m_collisionSamplesPerSegment), &rotations, static_cast<int>(index), &tangents);
                    segment.collision = BuildSplineMesh(segment.collisionPath, &rotations, m_width, m_thickness, m_uvMetersPerTile,
                        false, false, m_guardrailHeight, false, 1, &tangents);
                    segment.collisionLength = pathLength(segment.collisionPath);
                }
                segment.controlPoints = points;
                segment.valid = true;
                ++m_lastRebuiltSegmentCount;
            }
            if (segment.visual) append(*segment.visual, segment.visualRows, visualOffset, visual);
            if (segment.collision) append(*segment.collision, segment.collisionPath.size(), collisionOffset, collision);
            visualOffset += segment.visualLength;
            collisionOffset += segment.collisionLength;
            m_collisionPathPoints.insert(m_collisionPathPoints.end(),
                segment.collisionPath.begin() + (index > 0 && !segment.collisionPath.empty() ? 1 : 0), segment.collisionPath.end());
        }
        if (m_closed && !m_collisionPathPoints.empty()) m_collisionPathPoints.pop_back();
        // CPU geometry is rebuilt only for changed Catmull-Rom neighbourhoods.
        // GPU publication is a single owning-thread mesh replacement; each cached
        // segment becomes a submesh with independent renderer-selected LODs.
        m_generatedMesh.reset(visual.data.indices.empty() ? nullptr : render::Mesh::CreateInitialized(visual));
        m_generatedCollisionMesh = collision.data.indices.empty() ? nullptr : std::make_unique<render::Mesh>(collision);
        m_dirty = false;
        ApplyGeneratedComponents();
    }

    bool SplineComponent::GetEndpointEdge(bool atEnd, std::array<glm::vec3, 2> &edge, bool bottom) const
    {
        if (m_closed || !m_buildCache || m_buildCache->segments.empty()) return false;
        const auto &segment = atEnd ? m_buildCache->segments.back() : m_buildCache->segments.front();
        if (!segment.visual || segment.visualRows < 2) return false;
        const auto row = atEnd ? segment.visualRows - 1 : 0;
        for (unsigned side = 0; side < 2; ++side)
        {
            const auto &position = segment.visual->GetMeshData().vertices[row * 4 + side + (bottom ? 2 : 0)].position;
            edge[side] = {position[0], position[1], position[2]};
        }
        return true;
    }

    void SplineComponent::ApplyGeneratedComponents()
    {
        auto *owner = GetOwner();
        if (!owner)
        {
            return;
        }

        auto *meshComponent = owner->GetComponent<MeshComponent>();
        if (!meshComponent && m_generateMesh)
        {
            meshComponent = owner->CreateComponent<MeshComponent>(MeshComponentConfig{});
        }

        if (meshComponent)
        {
            meshComponent->SetMesh(m_generateMesh ? m_generatedMesh.get() : nullptr);
            meshComponent->SetSubmeshIndex(-1);
            // An unassigned spline material must not clear a material chosen in
            // the Mesh inspector when control points regenerate the geometry.
            if (m_material) meshComponent->SetMaterial(m_material);
            if (!m_materialAssetReference.empty())
            {
                meshComponent->SetMaterialAssetForMaterialSlot(0, m_materialAssetReference);
            }
            meshComponent->SetStatic(true);
        }

        auto *collider = owner->GetComponent<ColliderComponent>();
        if (!collider && m_generateCollision)
        {
            collider = owner->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Mesh});
        }
        if (collider && m_generateCollision)
        {
            collider->SetShape(ColliderShape::Mesh);
            collider->SetTrigger(false);
        }
    }

    bool SplineComponent::ExportMeshAsset(assets::AssetManager &assets, const std::string &reference, std::string *error)
    {
        if (m_dirty) Rebuild();
        if (!m_generatedMesh)
        {
            if (error) *error = "Enable road mesh generation before exporting.";
            return false;
        }
        render::MeshConfig config;
        config.data = m_generatedMesh->GetMeshData();
        for (std::size_t index = 0; index < m_generatedMesh->GetSubmeshCount(); ++index)
            config.submeshes.push_back(m_generatedMesh->GetSubmesh(index));
        return assets.SaveMeshAsset(reference, config, {m_materialAssetReference}, error);
    }

    std::vector<Property> SplineComponent::Serialize() const
    {
        std::vector<Property> properties = {
            {"Width", PropertyType::Float, std::to_string(m_width)},
            {"Thickness", PropertyType::Float, std::to_string(m_thickness)},
            {"GuardrailHeight", PropertyType::Float, std::to_string(m_guardrailHeight)},
            {"LodCount", PropertyType::Int, std::to_string(m_lodCount)},
            {"SamplesPerSegment", PropertyType::Int, std::to_string(m_samplesPerSegment)},
            {"CollisionSamplesPerSegment", PropertyType::Int, std::to_string(m_collisionSamplesPerSegment)},
            {"MaxChordError", PropertyType::Float, std::to_string(m_maxChordError)},
            {"MaxTangentAngleDegrees", PropertyType::Float, std::to_string(m_maxTangentAngleDegrees)},
            {"UvMetersPerTile", PropertyType::Float, std::to_string(m_uvMetersPerTile)},
            {"Closed", PropertyType::Bool, m_closed ? "true" : "false"},
            {"GenerateMesh", PropertyType::Bool, m_generateMesh ? "true" : "false"},
            {"GenerateCollision", PropertyType::Bool, m_generateCollision ? "true" : "false"},
            {"MaterialAsset", PropertyType::String, m_materialAssetReference},
            {"PointCount", PropertyType::Int, std::to_string(m_points.size())},
        };

        for (std::size_t index = 0; index < m_points.size(); ++index)
        {
            properties.push_back({"Points." + std::to_string(index), PropertyType::Vec3, SerializeVec3(m_points[index].position)});
            properties.push_back({"PointRotations." + std::to_string(index), PropertyType::Vec3, SerializeVec3(m_points[index].rotation)});
        }

        return properties;
    }

    void SplineComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::vector<SplineControlPoint> deserializedPoints;
        int pointCount = -1;

        for (const auto &property : properties)
        {
            if (property.name == "Width")
                m_width = std::max(std::stof(property.value), 0.05f);
            else if (property.name == "GuardrailHeight")
                SetGuardrailHeight(std::stof(property.value));
            else if (property.name == "LodCount")
                SetLodCount(std::stoi(property.value));
            else if (property.name == "Thickness")
                m_thickness = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "SamplesPerSegment")
                m_samplesPerSegment = std::clamp(std::stoi(property.value), 1, 128);
            else if (property.name == "CollisionSamplesPerSegment")
                m_collisionSamplesPerSegment = std::clamp(std::stoi(property.value), 1, 128);
            else if (property.name == "MaxChordError")
                m_maxChordError = std::max(std::stof(property.value), 0.001f);
            else if (property.name == "MaxTangentAngleDegrees")
                m_maxTangentAngleDegrees = std::clamp(std::stof(property.value), 0.1f, 90.0f);
            else if (property.name == "UvMetersPerTile")
                m_uvMetersPerTile = std::max(std::stof(property.value), 0.01f);
            else if (property.name == "Closed")
                m_closed = ParseBool(property.value);
            else if (property.name == "GenerateMesh")
                m_generateMesh = ParseBool(property.value);
            else if (property.name == "GenerateCollision")
                m_generateCollision = ParseBool(property.value);
            else if (property.name == "MaterialAsset")
                m_materialAssetReference = property.value;
            else if (property.name == "PointCount")
                pointCount = std::max(std::stoi(property.value), 0);
            else if (property.name.rfind("Points.", 0) == 0)
            {
                const auto index = static_cast<std::size_t>(std::stoul(property.name.substr(7)));
                if (index >= deserializedPoints.size())
                {
                    deserializedPoints.resize(index + 1);
                }
                deserializedPoints[index].position = ParseVec3(property.value);
            }
            else if (property.name.rfind("PointRotations.", 0) == 0)
            {
                const auto index = static_cast<std::size_t>(std::stoul(property.name.substr(15)));
                if (index >= deserializedPoints.size())
                {
                    deserializedPoints.resize(index + 1);
                }
                deserializedPoints[index].rotation = ParseVec3(property.value);
            }
        }

        if (pointCount >= 0 && deserializedPoints.size() > static_cast<std::size_t>(pointCount))
        {
            deserializedPoints.resize(static_cast<std::size_t>(pointCount));
        }
        if (!deserializedPoints.empty())
        {
            m_points = std::move(deserializedPoints);
        }

        EnsureDefaultPoints();
        RebuildMaterialFromReference();
        Rebuild();
    }
}
