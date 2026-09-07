#include "PlutoGE/render/DebugDraw.h"
#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    DebugDraw &DebugDraw::Get() { static DebugDraw draw; return draw; }
    bool DebugDraw::Add(DebugDrawCommand command)
    {
        std::lock_guard lock(m_mutex);
        const auto validPoint = [](glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && glm::all(glm::lessThanEqual(glm::abs(v), glm::vec3(1e9f))); };
        const auto validColor = [](glm::vec4 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); };
        if (command.category.empty()) command.category = "Default";
        if (m_commands.size() >= MaxCommands || command.category.size() > 64 || command.text.size() > 256 ||
            command.category.find('\0') != std::string::npos || command.text.find('\0') != std::string::npos ||
            !validPoint(command.start) || !validPoint(command.end) || !validColor(command.color) ||
            !std::isfinite(command.duration) || command.duration < 0 || command.duration > 3600 ||
            (command.primitive != DebugPrimitive::Line && command.primitive != DebugPrimitive::Sphere && command.primitive != DebugPrimitive::Label) ||
            (command.primitive == DebugPrimitive::Sphere && (!std::isfinite(command.radius) || command.radius <= 0 || command.radius > 1e6f)) ||
            (!m_categories.contains(command.category) && m_categories.size() >= MaxCategories))
        { ++m_dropped; return false; }
        command.color = glm::clamp(command.color, glm::vec4(0), glm::vec4(1));
        m_categories.try_emplace(command.category, true);
        m_commands.push_back(std::move(command));
        return true;
    }
    std::vector<DebugDrawCommand> DebugDraw::Snapshot() const
    {
        std::lock_guard lock(m_mutex);
        std::vector<DebugDrawCommand> result;
        if (m_enabled)
            for (const auto &command : m_commands)
                if (m_categories.at(command.category)) result.push_back(command);
        return result;
    }
    void DebugDraw::Advance(float delta)
    {
        if (!std::isfinite(delta) || delta <= 0) return;
        std::lock_guard lock(m_mutex);
        std::erase_if(m_commands, [delta](auto &command) { command.duration -= delta; return command.duration <= 0; });
    }
    void DebugDraw::Clear()
    {
        std::lock_guard lock(m_mutex);
        m_commands.clear(); m_categories.clear(); m_dropped = 0;
    }
    void DebugDraw::SetEnabled(bool enabled) { std::lock_guard lock(m_mutex); m_enabled = enabled; }
    bool DebugDraw::IsEnabled() const { std::lock_guard lock(m_mutex); return m_enabled; }
    void DebugDraw::SetCategoryVisible(const std::string &category, bool visible)
    {
        std::lock_guard lock(m_mutex);
        const auto found = m_categories.find(category);
        if (found != m_categories.end()) found->second = visible;
    }
    std::map<std::string, bool> DebugDraw::Categories() const { std::lock_guard lock(m_mutex); return m_categories; }
    std::size_t DebugDraw::Dropped() const { std::lock_guard lock(m_mutex); return m_dropped; }
}
