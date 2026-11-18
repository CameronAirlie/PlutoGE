#include "PlutoGE/render/LocalLightSelection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace PlutoGE::render
{
    namespace
    {
        using Type = LocalLightCandidate::Type;

        // The view's side planes (left, right, bottom, top) as (normal, distance), inside where dot(n, p) + d >= 0.
        // Near and far planes are left out: their form depends on the clip depth convention, and lights behind the
        // camera are already far down the ranking by distance.
        std::array<glm::vec4, 4> SidePlanes(const glm::mat4 &viewProjection)
        {
            const glm::vec4 row0{viewProjection[0][0], viewProjection[1][0], viewProjection[2][0], viewProjection[3][0]};
            const glm::vec4 row1{viewProjection[0][1], viewProjection[1][1], viewProjection[2][1], viewProjection[3][1]};
            const glm::vec4 row3{viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]};
            std::array<glm::vec4, 4> planes{row3 + row0, row3 - row0, row3 + row1, row3 - row1};
            for (auto &plane : planes)
            {
                const float length = glm::length(glm::vec3(plane));
                plane = length > 1.e-12f && std::isfinite(length) ? plane / length : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            }
            return planes;
        }

        bool SphereInView(const std::array<glm::vec4, 4> &planes, const glm::vec3 &centre, float radius)
        {
            return std::ranges::all_of(planes, [&](const glm::vec4 &plane)
                                       { return glm::dot(glm::vec3(plane), centre) + plane.w >= -radius; });
        }

        float Importance(const LocalLightCandidate &light, const glm::vec3 &camera, const std::array<glm::vec4, 4> &planes)
        {
            if (!(light.intensity > 0.0f) || !(light.range > 0.0f))
                return 0.0f;
            const glm::vec3 offset = light.position - camera;
            const float score = light.intensity / (1.0f + glm::dot(offset, offset));
            if (!std::isfinite(score))
                return 0.0f;
            return SphereInView(planes, light.position, light.range) ? score
                                                                      : score * LocalLightSelector::OffscreenWeight;
        }

        // `chosen` (indices into the type's lights) in last frame's order where they were remembered, then the new
        // ones by score.
        std::vector<std::size_t> Stable(std::vector<std::size_t> chosen, const std::vector<std::uintptr_t> &previous,
                                        std::span<const LocalLightCandidate> candidates,
                                        const std::vector<std::size_t> &lights, const std::vector<float> &scores)
        {
            std::unordered_map<std::uintptr_t, std::size_t> rank;
            for (std::size_t i = 0; i < previous.size(); ++i)
                rank.emplace(previous[i], i);
            std::stable_sort(chosen.begin(), chosen.end(), [&](std::size_t a, std::size_t b)
            {
                const auto rankA = rank.find(candidates[lights[a]].key), rankB = rank.find(candidates[lights[b]].key);
                const bool knownA = rankA != rank.end(), knownB = rankB != rank.end();
                if (knownA != knownB)
                    return knownA;
                if (knownA)
                    return rankA->second < rankB->second;
                return scores[a] > scores[b];
            });
            return chosen;
        }
    }

    std::vector<std::size_t> LocalLightSelector::Order(std::span<const LocalLightCandidate> candidates,
                                                       const glm::vec3 &camera, const glm::mat4 &viewProjection,
                                                       const LocalLightSelectionBudget &budget)
    {
        const auto planes = SidePlanes(viewProjection);
        std::vector<std::size_t> order;
        order.reserve(candidates.size());
        std::size_t shadedSoFar = 0;
        for (const Type type : {Type::Point, Type::Spot, Type::Rect})
        {
            // This type's lights (indices into candidates) and their scores.
            std::vector<std::size_t> lights;
            for (std::size_t index = 0; index < candidates.size(); ++index)
                if (candidates[index].type == type)
                    lights.push_back(index);
            std::vector<float> scores(lights.size());
            for (std::size_t i = 0; i < lights.size(); ++i)
                scores[i] = Importance(candidates[lights[i]], camera, planes);

            auto &memory = m_memory[static_cast<std::uint8_t>(type)];
            const std::unordered_set<std::uintptr_t> wasShadowed(memory.shadowed.begin(), memory.shadowed.end());
            const std::unordered_set<std::uintptr_t> wasShaded(memory.shaded.begin(), memory.shaded.end());
            const std::size_t typeCapacity = type == Type::Point ? budget.points : type == Type::Spot ? budget.spots
                                                                                                     : budget.total;
            const std::size_t capacity = std::min(typeCapacity, budget.total - std::min(budget.total, shadedSoFar));
            const std::size_t shadowCapacity = type == Type::Point ? budget.pointShadows
                                             : type == Type::Spot ? budget.spotShadows : 0;

            const auto ranked = [&](float bonus, const std::unordered_set<std::uintptr_t> &retained, auto &&eligible)
            {
                std::vector<std::size_t> result;
                for (std::size_t i = 0; i < lights.size(); ++i)
                    if (eligible(i) && scores[i] > 0.0f)
                        result.push_back(i);
                std::stable_sort(result.begin(), result.end(), [&](std::size_t a, std::size_t b)
                {
                    const float scoreA = scores[a] * (retained.contains(candidates[lights[a]].key) ? bonus : 1.0f);
                    const float scoreB = scores[b] * (retained.contains(candidates[lights[b]].key) ? bonus : 1.0f);
                    return scoreA > scoreB;
                });
                return result;
            };

            // Shadow maps first: the most important casters, those that had one keeping it unless clearly beaten.
            auto shadowed = ranked(ShadowRetainedBonus, wasShadowed,
                                   [&](std::size_t i) { return candidates[lights[i]].castsShadows; });
            shadowed.resize(std::min({shadowed.size(), shadowCapacity, capacity}));
            std::vector<bool> taken(lights.size(), false);
            for (const auto i : shadowed)
                taken[i] = true;
            // Then the other shaded lights.
            auto shaded = ranked(RetainedBonus, wasShaded, [&](std::size_t i) { return !taken[i]; });
            shaded.resize(std::min(shaded.size(), capacity - shadowed.size()));
            for (const auto i : shaded)
                taken[i] = true;

            shadowed = Stable(std::move(shadowed), memory.shadowed, candidates, lights, scores);
            shaded = Stable(std::move(shaded), memory.shaded, candidates, lights, scores);
            // The rest in key order, so their sequence does not follow the camera.
            std::vector<std::size_t> rest;
            for (std::size_t i = 0; i < lights.size(); ++i)
                if (!taken[i])
                    rest.push_back(i);
            std::stable_sort(rest.begin(), rest.end(), [&](std::size_t a, std::size_t b)
                             { return candidates[lights[a]].key < candidates[lights[b]].key; });

            memory.shadowed.clear();
            memory.shaded.clear();
            for (const auto i : shadowed)
                memory.shadowed.push_back(candidates[lights[i]].key);
            for (const auto *list : {&shadowed, &shaded})
                for (const auto i : *list)
                    memory.shaded.push_back(candidates[lights[i]].key);
            for (const auto *list : {&shadowed, &shaded, &rest})
                for (const auto i : *list)
                    order.push_back(lights[i]);
            shadedSoFar += shadowed.size() + shaded.size();
        }
        return order;
    }

    void LocalLightSelector::Reset() noexcept
    {
        m_memory.clear();
    }
}
