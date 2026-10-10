#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::assets { class ArtifactGenerationLock; }

namespace PlutoGE::ui
{
    using SceneGenerationRetention = std::vector<std::shared_ptr<const assets::ArtifactGenerationLock>>;

    struct SceneHistoryEntry
    {
        std::string label;
        std::string beforeState;
        std::string afterState;
        std::function<bool()> undo;
        std::function<bool()> redo;
        std::size_t retainedBytes = 0;
        // Keep accepted Library generations collect-proof without retaining GPU resources.
        SceneGenerationRetention beforeGenerations;
        SceneGenerationRetention afterGenerations;
    };

    // Only transfer ownership after application succeeds. Failed commands remain retryable.
    inline bool TransferSceneHistory(std::vector<SceneHistoryEntry> &from,
                                     std::vector<SceneHistoryEntry> &to,
                                     const std::function<bool(const SceneHistoryEntry &)> &apply)
    {
        if (from.empty() || !apply(from.back())) return false;
        to.push_back(std::move(from.back()));
        from.pop_back();
        return true;
    }
}
