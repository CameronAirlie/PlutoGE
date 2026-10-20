#include "PlutoGE/platform/LoadingWork.h"
#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/core/CpuTrace.h"

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

        // Rays restart this far under each surface they hit to look for the next layer down. Rays starting
        // inside a collider do not report it, so the solid that was hit is skipped.
        constexpr float LayerRayRestart = 0.01f;
    }

    void NavigationSystem::Clear()
    {
        m_cells.clear();
        m_columnStart.clear();
        m_debugPoints.clear();
        m_agentWalkabilityCache.clear();
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

        const auto raycast = [&](const glm::vec3 &origin, const glm::vec3 &direction, float length, PhysicsRaycastHit &hit) {
            return scene.Raycast(origin, direction, length, hit, 0, settings.staticObjectsOnly, settings.includeTags, settings.excludeTags);
        };
        const float minimumNormalY = std::cos(glm::radians(std::clamp(settings.maxSlopeDegrees, 0.0f, 89.0f)));
        const float halfCell = m_settings.cellSize * 0.5f;
        const glm::vec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        m_columnStart.assign(static_cast<size_t>(m_width) * m_depth + 1, 0);
        for (int z = 0; z < m_depth; ++z)
        {
            platform::LoadingWork::Checkpoint();
            for (int x = 0; x < m_width; ++x)
            {
                const int column = z * m_width + x;
                m_columnStart[column] = static_cast<int>(m_cells.size());
                const float columnX = settings.boundsMin.x + (x + 0.5f) * m_settings.cellSize;
                const float columnZ = settings.boundsMin.z + (z + 0.5f) * m_settings.cellSize;
                // Walk down the column: every surface hit is a candidate layer; the next ray starts just under it.
                float rayTop = settings.boundsMax.y;
                float surfaceAbove = std::numeric_limits<float>::infinity();
                for (int hits = 0; hits < MaxLayersPerColumn * 2 && rayTop > settings.boundsMin.y; ++hits)
                {
                    PhysicsRaycastHit floorHit;
                    if (!raycast({columnX, rayTop, columnZ}, {0.0f, -1.0f, 0.0f}, rayTop - settings.boundsMin.y, floorHit))
                        break;
                    rayTop = floorHit.point.y - LayerRayRestart;
                    const bool firstSurface = !std::isfinite(surfaceAbove);
                    const float above = surfaceAbove;
                    surfaceAbove = floorHit.point.y;
                    if (floorHit.normal.y < minimumNormalY)
                        continue;

                    Cell cell;
                    cell.column = column;
                    cell.walkable = true;
                    cell.height = floorHit.point.y;
                    const glm::vec3 clearanceOrigin = floorHit.point + glm::vec3(0.0f, 0.05f, 0.0f);
                    PhysicsRaycastHit ceilingHit;
                    if (firstSurface)
                    {
                        cell.clearance = std::max(0.0f, settings.boundsMax.y - cell.height);
                        if (raycast(clearanceOrigin, {0.0f, 1.0f, 0.0f}, cell.clearance, ceilingHit))
                            cell.clearance = std::max(0.0f, ceilingHit.distance + 0.05f);
                    }
                    else
                    {
                        // A lower surface is open only if something stops a ray going up before the surface above:
                        // the underside of the floor above, or anything in between. Under a solid (the ground
                        // beneath a thick wall) the upward ray starts inside it and finds nothing.
                        const float gap = above - clearanceOrigin.y;
                        if (gap <= 0.0f || !raycast(clearanceOrigin, {0.0f, 1.0f, 0.0f}, gap + LayerRayRestart, ceilingHit))
                            continue;
                        cell.clearance = std::max(0.0f, ceilingHit.distance + 0.05f);
                    }

                    // Vertical samples miss thin partitions between cell centres and
                    // colliders containing the top of the bake. Probe the cell's
                    // footprint above step height, in both directions so a probe
                    // starting inside a solid still has an outside-in counterpart.
                    const glm::vec3 probeCenter = floorHit.point +
                        glm::vec3(0.0f, std::max(0.0f, settings.maxStepHeight) + 0.05f, 0.0f);
                    for (const auto &direction : directions)
                    {
                        PhysicsRaycastHit obstacleHit;
                        if (raycast(probeCenter - direction * halfCell, direction, m_settings.cellSize, obstacleHit) &&
                            std::abs(obstacleHit.normal.y) < minimumNormalY)
                        {
                            cell.walkable = false;
                            break;
                        }
                    }
                    m_cells.push_back(cell);
                    if (static_cast<int>(m_cells.size()) - m_columnStart[column] >= MaxLayersPerColumn)
                        break;
                }
            }
        }
        m_columnStart.back() = static_cast<int>(m_cells.size());

        for (int index = 0; index < static_cast<int>(m_cells.size()); ++index)
            if (m_cells[index].walkable)
                m_debugPoints.push_back(CellPosition(index));
        return !m_debugPoints.empty();
    }

    glm::vec3 NavigationSystem::CellPosition(int index) const
    {
        const int x = m_cells[index].column % m_width;
        const int z = m_cells[index].column / m_width;
        return {m_settings.boundsMin.x + (x + 0.5f) * m_settings.cellSize,
                m_cells[index].height,
                m_settings.boundsMin.z + (z + 0.5f) * m_settings.cellSize};
    }

    int NavigationSystem::FindLayer(int column, float height, float maxDelta) const
    {
        if (column < 0 || column + 1 >= static_cast<int>(m_columnStart.size()))
            return -1;
        // Two layers in reach of one height are a floor and the crawlspace under it (the ground beneath the foot
        // of a ramp): take the one with the most headroom, then the nearest.
        int best = -1;
        for (int index = m_columnStart[column]; index < m_columnStart[column + 1]; ++index)
        {
            const float delta = std::abs(m_cells[index].height - height);
            if (delta > maxDelta)
                continue;
            if (best < 0 || m_cells[index].clearance > m_cells[best].clearance + 0.001f ||
                (std::abs(m_cells[index].clearance - m_cells[best].clearance) <= 0.001f &&
                 delta < std::abs(m_cells[best].height - height)))
                best = index;
        }
        return best;
    }

    bool NavigationSystem::ComputeCellWalkableForAgent(int index, float agentRadius, float agentHeight) const
    {
        if (index < 0 || index >= static_cast<int>(m_cells.size()) || !m_cells[index].walkable)
            return false;
        agentRadius = std::max(0.0f, agentRadius);
        agentHeight = std::max(0.0f, agentHeight);
        const int centerX = m_cells[index].column % m_width;
        const int centerZ = m_cells[index].column / m_width;
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
                // The footprint stands on the layer of each column nearest this floor; none in reach is a drop.
                const int layer = FindLayer(z * m_width + x, centerHeight, MaxTraversableHeightDelta(m_settings, distance));
                if (layer < 0 || !m_cells[layer].walkable || m_cells[layer].clearance + 0.001f < agentHeight)
                    return false;
            }
        }
        return true;
    }

    const std::vector<std::uint8_t> &NavigationSystem::GetAgentWalkability(float agentRadius, float agentHeight) const
    {
        const auto radiusKey = static_cast<std::uint32_t>(std::lround(std::max(0.0f, agentRadius) * 1000.0f));
        const auto heightKey = static_cast<std::uint32_t>(std::lround(std::max(0.0f, agentHeight) * 1000.0f));
        const std::uint64_t key = (static_cast<std::uint64_t>(radiusKey) << 32u) | heightKey;
        if (const auto found = m_agentWalkabilityCache.find(key); found != m_agentWalkabilityCache.end())
            return found->second;

        std::vector<std::uint8_t> mask(m_cells.size(), 0);
        for (int index = 0; index < static_cast<int>(m_cells.size()); ++index)
            mask[index] = ComputeCellWalkableForAgent(index, agentRadius, agentHeight) ? 1 : 0;
        return m_agentWalkabilityCache.emplace(key, std::move(mask)).first->second;
    }

    bool NavigationSystem::IsCellWalkableForAgent(int index, float agentRadius, float agentHeight) const
    {
        if (index < 0 || index >= static_cast<int>(m_cells.size()))
            return false;
        return GetAgentWalkability(agentRadius, agentHeight)[index] != 0;
    }

    int NavigationSystem::FindNearestCell(const glm::vec3 &point, float agentRadius, float agentHeight) const
    {
        if (m_cells.empty())
            return -1;
        const int centerX = static_cast<int>((point.x - m_settings.boundsMin.x) / m_settings.cellSize);
        const int centerZ = static_cast<int>((point.z - m_settings.boundsMin.z) / m_settings.cellSize);
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (int radius = 0; radius <= 8; ++radius)
        {
            // Layers make the nearest cell three-dimensional: a floor straight above or below may be farther than
            // one a ring out on the point's own floor. Stop once a ring cannot hold anything closer.
            const float ringDistance = std::max(0, radius - 1) * m_settings.cellSize;
            if (best >= 0 && ringDistance * ringDistance > bestDistance)
                break;
            for (int z = centerZ - radius; z <= centerZ + radius; ++z)
            {
                for (int x = centerX - radius; x <= centerX + radius; ++x)
                {
                    if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
                        continue;
                    if (std::max(std::abs(x - centerX), std::abs(z - centerZ)) != radius)
                        continue;
                    const int column = z * m_width + x;
                    for (int index = m_columnStart[column]; index < m_columnStart[column + 1]; ++index)
                    {
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
        const float stepDelta = m_settings.maxStepHeight + 0.001f;
        const float sideDelta = MaxTraversableHeightDelta(m_settings, m_settings.cellSize);
        float previousHeight = start.y;
        int previousX = -1, previousZ = -1;
        const auto &walkability = GetAgentWalkability(agentRadius, agentHeight);
        const auto walkableNear = [&](int column, float height, float maxDelta) {
            const int layer = FindLayer(column, height, maxDelta);
            return layer >= 0 && walkability[layer] != 0;
        };
        for (int sample = 0; sample <= samples; ++sample)
        {
            const float t = static_cast<float>(sample) / samples;
            const glm::vec3 point = glm::mix(start, end, t);
            const int x = static_cast<int>(std::floor((point.x - m_settings.boundsMin.x) / m_settings.cellSize));
            const int z = static_cast<int>(std::floor((point.z - m_settings.boundsMin.z) / m_settings.cellSize));
            if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
                return false;
            // Follow the floor the segment is on: the layer within a step of the last sample.
            const int index = FindLayer(z * m_width + x, previousHeight, stepDelta);
            if (index < 0 || !walkability[index])
                return false;
            // Match the A* diagonal rule: never shortcut through a blocked corner.
            if (previousX >= 0 && previousX != x && previousZ != z &&
                (!walkableNear(previousZ * m_width + x, previousHeight, sideDelta) ||
                 !walkableNear(z * m_width + previousX, previousHeight, sideDelta)))
                return false;
            previousX = x;
            previousZ = z;
            previousHeight = m_cells[index].height;
        }
        // Columns hold several floors: arriving under (or over) the end on another floor is not reaching it.
        return std::abs(previousHeight - end.y) <= stepDelta;
    }

    NavigationPath NavigationSystem::FindPath(const glm::vec3 &startPoint, const glm::vec3 &endPoint,
                                               float agentRadius, float agentHeight) const
    {
        core::CpuScope pathScope("Navigation path search", core::CpuCategory::Scripts);
        NavigationPath output;
        const int start = FindNearestCell(startPoint, agentRadius, agentHeight);
        const int goal = FindNearestCell(endPoint, agentRadius, agentHeight);
        if (start < 0 || goal < 0)
            return output;

        // Use the same clearance/height checks as path simplification before
        // allocating an A* frontier for an unobstructed route.
        auto surfaceStart = startPoint;
        auto surfaceEnd = endPoint;
        surfaceStart.y = m_cells[start].height;
        surfaceEnd.y = m_cells[goal].height;
        // Nearest-cell projection may cross a wall. Never replace a safe grid
        // endpoint with an unchecked position inside an obstacle or outside the bake.
        if (!IsSegmentWalkable(surfaceStart, CellPosition(start), agentRadius, agentHeight))
            return output;
        const bool retainEnd = IsSegmentWalkable(CellPosition(goal), surfaceEnd, agentRadius, agentHeight);
        const glm::vec3 resolvedEnd = retainEnd ? endPoint : CellPosition(goal);
        surfaceEnd = retainEnd ? surfaceEnd : CellPosition(goal);
        if (IsSegmentWalkable(surfaceStart, surfaceEnd, agentRadius, agentHeight))
        {
            output.points = {startPoint, resolvedEnd};
            output.complete = true;
            return output;
        }
        const auto &walkability = GetAgentWalkability(agentRadius, agentHeight);
        struct OpenNode { int index; float score; bool operator<(const OpenNode &other) const { return score > other.score; } };
        std::priority_queue<OpenNode> open;
        std::vector<float> costs(m_cells.size(), std::numeric_limits<float>::max());
        std::vector<int> parents(m_cells.size(), -1);
        std::vector<std::uint8_t> closed(m_cells.size(), 0);
        costs[start] = 0.0f;
        open.push({start, 0.0f});
        constexpr int directions[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
        const float goalHeight = m_cells[goal].height;
        const int goalX = m_cells[goal].column % m_width;
        const int goalZ = m_cells[goal].column / m_width;

        while (!open.empty())
        {
            const int current = open.top().index;
            open.pop();
            if (closed[current])
                continue;
            closed[current] = 1;
            if (current == goal)
                break;
            const int x = m_cells[current].column % m_width;
            const int z = m_cells[current].column / m_width;
            const float height = m_cells[current].height;
            for (const auto &direction : directions)
            {
                const int nextX = x + direction[0];
                const int nextZ = z + direction[1];
                if (nextX < 0 || nextZ < 0 || nextX >= m_width || nextZ >= m_depth)
                    continue;
                const bool diagonal = direction[0] != 0 && direction[1] != 0;
                const float linkDistance = m_settings.cellSize * (diagonal ? 1.41421356f : 1.0f);
                const float maxDelta = MaxTraversableHeightDelta(m_settings, linkDistance);
                // Step onto the neighbouring column's layer nearest this floor (ramps and stairs link floors).
                const int next = FindLayer(nextZ * m_width + nextX, height, maxDelta);
                if (next < 0 || !walkability[next])
                    continue;
                if (diagonal)
                {
                    const int sideX = FindLayer(z * m_width + nextX, height, maxDelta);
                    const int sideZ = FindLayer(nextZ * m_width + x, height, maxDelta);
                    if (sideX < 0 || sideZ < 0 || !walkability[sideX] || !walkability[sideZ])
                        continue;
                }
                const float climb = std::abs(m_cells[next].height - height) / m_settings.cellSize;
                const float newCost = costs[current] + std::sqrt((diagonal ? 2.0f : 1.0f) + climb * climb);
                if (newCost >= costs[next])
                    continue;
                costs[next] = newCost;
                parents[next] = current;
                const float rise = (goalHeight - m_cells[next].height) / m_settings.cellSize;
                open.push({next, newCost + std::sqrt(static_cast<float>((goalX - nextX) * (goalX - nextX) + (goalZ - nextZ) * (goalZ - nextZ)) + rise * rise)});
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
            // Keep the checked cell-center connections. Replacing the centers
            // after simplification can move a long segment through a corner.
            if (surfaceStart.x == output.points.front().x && surfaceStart.z == output.points.front().z)
                output.points.front() = startPoint;
            else
                output.points.insert(output.points.begin(), startPoint);
            if (surfaceEnd.x == output.points.back().x && surfaceEnd.z == output.points.back().z)
                output.points.back() = resolvedEnd;
            else
                output.points.push_back(resolvedEnd);
        }
        output.complete = true;
        return output;
    }
}
