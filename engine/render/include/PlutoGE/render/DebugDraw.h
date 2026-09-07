#pragma once
#include <glm/glm.hpp>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    enum class DebugPrimitive { Line, Sphere, Label };
    struct DebugDrawCommand
    {
        DebugPrimitive primitive = DebugPrimitive::Line;
        glm::vec3 start{0}, end{0};
        glm::vec4 color{1};
        float radius = 1;
        float duration = 0;
        std::string category = "Default";
        std::string text;
    };
    // Owns values only. Producers and consumers may use different threads.
    class DebugDraw
    {
    public:
        static constexpr std::size_t MaxCommands = 4096, MaxCategories = 64;
        static DebugDraw &Get();
        bool Add(DebugDrawCommand command);
        std::vector<DebugDrawCommand> Snapshot() const;
        // Called once after a host frame. Zero delta freezes all lifetimes.
        void Advance(float simulationDelta);
        void Clear();
        void SetEnabled(bool enabled);
        bool IsEnabled() const;
        void SetCategoryVisible(const std::string &category, bool visible);
        std::map<std::string, bool> Categories() const;
        std::size_t Dropped() const;
    private:
        mutable std::mutex m_mutex;
        std::vector<DebugDrawCommand> m_commands;
        std::map<std::string, bool> m_categories;
        bool m_enabled = true;
        std::size_t m_dropped = 0;
    };
}
