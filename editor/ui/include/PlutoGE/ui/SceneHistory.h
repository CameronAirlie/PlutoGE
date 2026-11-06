#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace PlutoGE::assets { class ArtifactGenerationLock; }

namespace PlutoGE::ui
{
    using SceneGenerationRetention = std::vector<std::shared_ptr<const assets::ArtifactGenerationLock>>;
    // Scene text is immutable once captured, so history entries that record the same state share one copy.
    using SceneStateText = std::shared_ptr<const std::string>;

    inline SceneStateText MakeSceneStateText(std::string text) { return std::make_shared<const std::string>(std::move(text)); }

    inline bool IsSameSceneState(const SceneStateText &a, const SceneStateText &b)
    {
        if (a == b) return true;
        return a && b && *a == *b;
    }

    struct SceneSnapshot
    {
        SceneStateText text;
        // Keep accepted Library generations collect-proof without retaining GPU resources.
        SceneGenerationRetention generations;
    };

    // One undo step: either scene snapshots to swap between, or commands that change the scene in place.
    struct SceneHistoryEntry
    {
        std::string label;
        SceneSnapshot before;
        SceneSnapshot after;
        std::function<bool()> undo;
        std::function<bool()> redo;
        std::size_t retainedBytes = 0;
        // Identifies the position in history this entry leads to, for dirty tracking. Assigned when pushed.
        std::uint64_t id = 0;
        // Whole-scene states for verification mode; only set while it is on.
        SceneStateText verifyBefore;
        SceneStateText verifyAfter;
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
