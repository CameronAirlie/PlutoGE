#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace PlutoGE::scene
{
    namespace
    {
        float MaxTraversableHeightDelta(const NavigationBakeSettings &settings, float horizontalDistance)
        {
            const float slope = std::tan(glm::radians(std::clamp(settings.maxSlopeDegrees, 0.0f, 89.0f)));
            return std::max(std::max(0.0f, settings.maxStepHeight), slope * std::max(0.0f, horizontalDistance)) + 0.001f;
        }
    }

    void NavigationSystem::Clear()
    {
        m_cells.clear();
        m_debugPoints.clear();
        m_width = m_depth = 0;
    }

    bool NavigationSystem::Bake(const Scene &scene, const NavigationBakeSettings &settings)
    {
        Clear();
        m_settings = settings;
        m_settings.cellSize = std::max(0.1f, settings.cellSize);
        m_width = std::max(1, static_cast<int>(std::ceil((settings.boundsMax.x - settings.boundsMin.x) / m_settings.cellSize)));
        m_depth = std::max(1, static_cast<int>(std::ceil((settings.boundsMax.z - settings.boundsMin.z) / m_settings.cellSize)));
        if (static_cast<uint64_t>(m_width) * m_depth > 4000000)
            return false;

        m_cells.resize(static_cast<size_t>(m_width) * m_depth);
        const float rayLength = std::max(0.1f, settings.boundsMax.y - settings.boundsMin.y);
        const float minimumNormalY = std::cos(glm::radians(std::clamp(settings.maxSlopeDegrees, 0.0f, 89.0f)));
        for (int z = 0; z < m_depth; ++z)
        {
            for (int x = 0; x < m_width; ++x)
            {
                const glm::vec3 origin{settings.boundsMin.x + (x + 0.5f) * m_settings.cellSize,
                                       settings.boundsMax.y,
                                       settings.boundsMin.z + (z + 0.5f) * m_settings.cellSize};
                PhysicsRaycastHit floorHit;
                auto &cell = m_cells[static_cast<size_t>(z) * m_width + x];
                if (!scene.Raycast(origin, {0.0f, -1.0f, 0.0f}, rayLength, floorHit) || floorHit.normal.y < minimumNormalY)
                    continue;

                cell.walkable = true;
                cell.height = floorHit.point.y;
                cell.clearance = std::max(0.0f, settings.boundsMax.y - cell.height);
                PhysicsRaycastHit ceilingHit;
                const glm::vec3 clearanceOrigin = floorHit.point + glm::vec3(0.0f, 0.05f, 0.0f);
                if (scene.Raycast(clearanceOrigin, {0.0f, 1.0f, 0.0f}, cell.clearance, ceilingHit))
                    cell.clearance = std::max(0.0f, ceilingHit.distance + 0.05f);
            }
        }

        for (int index = 0; index < static_cast<int>(m_cells.size()); ++index)
            if (m_cells[index].walkable)
                m_debugPoints.push_back(CellPosition(index));
        return !m_debugPoints.empty();
    }

    glm::vec3 NavigationSystem::CellPosition(int index) const
    {
        const int x = index % m_width;
        const int z = index / m_width;
        return {m_settings.boundsMin.x + (x + 0.5f) * m_settings.cellSize,
                m_cells[index].height,
                m_settings.boundsMin.z + (z + 0.5f) * m_settings.cellSize};
    }

    bool NavigationSystem::IsCellWalkableForAgent(int index, float agentRadius, float agentHeight) const
    {
        if (index < 0 || index >= static_cast<int>(m_cells.size()) || !m_cells[index].walkable)
            return false;
        agentRadius = std::max(0.0f, agentRadius);
        agentHeight = std::max(0.0f, agentHeight);
        const int centerX = index % m_width;
        const int centerZ = index / m_width;
        const int radiusInCells = static_cast<int>(std::ceil(agentRadius / m_settings.cellSize));
        const float centerHeight = m_cells[index].height;

        for (int dz = -radiusInCells; dz <= radiusInCells; ++dz)
        {
            for (int dx = -radiusInCells; dx <= radiusInCells; ++dx)
            {
                const float distance = m_settings.cellSize * std::hypot(static_cast<float>(dx), static_cast<float>(dz));
                if (distance > agentRadius + m_settings.cellSize * 0.5f)
                    continue;
                const int x = centerX + dx;
                const int z = centerZ + dz;
                if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
                    return false;
                const auto &cell = m_cells[static_cast<size_t>(z) * m_width + x];
                if (!cell.walkable || cell.clearance + 0.001f < agentHeight ||
                    std::abs(cell.height - centerHeight) > MaxTraversableHeightDelta(m_settings, distance))
                    return false;
            }
        }
        return true;
    }

    int NavigationSystem::FindNearestCell(const glm::vec3 &point, float agentRadius, float agentHeight) const
    {
        if (m_cells.empty())
            return -1;
        const int centerX = static_cast<int>((point.x - m_settings.boundsMin.x) / m_settings.cellSize);
        const int centerZ = static_cast<int>((point.z - m_settings.boundsMin.z) / m_settings.cellSize);
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (int radius = 0; radius <= 8 && best < 0; ++radius)
        {
            for (int z = centerZ - radius; z <= centerZ + radius; ++z)
            {
                for (int x = centerX - radius; x <= centerX + radius; ++x)
                {
                    if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
                        continue;
                    const int index = z * m_width + x;
                    if (!IsCellWalkableForAgent(index, agentRadius, agentHeight))
                        continue;
                    const glm::vec3 delta = CellPosition(index) - point;
                    const float distance = glm::dot(delta, delta);
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = index;
                    }
                }
            }
        }
        return best;
    }

    bool NavigationSystem::ProjectPoint(const glm::vec3 &point, glm::vec3 &projected, float agentRadius, float agentHeight) const
    {
        const int index = FindNearestCell(point, agentRadius, agentHeight);
        if (index < 0)
            return false;
        projected = CellPosition(index);
        return true;
    }

    bool NavigationSystem::IsSegmentWalkable(const glm::vec3 &start, const glm::vec3 &end, float agentRadius, float agentHeight) const
    {
        glm::vec3 horizontalDelta = end - start;
        horizontalDelta.y = 0.0f;
        const float distance = glm::length(horizontalDelta);
        const int samples = std::max(1, static_cast<int>(std::ceil(distance / (m_settings.cellSize * 0.25f))));
        float previousHeight = start.y;
        for (int sample = 0; sample <= samples; ++sample)
        {
            const float t = static_cast<float>(sample) / samples;
            const glm::vec3 point = glm::mix(start, end, t);
            const int x = static_cast<int>(std::floor((point.x - m_settings.boundsMin.x) / m_settings.cellSize));
            const int z = static_cast<int>(std::floor((point.z - m_settings.boundsMin.z) / m_settings.cellSize));
            if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
                return false;
            const int index = z * m_width + x;
            const auto &cell = m_cells[index];
            if (!IsCellWalkableForAgent(index, agentRadius, agentHeight) ||
                std::abs(cell.height - previousHeight) > m_settings.maxStepHeight + 0.001f)
                return false;
            previousHeight = cell.height;
        }
        return true;
    }

    NavigationPath NavigationSystem::FindPath(const glm::vec3 &startPoint, const glm::vec3 &endPoint,
                                               float agentRadius, float agentHeight) const
    {
        NavigationPath output;
        const int start = FindNearestCell(startPoint, agentRadius, agentHeight);
        const int goal = FindNearestCell(endPoint, agentRadius, agentHeight);
        if (start < 0 || goal < 0)
            return output;

        struct OpenNode { int index; float score; bool operator<(const OpenNode &other) const { return score > other.score; } };
        std::priority_queue<OpenNode> open;
        std::vector<float> costs(m_cells.size(), std::numeric_limits<float>::max());
        std::vector<int> parents(m_cells.size(), -1);
        costs[start] = 0.0f;
        open.push({start, 0.0f});
        constexpr int directions[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};

        while (!open.empty())
        {
            const int current = open.top().index;
            open.pop();
            if (current == goal)
                break;
            const int x = current % m_width;
            const int z = current / m_width;
            for (const auto &direction : directions)
            {
                const int nextX = x + direction[0];
                const int nextZ = z + direction[1];
                if (nextX < 0 || nextZ < 0 || nextX >= m_width || nextZ >= m_depth)
                    continue;
                const int next = nextZ * m_width + nextX;
                const bool diagonal = direction[0] != 0 && direction[1] != 0;
                const float linkDistance = m_settings.cellSize * (diagonal ? 1.41421356f : 1.0f);
                if (!IsCellWalkableForAgent(next, agentRadius, agentHeight) ||
                    std::abs(m_cells[next].height - m_cells[current].height) > MaxTraversableHeightDelta(m_settings, linkDistance))
                    continue;
                if (diagonal)
                {
                    const int sideX = z * m_width + nextX;
                    const int sideZ = nextZ * m_width + x;
                    if (!IsCellWalkableForAgent(sideX, agentRadius, agentHeight) ||
                        !IsCellWalkableForAgent(sideZ, agentRadius, agentHeight))
                        continue;
                }
                const float newCost = costs[current] + (diagonal ? 1.4142f : 1.0f);
                if (newCost >= costs[next])
                    continue;
                costs[next] = newCost;
                parents[next] = current;
                const int goalX = goal % m_width;
                const int goalZ = goal / m_width;
                open.push({next, newCost + std::hypot(static_cast<float>(goalX - nextX), static_cast<float>(goalZ - nextZ))});
            }
        }

        if (goal != start && parents[goal] < 0)
            return output;
        for (int index = goal; index >= 0; index = parents[index])
        {
            output.points.push_back(CellPosition(index));
            if (index == start)
                break;
        }
        std::reverse(output.points.begin(), output.points.end());

        if (output.points.size() > 2)
        {
            std::vector<glm::vec3> simplified{output.points.front()};
            size_t anchor = 0;
            while (anchor + 1 < output.points.size())
            {
                size_t furthest = anchor + 1;
                for (size_t candidate = output.points.size() - 1; candidate > anchor + 1; --candidate)
                {
                    if (IsSegmentWalkable(output.points[anchor], output.points[candidate], agentRadius, agentHeight))
                    {
                        furthest = candidate;
                        break;
                    }
                }
                simplified.push_back(output.points[furthest]);
                anchor = furthest;
            }
            output.points = std::move(simplified);
        }
        if (!output.points.empty())
        {
            output.points.front() = startPoint;
            output.points.back() = endPoint;
        }
        output.complete = true;
        return output;
    }
}
