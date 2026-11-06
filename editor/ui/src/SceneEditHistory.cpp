#include "PlutoGE/ui/SceneEditHistory.h"

#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <exception>
#include <unordered_set>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kUntrackedEditLabel = "Inspector / Scene Edit";

        // The first line two scene texts disagree on, shortened: heavy properties run to tens of MB on one line.
        std::string DescribeFirstDifference(const std::string &expected, const std::string &actual)
        {
            constexpr std::size_t kContext = 160;
            const auto mismatch = std::mismatch(expected.begin(), expected.end(), actual.begin(), actual.end());
            const auto offset = static_cast<std::size_t>(mismatch.first - expected.begin());
            const auto lineStart = [&](const std::string &text)
            {
                const auto newline = offset == 0 ? std::string::npos : text.rfind('\n', (std::min)(offset, text.size()) - 1);
                return newline == std::string::npos ? std::size_t{0} : newline + 1;
            };
            const auto excerpt = [&](const std::string &text)
            {
                const auto start = lineStart(text);
                auto line = text.substr(start, text.find('\n', start) == std::string::npos ? std::string::npos : text.find('\n', start) - start);
                if (line.size() > kContext) line = line.substr(0, kContext) + "...";
                return line;
            };
            return "expected '" + excerpt(expected) + "', found '" + excerpt(actual) + "'";
        }
    }

    bool SceneEditHistory::IsObservedAncestor() const
    {
        if (m_observedPosition <= m_basePosition) return true;
        return std::any_of(m_undo.begin(), m_undo.end(), [this](const SceneHistoryEntry &entry) { return entry.id == m_observedPosition; });
    }

    bool SceneEditHistory::CaptureObserved(std::string &error)
    {
        SceneSnapshot snapshot;
        if (!m_host.CaptureSceneSnapshot(snapshot, error)) return false;
        m_observed = std::move(snapshot);
        m_observedPosition = CurrentPosition();
        return true;
    }

    void SceneEditHistory::RefreshBaseline()
    {
        const auto *scene = m_host.GetHistoryScene();
        m_baseline.selectionRevision = m_host.GetSelectionRevision();
        m_baseline.ids = m_host.GetEditableSelection();
        m_baseline.states.clear();
        m_baseline.available = false;
        m_baseline.current = true;
        if (!scene || m_baseline.ids.empty() || m_baseline.ids.size() > kMaxScopedEntities) return;
        if (auto states = CaptureEntityStates(*scene, m_baseline.ids, false, HeavyDataCapture::RevisionOnly))
        {
            m_baseline.states = std::move(*states);
            m_baseline.available = true;
        }
    }

    void SceneEditHistory::UpdateBaselineAfterChange()
    {
        // A pending edit is not in the baseline yet, and must stay out of it. So is anything a suspended scene does.
        if (m_pendingEdit || m_host.IsHistorySuspended()) m_baseline.current = false;
        else RefreshBaseline();
    }

    void SceneEditHistory::PrepareForEdits()
    {
        if (m_gesture.active || m_pendingEdit || m_host.IsHistorySuspended() || !m_host.GetHistoryScene()) return;
        if (!m_baseline.current || m_baseline.selectionRevision != m_host.GetSelectionRevision())
            RefreshBaseline();
        // A selection history cannot scope, or an observed state no longer on the way back to here, would leave the
        // next edit with nothing to record it against. This is the last moment before it could happen.
        const bool selectionNeedsSnapshot = !m_baseline.available && !m_baseline.ids.empty() && !IsObservedCurrent();
        if (selectionNeedsSnapshot || !IsObservedAncestor())
        {
            std::string error;
            if (!CaptureObserved(error)) m_host.LogHistoryWarning(error);
        }
    }

    void SceneEditHistory::RefreshObservedState()
    {
        if (m_gesture.active || m_pendingEdit || m_host.IsHistorySuspended() || !m_host.GetHistoryScene() || IsObservedCurrent()) return;
        std::string error;
        if (!CaptureObserved(error)) m_host.LogHistoryWarning(error);
    }

    void SceneEditHistory::NoteEdit(SceneEditReach reach)
    {
        // Mixed edits between flushes are as wide as the widest of them.
        if (!m_pendingEdit) m_pendingReach = reach;
        else if (reach == SceneEditReach::Scene) m_pendingReach = reach;
        m_pendingEdit = true;
    }

    bool SceneEditHistory::TryRecordScoped(const std::string &label)
    {
        // The selection moved under the pending edit, so the baseline is for other entities.
        if (!m_baseline.available || !m_baseline.current || m_baseline.selectionRevision != m_host.GetSelectionRevision()) return false;
        auto *scene = m_host.GetHistoryScene();
        if (!scene) return false;
        auto after = CaptureEntityStates(*scene, m_baseline.ids, false, HeavyDataCapture::RevisionOnly);
        if (!after) return false;
        auto delta = DiffEntityStates(m_baseline.states, *after);
        if (delta.requiresSnapshot) return false;
        if (delta.Empty())
        {
            // Nothing the selection holds changed, and the edit declared that it could not reach further.
            if (m_verification)
                if (const auto actual = CaptureForVerification(); !IsSameSceneState(actual, m_verifyExpected))
                {
                    ReportVerificationFinding(label, "changed the scene outside the selection, which no step recorded", m_verifyExpected, actual);
                    return false;
                }
            return true;
        }
        m_baseline.states = std::move(*after);
        PushDelta(label, std::move(delta));
        return true;
    }

    void SceneEditHistory::RecordSnapshotEdit(const std::string &label)
    {
        core::CpuScope scope("Scene History Snapshot Edit", core::CpuCategory::UI, label);
        SceneSnapshot after;
        std::string error;
        if (!m_host.CaptureSceneSnapshot(after, error))
        {
            m_host.LogHistoryWarning(error);
            return;
        }

        const bool observedCurrent = IsObservedCurrent();
        if (!observedCurrent && (!m_observed.text || !IsObservedAncestor()))
        {
            // No earlier state to return to, which PrepareForEdits exists to prevent. Keep history usable.
            m_host.LogHistoryWarning("Scene edit could not be recorded for undo: '" + label + "'.");
            m_observed = std::move(after);
            m_observedPosition = CurrentPosition();
            return;
        }

        std::string mergedLabel = label;
        if (!observedCurrent)
        {
            // The state before this edit is gone, so it can only be undone together with the steps since the last
            // whole-scene state. Their commands would apply to a scene they no longer describe.
            const auto firstMerged = std::find_if(m_undo.begin(), m_undo.end(), [this](const SceneHistoryEntry &entry) { return entry.id > m_observedPosition; });
            mergedLabel = label + " (with " + std::to_string(m_undo.end() - firstMerged) + " earlier step(s))";
            m_undo.erase(firstMerged, m_undo.end());
        }
        if (!IsSameSceneState(m_observed.text, after.text))
            PushSnapshotStep(std::move(mergedLabel), m_observed, after);
        m_observed = std::move(after);
        m_observedPosition = CurrentPosition();
    }

    void SceneEditHistory::Flush()
    {
        if (!m_pendingEdit || m_gesture.active || m_host.IsHistorySuspended() || !m_host.GetHistoryScene()) return;
        core::CpuScope scope("Scene History Flush", core::CpuCategory::UI);
        const bool recorded = m_pendingReach == SceneEditReach::Selection && TryRecordScoped(kUntrackedEditLabel);
        if (!recorded) RecordSnapshotEdit(kUntrackedEditLabel);
        m_pendingEdit = false;
        if (!recorded) UpdateBaselineAfterChange();
    }

    void SceneEditHistory::PushEntry(SceneHistoryEntry entry)
    {
        entry.id = m_nextEntryId++;
        if (m_verification) VerifyStep(entry);
        m_undo.push_back(std::move(entry));
        m_redo.clear();
        // Entries overlap, so each distinct text counts once. Oldest entries release theirs first.
        while (m_undo.size() > 1 && (m_undo.size() > kMaxUndoEntries || GetRetainedBytes() > kMaxUndoBytes))
        {
            m_basePosition = m_undo.front().id;
            m_undo.erase(m_undo.begin());
        }
    }

    std::size_t SceneEditHistory::GetRetainedBytes() const
    {
        std::unordered_set<const std::string *> texts;
        std::size_t bytes = 0;
        const auto countText = [&](const SceneStateText &text)
        {
            if (text && texts.insert(text.get()).second) bytes += text->size();
        };
        for (const auto &entry : m_undo)
        {
            bytes += entry.label.size() + entry.retainedBytes;
            countText(entry.before.text);
            countText(entry.after.text);
        }
        return bytes;
    }

    void SceneEditHistory::PushCommand(std::string label, std::function<bool()> undo, std::function<bool()> redo, std::size_t retainedBytes)
    {
        if (!undo || !redo) return;
        PushEntry(SceneHistoryEntry{.label = std::move(label), .undo = std::move(undo), .redo = std::move(redo), .retainedBytes = retainedBytes});
        m_pendingEdit = false;
        UpdateBaselineAfterChange();
        m_host.OnSceneEdited();
    }

    void SceneEditHistory::PushDelta(std::string label, SceneEntityDelta delta)
    {
        const auto bytes = delta.ByteSize();
        auto shared = std::make_shared<const SceneEntityDelta>(std::move(delta));
        const auto apply = [this, shared](EntityDeltaDirection direction)
        {
            auto *scene = m_host.GetHistoryScene();
            std::string error;
            if (!scene || !ApplyEntityDelta(*scene, *shared, direction, error))
            {
                m_host.LogHistoryWarning(error.empty() ? "No scene to apply the edit to." : error);
                return false;
            }
            m_host.OnEntitiesChangedInPlace();
            return true;
        };
        PushEntry(SceneHistoryEntry{.label = std::move(label),
                                    .undo = [apply] { return apply(EntityDeltaDirection::Undo); },
                                    .redo = [apply] { return apply(EntityDeltaDirection::Redo); },
                                    .retainedBytes = bytes});
        m_host.OnSceneEdited();
    }

    void SceneEditHistory::PushSnapshotStep(std::string label, SceneSnapshot before, SceneSnapshot after)
    {
        PushEntry(SceneHistoryEntry{.label = std::move(label), .before = std::move(before), .after = std::move(after)});
        const auto &pushed = m_undo.back();
        m_observed = pushed.after;
        m_observedPosition = pushed.id;
        UpdateBaselineAfterChange();
        m_host.OnSceneEdited();
    }

    void SceneEditHistory::ExecuteEdit(std::string label, const std::function<void()> &edit, const SceneEditScope &scope)
    {
        if (!edit) return;
        if (m_gesture.active)
        {
            edit(); // The surrounding gesture owns the record.
            NoteEdit(SceneEditReach::Scene);
            m_host.OnSceneEdited();
            return;
        }
        Flush();
        if (!scope.wholeScene && m_host.GetHistoryScene() && !m_host.IsHistorySuspended())
            ExecuteScopedEdit(std::move(label), edit, scope);
        else
            ExecuteSnapshotEdit(std::move(label), edit);
    }

    void SceneEditHistory::ExecuteSnapshotEdit(std::string label, const std::function<void()> &edit)
    {
        core::CpuScope scope("Scene History Snapshot Edit", core::CpuCategory::UI, label);
        std::string error;
        const bool capturedBefore = IsObservedCurrent() || CaptureObserved(error);
        const SceneSnapshot before = capturedBefore ? m_observed : SceneSnapshot{};
        try { edit(); }
        catch (...)
        {
            bool exact = false;
            std::string restoreError = error;
            if (!capturedBefore || !m_host.RestoreSceneSnapshot(before, exact, restoreError))
            {
                m_host.OnSceneEdited();
                m_host.LogHistoryWarning("Cannot roll back failed scene edit: " + restoreError);
            }
            throw;
        }

        if (!capturedBefore)
        {
            NoteEdit(SceneEditReach::Scene);
            m_host.OnSceneEdited();
            m_host.LogHistoryWarning(error.empty() ? "Edited scene without undo snapshot." : error);
            return;
        }

        SceneSnapshot after;
        if (!m_host.CaptureSceneSnapshot(after, error))
        {
            NoteEdit(SceneEditReach::Scene);
            m_host.OnSceneEdited();
            m_host.LogHistoryWarning(error);
            return;
        }
        if (IsSameSceneState(before.text, after.text)) return;
        PushSnapshotStep(std::move(label), before, std::move(after));
    }

    void SceneEditHistory::ExecuteScopedEdit(std::string label, const std::function<void()> &edit, const SceneEditScope &scope)
    {
        core::CpuScope cpuScope("Scene History Scoped Edit", core::CpuCategory::UI, label);
        auto &scene = *m_host.GetHistoryScene();
        // Too many to capture cheaply, linked models and failed captures all go through the scene format instead.
        const auto capturedBefore = scope.entities.size() > kMaxScopedEntities
                                        ? std::nullopt
                                        : CaptureEntityStates(scene, scope.entities, scope.includeDescendants, HeavyDataCapture::Full);
        if (!capturedBefore)
        {
            ExecuteSnapshotEdit(std::move(label), edit);
            return;
        }
        const auto structureBefore = CaptureSceneStructure(scene);
        const auto instanceCount = scene.GetStaticModelInstances().size();

        const auto capturePostEdit = [&](bool &withinScope) -> std::optional<EntityStates>
        {
            std::vector<scene::EntityID> touched;
            const auto structureAfter = CaptureSceneStructure(scene);
            withinScope = scene.GetStaticModelInstances().size() == instanceCount;
            for (const auto &[id, parent] : structureBefore)
            {
                const auto found = structureAfter.find(id);
                if (capturedBefore->contains(id)) touched.push_back(id);
                else if (found == structureAfter.end() || found->second != parent) withinScope = false;
            }
            for (const auto &[id, parent] : structureAfter)
                if (!structureBefore.contains(id)) touched.push_back(id);
            return withinScope ? CaptureEntityStates(scene, touched, false, HeavyDataCapture::Full) : std::nullopt;
        };

        try { edit(); }
        catch (...)
        {
            bool withinScope = false;
            std::string error;
            auto after = capturePostEdit(withinScope);
            auto delta = after ? DiffEntityStates(*capturedBefore, *after) : SceneEntityDelta{};
            if (!after || !ApplyEntityDelta(scene, delta, EntityDeltaDirection::Undo, error))
            {
                m_host.OnSceneEdited();
                m_host.LogHistoryWarning("Cannot roll back failed scene edit: " + (error.empty() ? std::string("it reached beyond its scope.") : error));
            }
            else m_host.OnEntitiesChangedInPlace();
            throw;
        }

        bool withinScope = false;
        auto after = capturePostEdit(withinScope);
        if (after)
        {
            auto delta = DiffEntityStates(*capturedBefore, *after);
            if (!delta.Empty()) PushDelta(std::move(label), std::move(delta));
            UpdateBaselineAfterChange();
            return;
        }
        // The edit went further than it said. Its before-state is gone, so it records like any after-the-fact edit.
        RecordSnapshotEdit(label);
        UpdateBaselineAfterChange();
    }

    bool SceneEditHistory::BeginGesture(std::string label, SceneEditReach reach)
    {
        if (m_gesture.active) return false;
        Flush();
        std::string error;
        if (reach == SceneEditReach::Selection)
        {
            RefreshBaseline();
            if (!m_baseline.available) reach = SceneEditReach::Scene;
        }
        if (reach == SceneEditReach::Scene)
        {
            if (!IsObservedCurrent() && !CaptureObserved(error))
            {
                m_host.LogHistoryWarning(error.empty() ? "Started scene edit without undo snapshot." : error);
                return false;
            }
            m_gesture.before = m_observed;
        }
        m_gesture.label = std::move(label);
        m_gesture.reach = reach;
        m_gesture.active = true;
        return true;
    }

    bool SceneEditHistory::EndGesture()
    {
        if (!m_gesture.active) return false;
        Gesture gesture = std::move(m_gesture);
        m_gesture = {};
        const std::string label = gesture.label.empty() ? "Scene Edit" : gesture.label;
        const auto entriesBefore = m_undo.empty() ? 0 : m_undo.back().id;
        m_pendingEdit = false;

        if (gesture.reach == SceneEditReach::Selection)
        {
            // Failing to scope means heavy data changed, which the baseline did not hold.
            if (!TryRecordScoped(label)) RecordSnapshotEdit(label);
            UpdateBaselineAfterChange();
            return (m_undo.empty() ? 0 : m_undo.back().id) != entriesBefore;
        }

        SceneSnapshot after;
        std::string error;
        if (!gesture.before.text || !m_host.CaptureSceneSnapshot(after, error))
        {
            NoteEdit(SceneEditReach::Scene);
            m_host.OnSceneEdited();
            return false;
        }
        if (IsSameSceneState(gesture.before.text, after.text))
        {
            m_observed = std::move(after);
            m_observedPosition = CurrentPosition();
            UpdateBaselineAfterChange();
            return false;
        }
        PushSnapshotStep(label, std::move(gesture.before), std::move(after));
        return true;
    }

    void SceneEditHistory::CancelGesture()
    {
        m_gesture = {};
    }

    void SceneEditHistory::AfterEntryApplied(const SceneHistoryEntry &entry, bool undone, bool sceneExact)
    {
        // Commands report their own changes as edits while applying; those are the step itself.
        m_pendingEdit = false;
        UpdateBaselineAfterChange();
        const auto &target = undone ? entry.before : entry.after;
        if (target.text)
        {
            if (sceneExact)
            {
                m_observed = target;
                m_observedPosition = CurrentPosition();
            }
            else
            {
                std::string error;
                if (!CaptureObserved(error)) m_host.LogHistoryWarning(error);
            }
        }
        if (m_verification) VerifyAfterApply(entry, undone);
    }

    bool SceneEditHistory::Undo(std::string &label, std::string &error)
    {
        EndGesture();
        Flush();
        if (m_undo.empty()) return false;
        core::CpuScope scope("Scene History Undo", core::CpuCategory::UI, m_undo.back().label);
        label = m_undo.back().label;
        bool exact = true;
        const auto apply = [&](const SceneHistoryEntry &entry)
        { return entry.undo ? entry.undo() : m_host.RestoreSceneSnapshot(entry.before, exact, error); };
        if (!TransferSceneHistory(m_undo, m_redo, apply)) return false;
        AfterEntryApplied(m_redo.back(), true, exact);
        return true;
    }

    bool SceneEditHistory::Redo(std::string &label, std::string &error)
    {
        EndGesture();
        Flush();
        if (m_redo.empty()) return false;
        core::CpuScope scope("Scene History Redo", core::CpuCategory::UI, m_redo.back().label);
        label = m_redo.back().label;
        bool exact = true;
        const auto apply = [&](const SceneHistoryEntry &entry)
        { return entry.redo ? entry.redo() : m_host.RestoreSceneSnapshot(entry.after, exact, error); };
        if (!TransferSceneHistory(m_redo, m_undo, apply)) return false;
        AfterEntryApplied(m_undo.back(), false, exact);
        return true;
    }

    void SceneEditHistory::Clear()
    {
        m_undo.clear();
        m_redo.clear();
        // Positions never repeat, so a saved marker cannot match a state from before the clear by accident.
        m_basePosition = m_nextEntryId++;
        m_observedPosition = m_observed.text ? m_basePosition : 0;
        m_baseline.current = false;
    }

    void SceneEditHistory::Resynchronize()
    {
        m_baseline.current = false;
        m_gesture = {};
        std::string error;
        if (CaptureObserved(error))
        {
            m_pendingEdit = false;
            if (m_verification) m_verifyExpected = m_observed.text;
        }
    }

    void SceneEditHistory::AdoptObservedSnapshot(SceneSnapshot snapshot)
    {
        m_baseline.current = false;
        m_gesture = {};
        m_pendingEdit = false;
        m_observed = std::move(snapshot);
        m_observedPosition = CurrentPosition();
        if (m_verification) m_verifyExpected = m_observed.text;
    }

    void SceneEditHistory::Suspend()
    {
        m_suspendedUndo = std::move(m_undo);
        m_suspendedRedo = std::move(m_redo);
        m_suspendedBasePosition = m_basePosition;
        m_undo.clear();
        m_redo.clear();
        m_basePosition = m_nextEntryId++;
        m_baseline.current = false;
    }

    void SceneEditHistory::Resume()
    {
        m_undo = std::move(m_suspendedUndo);
        m_redo = std::move(m_suspendedRedo);
        m_basePosition = m_suspendedBasePosition;
        m_suspendedUndo.clear();
        m_suspendedRedo.clear();
        m_observedPosition = CurrentPosition();
        m_baseline.current = false;
    }

    // ---------------------------------------------------------------------
    // Verification mode

    void SceneEditHistory::SetVerificationEnabled(bool enabled)
    {
        if (m_verification == enabled) return;
        Flush();
        m_verification = enabled;
        m_verifyExpected = enabled ? CaptureForVerification() : nullptr;
    }

    SceneStateText SceneEditHistory::CaptureForVerification()
    {
        SceneSnapshot snapshot;
        std::string error;
        return m_host.CaptureSceneSnapshot(snapshot, error) ? snapshot.text : nullptr;
    }

    void SceneEditHistory::ReportVerificationFinding(const std::string &label, const std::string &message, const SceneStateText &expected, const SceneStateText &actual)
    {
        std::string finding = "Undo verification: '" + label + "' " + message;
        if (expected && actual) finding += ": " + DescribeFirstDifference(*expected, *actual);
        m_verificationFindings.push_back(finding);
        m_host.LogHistoryWarning(finding);
    }

    void SceneEditHistory::VerifyStep(SceneHistoryEntry &entry)
    {
        const auto after = CaptureForVerification();
        entry.verifyBefore = m_verifyExpected;
        entry.verifyAfter = after;
        m_verifyExpected = after;
        // A command that reverses exactly proves it recorded everything the edit changed. Snapshots are exact already.
        if (!entry.undo) return;
        if (!entry.undo())
        {
            ReportVerificationFinding(entry.label, "failed to undo when verified", nullptr, nullptr);
            return;
        }
        if (const auto undone = CaptureForVerification(); !IsSameSceneState(undone, entry.verifyBefore))
            ReportVerificationFinding(entry.label, "did not restore the scene when undone", entry.verifyBefore, undone);
        if (!entry.redo())
        {
            ReportVerificationFinding(entry.label, "failed to redo when verified", nullptr, nullptr);
            return;
        }
        if (const auto redone = CaptureForVerification(); !IsSameSceneState(redone, after))
            ReportVerificationFinding(entry.label, "did not reach its result when redone", after, redone);
    }

    void SceneEditHistory::VerifyAfterApply(const SceneHistoryEntry &entry, bool undone)
    {
        const auto actual = CaptureForVerification();
        const auto &expected = undone ? entry.verifyBefore : entry.verifyAfter;
        if (expected && !IsSameSceneState(actual, expected))
            ReportVerificationFinding(entry.label, undone ? "undo did not restore the expected scene" : "redo did not restore the expected scene", expected, actual);
        m_verifyExpected = actual;
    }
}
