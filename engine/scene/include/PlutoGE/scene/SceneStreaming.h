#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace PlutoGE::scene
{
    class Scene;
    using SceneSectionID = std::uint64_t;
    enum class SceneSectionState { Reading, Ready, Active, Cancelled, Failed, Unloaded };
    struct SceneSectionStatus
    {
        SceneSectionState state = SceneSectionState::Failed;
        float progress = 0;
        std::string error;
    };
    // All public operations run on the owning Scene's thread. Workers only read
    // bytes; scene parsing, asset creation and entity publication run in Pump.
    class SceneStreaming
    {
    public:
        explicit SceneStreaming(Scene &scene);
        ~SceneStreaming();
        SceneStreaming(const SceneStreaming &) = delete;
        SceneStreaming &operator=(const SceneStreaming &) = delete;
        SceneSectionID Load(const std::filesystem::path &path, bool activateWhenReady = true);
        SceneSectionStatus Status(SceneSectionID id) const;
        std::uint64_t Generation() const;
        bool Activate(SceneSectionID id);
        bool Cancel(SceneSectionID id);
        bool Unload(SceneSectionID id);
        bool Forget(SceneSectionID id); // Release terminal request records.
        void Pump(); // Called by Scene before entity updates; at most one activation/frame.
        void Reset();
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
