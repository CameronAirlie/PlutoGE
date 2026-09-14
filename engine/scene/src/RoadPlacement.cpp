#include "PlutoGE/scene/RoadPlacement.h"

#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <cmath>

namespace PlutoGE::scene
{
    // Deterministic local-space poses for an explicit authoring bake. No assets,
    // entities or GPU objects are created by the sampler. Empty means invalid,
    // degenerate or over the 1,024-instance budget; partial output is never returned.
    std::vector<SplineControlPoint> SampleRoadsidePlacements(
        const SplineComponent &road, float spacing, float edgeOffset, bool bothSides)
    {
        if (!std::isfinite(spacing) || spacing < 0.1f || !std::isfinite(edgeOffset) || edgeOffset < 0)
            return {};
        auto samples = road.SampleControlPoints(road.GetSamplesPerSegment());
        if (samples.size() < 2) return {};
        if (road.IsClosed()) samples.push_back(samples.front());
        std::vector<double> distances(samples.size(), 0);
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            const double distance = glm::length(glm::dvec3(samples[i].position) - glm::dvec3(samples[i - 1].position));
            if (!std::isfinite(distance)) return {};
            distances[i] = distances[i - 1] + distance;
        }
        const double length = distances.back();
        if (length < 0.0001) return {};
        // Closed roads exclude the repeated end; open roads include the end only
        // when spacing lands exactly there. Integer iteration avoids accumulated drift.
        const double count = road.IsClosed() ? std::ceil(length / spacing) : std::floor(length / spacing) + 1;
        if (count * (bothSides ? 2 : 1) > 1024) return {};
        std::vector<SplineControlPoint> poses;
        poses.reserve(static_cast<std::size_t>(count) * (bothSides ? 2 : 1));
        std::size_t segment = 1;
        for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index)
        {
            const double distance = index * static_cast<double>(spacing);
            while (segment + 1 < samples.size() && (distances[segment] < distance || distances[segment] == distances[segment - 1])) ++segment;
            const double span = distances[segment] - distances[segment - 1];
            if (span <= 0) return {};
            const float t = static_cast<float>((distance - distances[segment - 1]) / span);
            const auto &a = samples[segment - 1], &b = samples[segment];
            const glm::vec3 forward = glm::normalize(b.position - a.position);
            glm::vec3 right = glm::cross(glm::vec3(0, 1, 0), forward);
            if (glm::dot(right, right) < 0.000001f) right = glm::cross(glm::vec3(0, 0, 1), forward);
            right = glm::normalize(right);
            const glm::vec3 up = glm::cross(forward, right);
            const auto rotation = [](const glm::vec3 &degrees)
            {
                const auto r = glm::radians(degrees);
                return glm::quat_cast(glm::eulerAngleXYZ(r.x, r.y, r.z));
            };
            const auto bank = glm::slerp(rotation(a.rotation), rotation(b.rotation), t);
            const glm::mat3 frame = glm::mat3(right, up, forward) * glm::mat3_cast(bank);
            glm::vec3 euler;
            glm::extractEulerAngleXYZ(glm::mat4(frame), euler.x, euler.y, euler.z);
            const auto center = glm::mix(a.position, b.position, t);
            const float offset = road.GetWidth() * 0.5f + edgeOffset;
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(euler[axis]) || !std::isfinite(center[axis]) || !std::isfinite(frame[0][axis] * offset)) return {};
            poses.push_back({center + frame[0] * offset, glm::degrees(euler)});
            if (bothSides) poses.push_back({center - frame[0] * offset, glm::degrees(euler)});
        }
        return poses;
    }

    Entity *BakeRoadsidePrefabs(Entity &owner, const std::string &prefab,
        float spacing, float edgeOffset, bool bothSides, std::string &error)
    {
        error.clear();
        auto *scene = owner.GetScene();
        auto *road = owner.GetComponent<SplineComponent>();
        if (!scene || scene->IsRuntimeStarted() || !road)
        { error = "Select an authoring road in a scene."; return nullptr; }
        const auto poses = SampleRoadsidePlacements(*road, spacing, edgeOffset, bothSides);
        if (poses.empty())
        { error = "Invalid road/spacing, or placement exceeds 1,024 instances."; return nullptr; }
        const auto preload = Prefab::Preload(prefab);
        if (!preload.ready) { error = preload.error; return nullptr; }
        auto *group = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Roadside placement"}), &owner);
        for (const auto &pose : poses)
        {
            auto *instance = Prefab::Instantiate(*scene, prefab, group, &error);
            if (!instance) { scene->RemoveEntity(group); return nullptr; }
            instance->SetPosition(pose.position);
            instance->SetRotation(pose.rotation);
            instance->AddPrefabOverride("Position");
            instance->AddPrefabOverride("Rotation");
        }
        return group;
    }
}
