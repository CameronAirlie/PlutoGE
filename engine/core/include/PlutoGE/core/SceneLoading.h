#pragma once
#include <functional>
#include <memory>
#include <string>
#include <cstdint>

namespace PlutoGE::scene { class Scene; }
namespace PlutoGE::core
{
    enum class SceneLoadStage { Idle, Reading, Constructing, Activating, Complete, Failed };
    struct SceneLoadStatus
    {
        SceneLoadStage stage = SceneLoadStage::Idle;
        std::string path;
        std::string error;
        std::uint64_t presentedFrames = 0;
        double longestFrameMs = 0;
    };

    // Owns transition policy, not scene ownership or a particular UI backend.
    // The host retains the old scene until construction succeeds. Presentation
    // pumps only loading UI/events, never gameplay or the editor's normal loop.
    class SceneLoading
    {
    public:
        using Presenter = std::function<void(const SceneLoadStatus &)>;
        using Activation = std::function<void(std::unique_ptr<scene::Scene>)>;
        bool Load(const std::string &path, const Activation &activate, const Presenter &present);
        bool Activate(const std::function<void()> &activate, const Presenter &present);
        const SceneLoadStatus &Status() const noexcept { return m_status; }
        bool IsLoading() const noexcept { return m_active; }
        std::uint64_t Sequence() const noexcept { return m_sequence; }
    private:
        bool Run(const std::function<void()> &work, const Presenter &present);
        SceneLoadStatus m_status;
        bool m_active = false;
        std::uint64_t m_sequence = 0;
    };
}
