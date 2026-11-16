#include "PlutoGE/scene/water/WaterSurfaceSnapshot.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/RiverComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace PlutoGE::scene::water
{
    namespace
    {
        // Every active entity, depth first.
        template <typename Visit>
        void ForEachActiveEntity(const Scene &scene, Visit &&visit)
        {
            const auto walk = [&](auto &&self, const Entity *entity) -> void {
                if (!entity->IsActive())
                    return;
                visit(*entity);
                for (const auto *child : entity->GetChildren())
                    self(self, child);
            };
            for (const auto *root : scene.GetRootEntities())
                walk(walk, root);
        }

        // FNV-1a over raw values: a change detector, not a stable identifier.
        struct SignatureHash
        {
            std::uint64_t value = 1469598103934665603ull;
            void Bytes(const void *data, std::size_t size)
            {
                const auto *bytes = static_cast<const unsigned char *>(data);
                for (std::size_t i = 0; i < size; ++i)
                    value = (value ^ bytes[i]) * 1099511628211ull;
            }
            template <typename T>
            void Add(const T &item) { Bytes(&item, sizeof(T)); }
        };

        glm::vec2 NearestOnSegment(glm::vec2 point, glm::vec2 a, glm::vec2 b)
        {
            const glm::vec2 edge = b - a;
            const float lengthSquared = glm::dot(edge, edge);
            const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(point - a, edge) / lengthSquared, 0.0f, 1.0f) : 0.0f;
            return a + edge * t;
        }
    }

    WaterSurfaceSnapshot WaterSurfaceSnapshot::Capture(const Scene &scene)
    {
        WaterSurfaceSnapshot snapshot;
        ForEachActiveEntity(scene, [&](const Entity &entity) {
            for (const auto *ocean : entity.GetComponents<OceanComponent>())
            {
                if (!ocean->IsEnabled())
                    continue;
                Ocean copy;
                copy.component = ocean;
                copy.entity = entity.GetID();
                copy.localToWorld = entity.GetWorldTransform();
                const float determinant = glm::determinant(copy.localToWorld);
                if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f)
                    continue;
                copy.worldToLocal = glm::inverse(copy.localToWorld);
                copy.invertAreaMask = ocean->GetInvertAreaMask();
                for (const auto &area : ocean->GetAreas())
                    copy.areas.push_back(area.points);
                copy.spectrum = ocean->GetWaveSpectrum();
                snapshot.m_oceans.push_back(std::move(copy));
            }
            for (const auto *river : entity.GetComponents<RiverComponent>())
            {
                if (!river->IsEnabled())
                    continue;
                River copy;
                copy.component = river;
                copy.entity = entity.GetID();
                copy.path = river->GetWorldPath();
                copy.halfWidth = river->GetHalfWidth();
                copy.flowSpeed = river->GetFlowSpeed();
                if (copy.path.size() < 2 || copy.halfWidth <= 0.0f)
                    continue;
                copy.boundsMin = glm::vec2(std::numeric_limits<float>::max());
                copy.boundsMax = glm::vec2(std::numeric_limits<float>::lowest());
                for (const auto &point : copy.path)
                {
                    copy.boundsMin = glm::min(copy.boundsMin, glm::vec2(point.x, point.z));
                    copy.boundsMax = glm::max(copy.boundsMax, glm::vec2(point.x, point.z));
                }
                copy.boundsMin -= copy.halfWidth;
                copy.boundsMax += copy.halfWidth;
                snapshot.m_rivers.push_back(std::move(copy));
            }
        });
        if (!snapshot.Empty())
        {
            SignatureHash hash;
            for (const auto &ocean : snapshot.m_oceans)
            {
                hash.Add(ocean.entity);
                hash.Add(ocean.localToWorld);
                hash.Add(ocean.invertAreaMask);
                for (const auto &area : ocean.areas)
                {
                    hash.Add(area.size());
                    hash.Bytes(area.data(), area.size() * sizeof(glm::vec2));
                }
            }
            for (const auto &river : snapshot.m_rivers)
            {
                hash.Add(river.entity);
                hash.Add(river.halfWidth);
                hash.Bytes(river.path.data(), river.path.size() * sizeof(glm::vec3));
            }
            snapshot.m_stillSignature = hash.value == 0 ? 1 : hash.value;
        }
        return snapshot;
    }

    bool WaterSurfaceSnapshot::OceanCovers(const Ocean &ocean, glm::vec2 local)
    {
        // Areas carve holes in the sea, or with Invert Area Mask are the only water (lakes). Without areas the mask does
        // nothing and the whole plane is water, as the renderer draws it (Ocean.slang waterAt), inverted or not.
        if (ocean.areas.empty())
            return true;
        bool insideArea = false;
        for (const auto &area : ocean.areas)
            insideArea = insideArea || PointInPolygon(area, local);
        return ocean.invertAreaMask ? insideArea : !insideArea;
    }

    std::optional<WaterSurfaceSample> WaterSurfaceSnapshot::SampleOcean(const Ocean &ocean, glm::vec3 worldPosition, WaterSampleMode mode) const
    {
        const glm::vec3 local = glm::vec3(ocean.worldToLocal * glm::vec4(worldPosition, 1.0f));
        if (!OceanCovers(ocean, {local.x, local.z}))
            return std::nullopt;
        WaterSurfaceSample sample;
        sample.kind = WaterBodyKind::Ocean;
        sample.body = ocean.component;
        sample.entity = ocean.entity;
        sample.stillHeight = glm::vec3(ocean.localToWorld * glm::vec4(local.x, 0.0f, local.z, 1.0f)).y;
        const glm::mat3 normalMatrix = glm::transpose(glm::mat3(ocean.worldToLocal));
        if (mode == WaterSampleMode::Animated)
        {
            const auto wave = SampleOceanSurface(ocean.spectrum, {local.x, local.z});
            sample.height = glm::vec3(ocean.localToWorld * glm::vec4(local.x, wave.height, local.z, 1.0f)).y;
            sample.normal = glm::normalize(normalMatrix * wave.normal);
        }
        else
        {
            sample.height = sample.stillHeight;
            sample.normal = glm::normalize(normalMatrix * glm::vec3(0.0f, 1.0f, 0.0f));
        }
        return sample;
    }

    std::optional<WaterSurfaceSample> WaterSurfaceSnapshot::Sample(glm::vec3 worldPosition, WaterSampleMode mode) const
    {
        std::optional<WaterSurfaceSample> best;
        const auto consider = [&](std::optional<WaterSurfaceSample> sample) {
            if (sample && (!best || sample->height > best->height))
                best = sample;
        };
        for (const auto &ocean : m_oceans)
            consider(SampleOcean(ocean, worldPosition, mode));
        const glm::vec2 point(worldPosition.x, worldPosition.z);
        for (const auto &river : m_rivers)
        {
            if (point.x < river.boundsMin.x || point.y < river.boundsMin.y || point.x > river.boundsMax.x || point.y > river.boundsMax.y)
                continue;
            auto sample = SampleRiverPath(river.path, river.halfWidth, river.flowSpeed, worldPosition);
            if (sample)
            {
                sample->body = river.component;
                sample->entity = river.entity;
            }
            consider(sample);
        }
        return best;
    }

    std::optional<float> WaterSurfaceSnapshot::StillDepth(glm::vec2 worldXZ, float groundHeight) const
    {
        const auto sample = Sample({worldXZ.x, groundHeight, worldXZ.y}, WaterSampleMode::Still);
        if (!sample)
            return std::nullopt;
        return sample->stillHeight - groundHeight;
    }

    std::vector<std::uint32_t> WaterSurfaceSnapshot::BodyEntities() const
    {
        std::vector<std::uint32_t> entities;
        entities.reserve(BodyCount());
        for (const auto &ocean : m_oceans)
            entities.push_back(ocean.entity);
        for (const auto &river : m_rivers)
            entities.push_back(river.entity);
        return entities;
    }

    std::optional<glm::vec3> WaterSurfaceSnapshot::NearestSurfacePoint(std::uint32_t waterEntity, glm::vec3 worldPosition) const
    {
        for (const auto &ocean : m_oceans)
        {
            if (ocean.entity != waterEntity)
                continue;
            const glm::vec3 local = glm::vec3(ocean.worldToLocal * glm::vec4(worldPosition, 1.0f));
            glm::vec2 nearest(local.x, local.z);
            if (!OceanCovers(ocean, nearest))
            {
                // Outside the water: the nearest point on the shore of any area (a lake's outline, an island's hole).
                float best = std::numeric_limits<float>::max();
                for (const auto &area : ocean.areas)
                    for (std::size_t i = 0, j = area.size() - 1; i < area.size(); j = i++)
                    {
                        const glm::vec2 candidate = NearestOnSegment({local.x, local.z}, area[j], area[i]);
                        const float distance = glm::distance(candidate, glm::vec2(local.x, local.z));
                        if (distance < best)
                            best = distance, nearest = candidate;
                    }
            }
            return glm::vec3(ocean.localToWorld * glm::vec4(nearest.x, 0.0f, nearest.y, 1.0f));
        }
        for (const auto &river : m_rivers)
        {
            if (river.entity != waterEntity)
                continue;
            const glm::vec2 point(worldPosition.x, worldPosition.z);
            float best = std::numeric_limits<float>::max();
            glm::vec3 result = river.path.front();
            for (std::size_t i = 0; i + 1 < river.path.size(); ++i)
            {
                const glm::vec3 &a = river.path[i], &b = river.path[i + 1];
                const glm::vec2 onCentre = NearestOnSegment(point, {a.x, a.z}, {b.x, b.z});
                const float distance = glm::distance(point, onCentre);
                if (distance >= best)
                    continue;
                best = distance;
                const glm::vec2 edge(b.x - a.x, b.z - a.z);
                const float t = glm::dot(edge, edge) > 0.0f ? glm::length(onCentre - glm::vec2(a.x, a.z)) / glm::length(edge) : 0.0f;
                // Within the water's width the point itself; beyond it, the bank facing the listener.
                const glm::vec2 offset = distance > river.halfWidth ? (point - onCentre) * (river.halfWidth / distance) : point - onCentre;
                result = glm::vec3(onCentre.x + offset.x, a.y + (b.y - a.y) * t, onCentre.y + offset.y);
            }
            return result;
        }
        return std::nullopt;
    }
}
