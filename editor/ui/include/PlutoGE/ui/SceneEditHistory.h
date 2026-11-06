#pragma once

#include "PlutoGE/ui/EntityStateCapture.h"
#include "PlutoGE/ui/SceneHistory.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::scene { class Scene; }

namespace PlutoGE::ui
{
    // What an edit can change. This decides how it is recorded, so a wider answer is always safe, just slower.
    enum class SceneEditReach
    {
        // Anything in the scene. Recorded as a whole-scene snapshot.
        Scene,
        // Only the selected entities, without their descendants. Recorded as the entities' changes, applied in place.
        Selection,
    };

    // The entities an ExecuteEdit may change or remove. Entities it creates are found afterwards, so a purely additive
    // edit names nothing. An edit that turns out to touch anything else falls back to a snapshot.
    struct SceneEditScope
    {
        std::vector<scene::EntityID> entities;
        bool includeDescendants = false;
        bool wholeScene = true;

        [[nodiscard]] static SceneEditScope WholeScene() { return {}; }
        [[nodiscard]] static SceneEditScope Additive() { return {.wholeScene = false}; }
        [[nodiscard]] static SceneEditScope Entities(std::vector<scene::EntityID> ids) { return {.entities = std::move(ids), .wholeScene = false}; }
        [[nodiscard]] static SceneEditScope Subtrees(std::vector<scene::EntityID> ids) { return {.entities = std::move(ids), .includeDescendants = true, .wholeScene = false}; }
    };

    // What history needs from the editor. Tests supply their own, so history runs without a window or GPU.
    class ISceneHistoryHost
    {
    public:
        virtual ~ISceneHistoryHost() = default;
        [[nodiscard]] virtual scene::Scene *GetHistoryScene() = 0;
        // Saves are the cost this design exists to avoid: every capture of the whole scene goes through here.
        virtual bool CaptureSceneSnapshot(SceneSnapshot &snapshot, std::string &error) = 0;
        // Replaces the scene. `exact` reports whether it now serializes to the snapshot's text without a new capture;
        // a host that had to transform the scene (a model instance refresh, say) sets it to false.
        virtual bool RestoreSceneSnapshot(const SceneSnapshot &snapshot, bool &exact, std::string &error) = 0;
        // Entities may have been destroyed and recreated in place: refresh anything holding pointers to them.
        virtual void OnEntitiesChangedInPlace() = 0;
        virtual void OnSceneEdited() = 0;
        // Editing is suspended (the runtime is playing, for example): nothing is recorded.
        [[nodiscard]] virtual bool IsHistorySuspended() const = 0;
        // The entities Selection-reach edits apply to, and a revision that changes when they do.
        [[nodiscard]] virtual std::vector<scene::EntityID> GetEditableSelection() const = 0;
        [[nodiscard]] virtual std::uint64_t GetSelectionRevision() const = 0;
        virtual void LogHistoryWarning(const std::string &message) = 0;
    };

    // Scene undo and redo. Edits are recorded one of two ways:
    //  - Scoped: before and after states of the entities changed, applied in place. The cost follows the edit.
    //  - Snapshot: the whole scene's text, for edits whose reach is unknown or scene-wide. The cost follows the scene.
    //
    // The "observed" snapshot is the newest whole-scene text, used as the before-state of the next snapshot edit. Scoped
    // edits and commands deliberately leave it behind rather than pay for a save. If an edit of unknown reach then
    // arrives after the fact, when it is too late to capture what came before it, it merges with those steps into one
    // snapshot step back to the observed state. That loses granularity in a rare case but never loses data.
    class SceneEditHistory
    {
    public:
        // More than this many selected entities are recorded by snapshot: capturing them all would itself be a stall.
        static constexpr std::size_t kMaxScopedEntities = 1024;

        explicit SceneEditHistory(ISceneHistoryHost &host) : m_host(host) {}

        // Call before anything can edit this frame: captures what scoped edits need while the scene is still unchanged.
        void PrepareForEdits();
        // Catches the observed state up with the scene. For when the editor is idle: it costs a save, and it means a
        // later edit of unknown reach gets a step of its own rather than merging with the steps before it.
        void RefreshObservedState();

        // An edit already made. Several between flushes form one undo step.
        void NoteEdit(SceneEditReach reach);
        // Records pending edits as an undo step. Not while a gesture is running.
        void Flush();

        // Runs and records `edit`, rolling the scene back if it throws.
        void ExecuteEdit(std::string label, const std::function<void()> &edit, const SceneEditScope &scope = SceneEditScope::WholeScene());

        // A multi-frame edit that becomes one undo step.
        bool BeginGesture(std::string label, SceneEditReach reach = SceneEditReach::Scene);
        bool EndGesture();
        void CancelGesture();
        [[nodiscard]] bool IsGestureActive() const { return m_gesture.active; }

        // An edit the caller already made and knows how to reverse. Captures nothing.
        void PushCommand(std::string label, std::function<bool()> undo, std::function<bool()> redo, std::size_t retainedBytes = 0);
        // A prepared snapshot step, for edits that captured their own states.
        void PushSnapshotStep(std::string label, SceneSnapshot before, SceneSnapshot after);

        bool Undo(std::string &label, std::string &error);
        bool Redo(std::string &label, std::string &error);
        [[nodiscard]] bool HasPendingEdit() const { return m_pendingEdit; }
        [[nodiscard]] bool CanUndo() const { return !m_undo.empty() || m_pendingEdit; }
        [[nodiscard]] bool CanRedo() const { return !m_redo.empty(); }
        void ClearRedo() { m_redo.clear(); }
        void Clear();

        // The scene was replaced, or changed outside history: re-reads it, leaving no pending edit.
        void Resynchronize();
        // As Resynchronize, when the caller already knows the scene's state.
        void AdoptObservedSnapshot(SceneSnapshot snapshot);

        // Whether the scene is where it was when last saved, and the marker for that.
        [[nodiscard]] bool IsAtSavedPosition() const { return m_savedPosition && *m_savedPosition == CurrentPosition(); }
        void MarkSaved() { m_savedPosition = CurrentPosition(); }
        void ClearSavedPosition() { m_savedPosition.reset(); }

        // Play mode gets a history of its own; these park the editor's and bring it back.
        void Suspend();
        void Resume();

        // Verification mode: a whole-scene snapshot beside every step, compared after each undo and redo. Slow, and
        // meant for finding edits that scoped recording misses. Findings go to the host's log and are kept here.
        void SetVerificationEnabled(bool enabled);
        [[nodiscard]] bool IsVerificationEnabled() const { return m_verification; }
        [[nodiscard]] const std::vector<std::string> &GetVerificationFindings() const { return m_verificationFindings; }

        // For tests and diagnostics.
        [[nodiscard]] const std::vector<SceneHistoryEntry> &GetUndoStack() const { return m_undo; }
        [[nodiscard]] const std::vector<SceneHistoryEntry> &GetRedoStack() const { return m_redo; }
        [[nodiscard]] std::size_t GetRetainedBytes() const;

        static constexpr std::size_t kMaxUndoEntries = 80;
        static constexpr std::size_t kMaxUndoBytes = 256ull * 1024ull * 1024ull;

    private:
        struct SelectionBaseline
        {
            bool available = false; // The selection can be recorded by entity state.
            bool current = false;   // `states` matches the scene as of `selectionRevision`.
            std::uint64_t selectionRevision = 0;
            std::vector<scene::EntityID> ids;
            EntityStates states;
        };

        struct Gesture
        {
            bool active = false;
            SceneEditReach reach = SceneEditReach::Scene;
            std::string label;
            SceneSnapshot before;
        };

        [[nodiscard]] std::uint64_t CurrentPosition() const { return m_undo.empty() ? m_basePosition : m_undo.back().id; }
        [[nodiscard]] bool IsObservedCurrent() const { return m_observedPosition == CurrentPosition() && m_observed.text; }
        [[nodiscard]] bool IsObservedAncestor() const;

        bool CaptureObserved(std::string &error);
        void RefreshBaseline();
        // After history moved or an edit was recorded. The baseline has to be right straight away, since the same frame
        // may report another edit, and it only reads light properties so this stays cheap.
        void UpdateBaselineAfterChange();
        // Records the selection's changes since the baseline. False means the snapshot path has to handle them.
        bool TryRecordScoped(const std::string &label);
        void RecordSnapshotEdit(const std::string &label);
        void PushEntry(SceneHistoryEntry entry);
        void PushDelta(std::string label, SceneEntityDelta delta);
        void ExecuteScopedEdit(std::string label, const std::function<void()> &edit, const SceneEditScope &scope);
        void ExecuteSnapshotEdit(std::string label, const std::function<void()> &edit);
        void AfterEntryApplied(const SceneHistoryEntry &entry, bool undone, bool sceneExact);

        // Verification mode.
        void VerifyStep(SceneHistoryEntry &entry);
        void VerifyAfterApply(const SceneHistoryEntry &entry, bool undone);
        void ReportVerificationFinding(const std::string &label, const std::string &message, const SceneStateText &expected, const SceneStateText &actual);
        [[nodiscard]] SceneStateText CaptureForVerification();

        ISceneHistoryHost &m_host;
        std::vector<SceneHistoryEntry> m_undo;
        std::vector<SceneHistoryEntry> m_redo;
        std::vector<SceneHistoryEntry> m_suspendedUndo;
        std::vector<SceneHistoryEntry> m_suspendedRedo;
        std::uint64_t m_suspendedBasePosition = 0;
        std::uint64_t m_nextEntryId = 1;
        std::uint64_t m_basePosition = 0; // Where history starts, once older steps have been dropped or cleared.
        std::optional<std::uint64_t> m_savedPosition;

        SceneSnapshot m_observed;
        std::uint64_t m_observedPosition = 0;

        bool m_pendingEdit = false;
        SceneEditReach m_pendingReach = SceneEditReach::Selection;
        SelectionBaseline m_baseline;
        Gesture m_gesture;

        bool m_verification = false;
        SceneStateText m_verifyExpected;
        std::vector<std::string> m_verificationFindings;
    };
}
