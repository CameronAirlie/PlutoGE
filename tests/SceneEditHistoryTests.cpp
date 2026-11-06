// Scene undo without an editor: when history saves the scene, in-place undo of each kind of edit, snapshots and scoped
// steps coexisting, dirty tracking, verification mode, and the heavy-data paths they depend on.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/EntityStateCapture.h"
#include "PlutoGE/ui/MultiEntityEdit.h"
#include "PlutoGE/ui/SceneEditHistory.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include "PlutoGE/ui/hierarchy/HierarchyExpansion.h"
#include "PlutoGE/ui/hierarchy/HierarchyFilter.h"
#include "PlutoGE/ui/hierarchy/HierarchyRows.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;
using namespace PlutoGE::scene;
using namespace PlutoGE::ui;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    std::string Text(const Scene &scene)
    {
        std::string text, error;
        Require(SceneSerializer::SaveToString(scene, text, &error), "Save failed: " + error);
        return text;
    }

    class TestHost final : public ISceneHistoryHost
    {
    public:
        explicit TestHost(std::unique_ptr<Scene> initial) : scene(std::move(initial)) {}

        std::unique_ptr<Scene> scene;
        std::vector<EntityID> selection;
        std::uint64_t selectionRevision = 1;
        int captures = 0;
        int restores = 0;
        int edits = 0;
        int inPlaceChanges = 0;
        bool suspended = false;
        bool nextRestoreInexact = false;
        std::vector<std::string> warnings;

        Scene *GetHistoryScene() override { return scene.get(); }
        bool CaptureSceneSnapshot(SceneSnapshot &snapshot, std::string &error) override
        {
            ++captures;
            std::string text;
            if (!SceneSerializer::SaveToString(*scene, text, &error)) return false;
            snapshot.text = MakeSceneStateText(std::move(text));
            return true;
        }
        bool RestoreSceneSnapshot(const SceneSnapshot &snapshot, bool &exact, std::string &error) override
        {
            ++restores;
            auto restored = LoadSceneSnapshot(*snapshot.text, error);
            if (!restored) return false;
            scene = std::move(restored);
            exact = !nextRestoreInexact;
            return true;
        }
        void OnEntitiesChangedInPlace() override { ++inPlaceChanges; }
        void OnSceneEdited() override { ++edits; }
        bool IsHistorySuspended() const override { return suspended; }
        std::vector<EntityID> GetEditableSelection() const override { return selection; }
        std::uint64_t GetSelectionRevision() const override { return selectionRevision; }
        void LogHistoryWarning(const std::string &message) override { warnings.push_back(message); }

        void Select(std::vector<EntityID> ids)
        {
            selection = std::move(ids);
            ++selectionRevision;
        }
    };

    // Two root entities, one with a child, and enough components to notice a lost value.
    struct Fixture
    {
        TestHost host;
        SceneEditHistory history;
        EntityID parentId = 0;
        EntityID childId = 0;
        EntityID otherId = 0;

        static std::unique_ptr<Scene> MakeScene(EntityID &parentId, EntityID &childId, EntityID &otherId)
        {
            auto scene = std::make_unique<Scene>();
            auto *parent = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Parent"}));
            parent->CreateComponent<LightComponent>()->SetIntensity(2.0f);
            parent->CreateComponent<ColliderComponent>();
            auto *child = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Child"}), parent);
            child->CreateComponent<LightComponent>()->SetIntensity(3.0f);
            child->SetPosition({1.0f, 2.0f, 3.0f});
            auto *other = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Other", .tags = {"Marker"}}));
            other->CreateComponent<ColliderComponent>();
            parentId = parent->GetID();
            childId = child->GetID();
            otherId = other->GetID();
            return scene;
        }

        Fixture() : host(nullptr), history(host)
        {
            host.scene = MakeScene(parentId, childId, otherId);
            history.Resynchronize();
            history.MarkSaved();
            host.captures = 0;
        }

        Entity *Find(EntityID id) const { return host.scene->FindEntityByID(id); }
        void Prepare() { history.PrepareForEdits(); }
        // An inspector-style edit: made first, reported after.
        void EditSelected(const std::function<void()> &edit)
        {
            Prepare();
            edit();
            history.NoteEdit(SceneEditReach::Selection);
            history.Flush();
        }
        bool Undo()
        {
            std::string label, error;
            const bool done = history.Undo(label, error);
            Require(done || label.empty(), "Undo failed: " + error);
            return done;
        }
        bool Redo()
        {
            std::string label, error;
            const bool done = history.Redo(label, error);
            Require(done || label.empty(), "Redo failed: " + error);
            return done;
        }
    };

    // ------------------------------------------------------------------ Saves per edit

    void TestSavesPerEdit()
    {
        Fixture f;
        const auto *sceneBefore = f.host.scene.get();

        f.history.ExecuteEdit("Whole scene", [&] { f.Find(f.otherId)->SetName("Renamed"); });
        Require(f.host.captures == 1, "A snapshot edit should save once, reusing the observed state as its before-state");
        const auto &first = f.history.GetUndoStack().back();
        const auto sharedText = first.after.text;

        f.host.captures = 0;
        f.history.ExecuteEdit("Whole scene again", [&] { f.Find(f.otherId)->SetName("Renamed twice"); });
        Require(f.host.captures == 1, "A second snapshot edit should also save once");
        Require(f.history.GetUndoStack().back().before.text == sharedText, "Consecutive steps should share one state");

        f.host.captures = 0;
        f.history.BeginGesture("Gesture");
        f.Find(f.otherId)->SetName("Gestured");
        f.history.NoteEdit(SceneEditReach::Scene);
        Require(f.history.EndGesture(), "Gesture did not record");
        Require(f.host.captures == 1, "A whole-scene gesture should save once");

        f.host.captures = 0;
        std::string expected = Text(*f.host.scene);
        Require(f.Undo(), "Undo of a gesture");
        Require(f.host.captures == 0 && f.host.restores == 1, "Snapshot undo should restore without saving");
        Require(f.host.scene.get() != sceneBefore && f.Find(f.otherId)->GetName() == "Renamed twice", "Snapshot undo restored the wrong state");
        Require(f.Redo() && Text(*f.host.scene) == expected, "Snapshot redo restored the wrong state");

        // Restoring something the host had to transform cannot trust the entry's text.
        f.host.captures = 0;
        f.host.nextRestoreInexact = true;
        Require(f.Undo(), "Undo with an inexact restore");
        Require(f.host.captures == 1, "An inexact restore has to read the scene back");
    }

    void TestCommandsAreLazy()
    {
        Fixture f;
        int value = 0;
        const auto command = [&](int to)
        {
            f.history.PushCommand("Command", [&value, to] { value = to - 1; return true; }, [&value, to] { value = to; return true; });
            value = to;
        };
        command(1);
        command(2);
        command(3);
        Require(f.host.captures == 0, "Commands must not save");
        f.Undo();
        f.Undo();
        Require(value == 1 && f.host.captures == 0, "Undoing commands must not save");
        f.Redo();
        Require(value == 2 && f.host.captures == 0, "Redoing commands must not save");

        // A before-state for the next snapshot edit is only taken when one is needed.
        f.history.ExecuteEdit("Snapshot after commands", [&] { f.Find(f.otherId)->SetName("X"); });
        Require(f.host.captures == 2, "The first snapshot edit after a command pays for its before-state");
    }

    void TestCommandApplyDoesNotLeavePendingEdit()
    {
        Fixture f;
        auto *other = f.Find(f.otherId);
        f.history.PushCommand("Command",
                              [&] { other->SetName("Undone"); f.history.NoteEdit(SceneEditReach::Scene); return true; },
                              [&] { other->SetName("Redone"); f.history.NoteEdit(SceneEditReach::Scene); return true; });
        f.Undo();
        Require(!f.history.HasPendingEdit(), "A command reporting its own undo must not look like a new edit");
        Require(f.history.CanRedo(), "A command's own undo cleared redo");
    }

    // ------------------------------------------------------------------ Scoped edits

    void TestInspectorEditIsScoped()
    {
        Fixture f;
        const auto *sceneBefore = f.host.scene.get();
        const auto original = Text(*f.host.scene);
        f.host.Select({f.parentId});
        f.host.captures = 0;

        f.EditSelected([&]
        {
            auto *parent = f.Find(f.parentId);
            parent->SetPosition({5.0f, 6.0f, 7.0f});
            parent->GetComponent<LightComponent>()->SetIntensity(9.0f);
            parent->AddPrefabOverride("Transform.Position");
            parent->AddTag("Edited");
        });
        Require(f.host.captures == 0, "A scoped edit must not save the scene");
        Require(f.history.GetUndoStack().size() == 1, "A scoped edit should push one step");
        const auto edited = Text(*f.host.scene);

        Require(f.Undo(), "Undo of a scoped edit");
        Require(f.host.scene.get() == sceneBefore, "Scoped undo must not replace the scene");
        Require(f.host.restores == 0 && f.host.inPlaceChanges == 1, "Scoped undo must apply in place");
        Require(Text(*f.host.scene) == original, "Scoped undo did not restore the scene");
        Require(f.Find(f.childId)->GetParent() == f.Find(f.parentId), "Scoped undo disturbed the hierarchy");

        Require(f.Redo(), "Redo of a scoped edit");
        Require(Text(*f.host.scene) == edited && f.host.scene.get() == sceneBefore, "Scoped redo did not reach its result");
    }

    void TestScopedEditCoalescesUntilFlush()
    {
        Fixture f;
        f.host.Select({f.parentId});
        f.Prepare();
        for (int step = 0; step < 5; ++step)
        {
            f.Find(f.parentId)->SetPosition({static_cast<float>(step), 0.0f, 0.0f});
            f.history.NoteEdit(SceneEditReach::Selection);
        }
        f.history.Flush();
        Require(f.history.GetUndoStack().size() == 1, "One drag should be one step");
        f.Undo();
        Require(f.Find(f.parentId)->GetPosition() == glm::vec3(0.0f), "A drag's undo should return to where it began");
    }

    void TestNoOpEditPushesNothing()
    {
        Fixture f;
        f.host.Select({f.parentId});
        f.EditSelected([] {});
        Require(f.history.GetUndoStack().empty() && f.host.captures == 0, "An edit that changed nothing should record nothing");
    }

    void TestEditInSameFrameAsARecordedStep()
    {
        // The inspector can run a recorded edit and then report another edit in the same frame, before PrepareForEdits.
        Fixture f;
        f.host.Select({f.otherId});
        f.history.ExecuteEdit("Add", [&] { f.Find(f.otherId)->CreateComponent<LightComponent>(); }, SceneEditScope::Entities({f.otherId}));
        f.history.NoteEdit(SceneEditReach::Selection);
        f.history.Flush();
        Require(f.host.captures == 0 && f.history.GetUndoStack().size() == 1, "A report with nothing new should add nothing and merge nothing");

        int value = 0;
        f.history.PushCommand("Command", [&value] { value = 0; return true; }, [&value] { value = 1; return true; });
        f.Find(f.otherId)->SetName("Edited");
        f.history.NoteEdit(SceneEditReach::Selection);
        f.history.Flush();
        Require(f.host.captures == 0 && f.history.GetUndoStack().size() == 3, "An edit straight after a command should stay its own step");
        f.Undo();
        Require(f.Find(f.otherId)->GetName() == "Other", "Undo of the edit after the command");
    }

    void TestUnreportedChangeJoinsTheNextStep()
    {
        // Reusing the observed state means a change nobody reported is part of the next snapshot step, and undoing that
        // step reverts it too. Code that changes the scene has to report it; this pins down what happens if it does not.
        Fixture f;
        f.Find(f.parentId)->SetName("Unreported");
        f.history.ExecuteEdit("Reported", [&] { f.Find(f.otherId)->SetName("Reported"); });
        f.Undo();
        Require(f.Find(f.otherId)->GetName() == "Other", "Undo of the reported edit");
        Require(f.Find(f.parentId)->GetName() == "Parent", "An unreported change should revert with the next step");

        // Resynchronizing, as saving a scene does, takes it in as the new starting point instead.
        f.Find(f.parentId)->SetName("Unreported");
        f.history.Resynchronize();
        f.history.ExecuteEdit("Reported", [&] { f.Find(f.otherId)->SetName("Reported"); });
        f.Undo();
        Require(f.Find(f.parentId)->GetName() == "Unreported", "A resynchronized change should survive undo");
    }

    void TestSelectionChangeRebaselines()
    {
        Fixture f;
        f.host.Select({f.parentId});
        f.EditSelected([&] { f.Find(f.parentId)->SetName("A"); });
        f.host.Select({f.otherId});
        f.EditSelected([&] { f.Find(f.otherId)->SetName("B"); });
        Require(f.host.captures == 0 && f.history.GetUndoStack().size() == 2, "Each selection should get its own scoped step");
        f.Undo();
        Require(f.Find(f.otherId)->GetName() == "Other" && f.Find(f.parentId)->GetName() == "A", "Undo crossed selections wrongly");
        f.Undo();
        Require(f.Find(f.parentId)->GetName() == "Parent", "Undo did not reach the first selection's edit");
    }

    void TestComponentAddAndRemove()
    {
        Fixture f;
        const auto original = Text(*f.host.scene);
        f.history.ExecuteEdit("Add Component", [&] { f.Find(f.otherId)->CreateComponent<LightComponent>()->SetIntensity(4.0f); },
                              SceneEditScope::Entities({f.otherId}));
        Require(f.host.captures == 0, "Adding a component must not save");
        const auto added = Text(*f.host.scene);
        f.Undo();
        Require(!f.Find(f.otherId)->HasComponent<LightComponent>() && Text(*f.host.scene) == original, "Undo of Add Component");
        f.Redo();
        Require(f.Find(f.otherId)->GetComponent<LightComponent>()->GetLight().intensity == 4.0f && Text(*f.host.scene) == added, "Redo of Add Component");
        f.Undo();

        // Removing the middle of three: positions in a bucket are part of the saved state.
        auto *entity = f.Find(f.otherId);
        for (const float intensity : {1.0f, 2.0f, 3.0f}) entity->CreateComponent<LightComponent>()->SetIntensity(intensity);
        f.history.Resynchronize();
        const auto three = Text(*f.host.scene);
        f.history.ExecuteEdit("Remove Component", [&] { entity->RemoveComponent(entity->GetComponents<LightComponent>()[1]); },
                              SceneEditScope::Entities({f.otherId}));
        f.Undo();
        Require(Text(*f.host.scene) == three, "Undo of removing a middle component lost its position");
    }

    void TestCreateAndDuplicate()
    {
        Fixture f;
        const auto original = Text(*f.host.scene);
        EntityID createdId = 0;
        f.history.ExecuteEdit("Create", [&]
        {
            auto *created = f.host.scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Created"}), f.Find(f.parentId));
            created->CreateComponent<LightComponent>()->SetIntensity(7.0f);
            createdId = created->GetID();
        }, SceneEditScope::Additive());
        Require(f.host.captures == 0, "Creating an entity must not save");
        const auto created = Text(*f.host.scene);
        f.Undo();
        Require(!f.Find(createdId) && Text(*f.host.scene) == original, "Undo of Create");
        f.Redo();
        Require(f.Find(createdId) && f.Find(createdId)->GetParent() == f.Find(f.parentId), "Redo of Create lost its parent or ID");
        Require(Text(*f.host.scene) == created, "Redo of Create");
        f.Undo();

        EntityID copyId = 0;
        f.history.ExecuteEdit("Duplicate", [&] { copyId = Prefab::DuplicateEntity(*f.host.scene, *f.Find(f.parentId), nullptr, true)->GetID(); },
                              SceneEditScope::Additive());
        Require(f.Find(copyId)->GetChildren().size() == 1, "Duplicate should copy the subtree");
        const auto duplicated = Text(*f.host.scene);
        f.Undo();
        Require(Text(*f.host.scene) == original, "Undo of Duplicate left its subtree");
        f.Redo();
        Require(Text(*f.host.scene) == duplicated, "Redo of Duplicate did not bring its subtree back");
    }

    void TestDeleteRestoresSubtreeInPlace()
    {
        Fixture f;
        const auto original = Text(*f.host.scene);
        const auto *sceneBefore = f.host.scene.get();
        // Deleting the first root checks the sibling position is restored, not just the entity.
        f.history.ExecuteEdit("Delete", [&] { f.host.scene->RemoveEntity(f.Find(f.parentId)); }, SceneEditScope::Subtrees({f.parentId}));
        Require(f.host.captures == 0, "Deleting an entity must not save");
        Require(!f.Find(f.parentId) && !f.Find(f.childId), "Delete should remove the subtree");
        const auto deleted = Text(*f.host.scene);
        f.Undo();
        Require(f.host.scene.get() == sceneBefore && f.host.restores == 0, "Undo of Delete should be in place");
        Require(Text(*f.host.scene) == original, "Undo of Delete did not restore the subtree, IDs and order");
        Require(f.host.scene->GetRootEntities().front()->GetID() == f.parentId, "Undo of Delete lost the sibling position");
        f.Redo();
        Require(Text(*f.host.scene) == deleted, "Redo of Delete");
    }

    void TestReparentAndReorder()
    {
        Fixture f;
        const auto original = Text(*f.host.scene);
        f.history.ExecuteEdit("Reparent", [&] { f.Find(f.childId)->SetParent(f.Find(f.otherId)); }, SceneEditScope::Entities({f.childId}));
        Require(f.host.captures == 0, "Reparenting must not save");
        f.Undo();
        Require(f.Find(f.childId)->GetParent() == f.Find(f.parentId) && Text(*f.host.scene) == original, "Undo of Reparent");
        f.Redo();
        Require(f.Find(f.childId)->GetParent() == f.Find(f.otherId), "Redo of Reparent");
        f.Undo();

        f.history.ExecuteEdit("Reorder", [&] { f.Find(f.otherId)->SetSiblingIndex(0); }, SceneEditScope::Entities({f.otherId}));
        Require(f.host.scene->GetRootEntities().front()->GetID() == f.otherId, "Reorder did not apply");
        f.Undo();
        Require(Text(*f.host.scene) == original, "Undo of Reorder");

        // The hierarchy reversing itself needs parents moved first, or a move would be refused as a cycle.
        f.history.ExecuteEdit("Swap", [&]
        {
            auto *parent = f.Find(f.parentId);
            auto *child = f.Find(f.childId);
            child->SetParent(nullptr);
            child->AddChild(parent);
        }, SceneEditScope::Entities({f.parentId, f.childId}));
        f.Undo();
        Require(Text(*f.host.scene) == original, "Undo of a hierarchy inversion");
        f.Redo();
        Require(f.Find(f.parentId)->GetParent() == f.Find(f.childId), "Redo of a hierarchy inversion");
    }

    void TestEditBeyondDeclaredScopeFallsBack()
    {
        Fixture f;
        const auto original = Text(*f.host.scene);
        // Declares one entity, removes another: the removed one's state was never captured.
        f.history.ExecuteEdit("Overreach", [&]
        {
            f.Find(f.parentId)->SetName("Declared");
            f.host.scene->RemoveEntity(f.Find(f.otherId));
        }, SceneEditScope::Entities({f.parentId}));
        Require(f.Undo(), "An overreaching edit should still be undoable");
        Require(Text(*f.host.scene) == original, "Fallback did not restore what the edit reached");
    }

    void TestScopedEditRollsBackOnThrow()
    {
        Fixture f;
        const auto original = Text(*f.host.scene);
        bool threw = false;
        try
        {
            f.history.ExecuteEdit("Fails", [&]
            {
                f.Find(f.parentId)->SetName("Half done");
                f.host.scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Half created"}));
                throw std::runtime_error("Failed");
            }, SceneEditScope::Entities({f.parentId}));
        }
        catch (const std::runtime_error &) { threw = true; }
        Require(threw, "The edit's exception should propagate");
        Require(Text(*f.host.scene) == original, "A failed scoped edit did not roll back");
        Require(f.history.GetUndoStack().empty(), "A failed edit should record nothing");

        threw = false;
        try { f.history.ExecuteEdit("Fails", [&] { f.Find(f.parentId)->SetName("X"); throw std::runtime_error("Failed"); }); }
        catch (const std::runtime_error &) { threw = true; }
        Require(threw && Text(*f.host.scene) == original, "A failed snapshot edit did not roll back");
    }

    // ------------------------------------------------------------------ Snapshots and scoped steps together

    void TestMixedHistoryCrossesBothKinds()
    {
        Fixture f;
        f.host.Select({f.parentId});
        std::vector<std::string> states{Text(*f.host.scene)};
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Scoped 1"); });
        states.push_back(Text(*f.host.scene));
        f.history.ExecuteEdit("Snapshot", [&] { f.Find(f.otherId)->SetName("Snapshot"); });
        states.push_back(Text(*f.host.scene));
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Scoped 2"); });
        states.push_back(Text(*f.host.scene));
        f.history.ExecuteEdit("Structural", [&] { f.host.scene->RemoveEntity(f.Find(f.childId)); }, SceneEditScope::Subtrees({f.childId}));
        states.push_back(Text(*f.host.scene));

        for (int pass = 0; pass < 2; ++pass)
        {
            for (auto state = states.size() - 1; state > 0; --state)
            {
                f.Prepare();
                Require(f.Undo(), "Mixed undo stopped early");
                Require(Text(*f.host.scene) == states[state - 1], "Mixed undo reached the wrong state at step " + std::to_string(state));
            }
            for (std::size_t state = 1; state < states.size(); ++state)
            {
                f.Prepare();
                Require(f.Redo(), "Mixed redo stopped early");
                Require(Text(*f.host.scene) == states[state], "Mixed redo reached the wrong state at step " + std::to_string(state));
            }
        }
    }

    void TestLateWideEditMergesInsteadOfLosingData()
    {
        Fixture f;
        f.host.Select({f.parentId});
        const auto original = Text(*f.host.scene);
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Scoped"); });
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Scoped again"); });
        Require(f.history.GetUndoStack().size() == 2, "Two scoped steps");

        // Reported after the fact, with the observed state two steps behind: its before-state no longer exists.
        f.Find(f.otherId)->SetName("Wide");
        f.history.NoteEdit(SceneEditReach::Scene);
        f.history.Flush();
        Require(f.history.GetUndoStack().size() == 1, "The late edit should merge with the steps since the observed state");
        const auto edited = Text(*f.host.scene);
        f.Undo();
        Require(Text(*f.host.scene) == original, "Undoing the merged step should return to the observed state");
        f.Redo();
        Require(Text(*f.host.scene) == edited, "Redoing the merged step");
    }

    void TestIdleRefreshKeepsLateEditsSeparate()
    {
        Fixture f;
        f.host.Select({f.parentId});
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Scoped"); });
        const auto afterScoped = Text(*f.host.scene);
        f.host.captures = 0;
        f.history.RefreshObservedState();
        Require(f.host.captures == 1, "A stale observed state should refresh");
        f.history.RefreshObservedState();
        Require(f.host.captures == 1, "A current observed state should not refresh again");

        f.Find(f.otherId)->SetName("Wide");
        f.history.NoteEdit(SceneEditReach::Scene);
        f.history.Flush();
        Require(f.history.GetUndoStack().size() == 2, "After a refresh a late edit should keep its own step");
        f.Undo();
        Require(Text(*f.host.scene) == afterScoped, "Undo of the late edit should keep the scoped edit");
    }

    void TestObservedStateCapturedAfterUndoingPastIt()
    {
        Fixture f;
        f.host.Select({f.parentId});
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Scoped"); });
        f.history.ExecuteEdit("Snapshot", [&] { f.Find(f.otherId)->SetName("Snapshot"); });
        f.Undo(); // Observed follows the snapshot back to just after "Scoped".
        f.Undo(); // Now past it, so there is no whole-scene state on the way back from here.
        const auto base = Text(*f.host.scene);

        f.host.Select({});
        f.Prepare();
        f.Find(f.otherId)->SetName("Late");
        f.history.NoteEdit(SceneEditReach::Scene);
        f.history.Flush();
        f.Undo();
        Require(Text(*f.host.scene) == base, "A late edit after undoing past the observed state should still undo correctly");
        Require(f.host.warnings.empty(), "Nothing should have gone unrecorded");
    }

    void TestLinkedSelectionFallsBackToSnapshot()
    {
        Fixture f;
        // Above the scoped limit is the case reachable without a linked model on disk.
        f.host.Select(std::vector<EntityID>(SceneEditHistory::kMaxScopedEntities + 1, f.parentId));
        f.host.captures = 0;
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Snapshot path"); });
        Require(f.host.captures == 1, "A selection too large to scope should record by snapshot");
        Require(f.history.GetUndoStack().back().before.text != nullptr, "Expected a snapshot step");
    }

    // ------------------------------------------------------------------ Limits, dirty tracking, play mode

    void TestSharedStatesDoubleDepth()
    {
        Fixture f;
        // Big enough that the byte limit, not the entry limit, decides depth.
        auto *other = f.Find(f.otherId);
        other->SetName(std::string(SceneEditHistory::kMaxUndoBytes / 6, 'x'));
        f.history.Resynchronize();
        for (int step = 0; step < 12; ++step)
            f.history.ExecuteEdit("Edit", [&] { other->SetPosition({static_cast<float>(step + 1), 0.0f, 0.0f}); });
        const auto depth = f.history.GetUndoStack().size();
        // Unshared, two copies per step would allow only 2 steps. Shared, it is about one copy per step.
        Require(depth >= 4, "Shared states should hold at least twice the unshared depth, got " + std::to_string(depth));
        Require(f.history.GetRetainedBytes() <= SceneEditHistory::kMaxUndoBytes, "History exceeded its byte limit");
    }

    void TestDirtyTracksHistoryPosition()
    {
        Fixture f;
        f.host.Select({f.parentId});
        Require(f.history.IsAtSavedPosition(), "Fresh scene should be at its saved position");
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Edited"); });
        Require(!f.history.IsAtSavedPosition(), "An edit should leave the saved position");
        f.Undo();
        Require(f.history.IsAtSavedPosition(), "Undoing back should reach the saved position");
        f.Redo();
        f.history.MarkSaved();
        f.host.captures = 0;
        Require(f.history.IsAtSavedPosition() && f.host.captures == 0, "Marking saved should not save");
        f.Undo();
        Require(!f.history.IsAtSavedPosition(), "Undoing past a save should be dirty");
        f.Redo();
        Require(f.history.IsAtSavedPosition(), "Redoing to the save should be clean");
        f.Undo();
        f.history.ExecuteEdit("Branch", [&] { f.Find(f.otherId)->SetName("Branch"); });
        Require(!f.history.IsAtSavedPosition(), "A new branch must not match the saved position");

        f.history.ClearSavedPosition();
        Require(!f.history.IsAtSavedPosition(), "With no saved position the scene is always dirty");
        f.history.Clear();
        Require(!f.history.IsAtSavedPosition(), "Clearing history does not make the scene saved");
        f.history.MarkSaved();
        Require(f.history.IsAtSavedPosition(), "Marking after a clear");
    }

    void TestPlayModeParksHistory()
    {
        Fixture f;
        f.history.ExecuteEdit("Before play", [&] { f.Find(f.otherId)->SetName("Before play"); });
        SceneSnapshot snapshot;
        std::string error;
        Require(f.host.CaptureSceneSnapshot(snapshot, error), "Capture");
        f.history.Suspend();
        f.host.suspended = true;
        Require(!f.history.CanUndo(), "Play mode has its own history");

        f.Find(f.otherId)->SetName("During play");
        f.history.NoteEdit(SceneEditReach::Selection);
        f.history.Flush();
        Require(f.history.GetUndoStack().empty(), "Nothing is recorded while suspended");

        bool exact = false;
        f.host.suspended = false;
        Require(f.host.RestoreSceneSnapshot(snapshot, exact, error), "Restore");
        f.history.AdoptObservedSnapshot(snapshot);
        f.history.Resume();
        Require(!f.history.HasPendingEdit() && f.history.GetUndoStack().size() == 1, "Play mode's end should restore the parked history");
        f.Undo();
        Require(f.Find(f.otherId)->GetName() == "Other", "Undo after play mode");
    }

    // ------------------------------------------------------------------ Verification mode

    void RunScriptedSequence(Fixture &f)
    {
        f.host.Select({f.parentId});
        f.EditSelected([&] { f.Find(f.parentId)->SetPosition({1.0f, 1.0f, 1.0f}); });
        f.EditSelected([&] { f.Find(f.parentId)->GetComponent<LightComponent>()->SetIntensity(5.0f); });
        f.EditSelected([&] { f.Find(f.parentId)->SetName("Renamed"); f.Find(f.parentId)->AddPrefabOverride("Name"); });
        f.host.Select({f.parentId, f.otherId});
        f.EditSelected([&] { f.Find(f.parentId)->SetActive(false); f.Find(f.otherId)->SetActive(false); });
        f.history.ExecuteEdit("Add", [&] { f.Find(f.otherId)->CreateComponent<LightComponent>(); }, SceneEditScope::Entities({f.otherId}));
        EntityID created = 0;
        f.history.ExecuteEdit("Create", [&] { created = f.host.scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "New"}), f.Find(f.otherId))->GetID(); },
                              SceneEditScope::Additive());
        f.history.ExecuteEdit("Move", [&] { f.Find(f.childId)->SetParent(f.Find(created)); }, SceneEditScope::Entities({f.childId}));
        f.history.ExecuteEdit("Snapshot", [&] { f.host.scene->SetEnvironmentIntensity(3.0f); });
        f.history.ExecuteEdit("Duplicate", [&] { Prefab::DuplicateEntity(*f.host.scene, *f.Find(f.otherId), nullptr, true); }, SceneEditScope::Additive());
        f.history.ExecuteEdit("Delete", [&] { f.host.scene->RemoveEntity(f.Find(f.otherId)); }, SceneEditScope::Subtrees({f.otherId}));
        const auto end = Text(*f.host.scene);
        const auto steps = f.history.GetUndoStack().size();
        for (std::size_t step = 0; step < steps; ++step) { f.Prepare(); Require(f.Undo(), "Scripted undo"); }
        for (std::size_t step = 0; step < steps; ++step) { f.Prepare(); Require(f.Redo(), "Scripted redo"); }
        Require(Text(*f.host.scene) == end, "Scripted sequence did not return to its end state");
    }

    void TestVerificationModeFindsNothingInScriptedSequence()
    {
        Fixture f;
        f.history.SetVerificationEnabled(true);
        RunScriptedSequence(f);
        for (const auto &finding : f.history.GetVerificationFindings()) std::cerr << finding << '\n';
        Require(f.history.GetVerificationFindings().empty(), "Verification mode reported differences in the scripted sequence");
    }

    void TestVerificationModeCatchesAMissedChange()
    {
        Fixture f;
        f.history.SetVerificationEnabled(true);
        f.host.Select({f.parentId});
        // Says it edits only the selection, but changes another entity too.
        f.EditSelected([&]
        {
            f.Find(f.parentId)->SetName("Selected");
            f.Find(f.otherId)->SetName("Not selected");
        });
        Require(!f.history.GetVerificationFindings().empty(), "A change outside the recorded step should be reported");

        Fixture g;
        g.history.SetVerificationEnabled(true);
        g.host.Select({g.parentId});
        // Changes only something outside the selection: without verification this would be dropped silently.
        g.EditSelected([&] { g.Find(g.otherId)->SetName("Not selected"); });
        Require(!g.history.GetVerificationFindings().empty(), "An unrecorded change should be reported");
        Require(g.Undo(), "The safety net should record what no step did");
        Require(g.Find(g.otherId)->GetName() == "Other", "The safety net's step should undo the change");
    }

    // ------------------------------------------------------------------ Heavy data

    FoliageInstanceSnapshot MakeInstances(int count)
    {
        FoliageInstanceSnapshot snapshot(1);
        for (int index = 0; index < count; ++index)
            snapshot[0].push_back({.id = static_cast<std::uint64_t>(index + 1),
                                   .position = {index * 1.123456789f, 2.5f, -index * 0.1f},
                                   .rotationDegrees = {0.0f, index * 7.654321f, 0.0f},
                                   .scale = {1.0f / 3.0f, 1.0f, 1e-7f}});
        return snapshot;
    }

    std::string PropertyText(const Component &component, const std::string &name)
    {
        for (const auto &property : component.Serialize())
            if (property.name == name) return property.value;
        throw std::runtime_error("Missing property " + name);
    }

    void TestFoliageEncodingRoundTripsExactly()
    {
        Entity entity;
        auto *foliage = entity.CreateComponent<FoliageComponent>();
        foliage->AddType("Type");
        foliage->RestoreInstanceSnapshot(MakeInstances(50));
        const auto expected = foliage->CaptureInstanceSnapshot();

        Entity restoredEntity;
        auto *restored = restoredEntity.CreateComponent<FoliageComponent>();
        restored->Deserialize(foliage->Serialize());
        Require(restored->CaptureInstanceSnapshot()[0] == expected[0], "Foliage instances should round-trip exactly");

        // Text written by the old stream encoder, and records from before instance IDs.
        restored->Deserialize({{"FoliageTypeCount", PropertyType::Int, "1"},
                               {"Type.0.Instances", PropertyType::String, "7,1.5,2,3,0,90,0,1,1,1e-05;4,5,6,0,0,0,2,2,2;bad,record;20,1,1,1,0,0,0,1,1,1"}});
        const auto parsed = restored->CaptureInstanceSnapshot()[0];
        Require(parsed.size() == 3, "Old foliage text should still parse, skipping bad records");
        Require(parsed[0].id == 7 && parsed[0].position.x == 1.5f && parsed[0].scale.z == 1e-05f, "Old foliage record");
        Require(parsed[1].position == glm::vec3(4.0f, 5.0f, 6.0f), "Foliage record without an ID");
        Require(parsed[2].id == 20, "Foliage record after a bad one");
    }

    void TestFoliageCacheFollowsEveryWrite()
    {
        Entity entity;
        auto *foliage = entity.CreateComponent<FoliageComponent>();
        foliage->AddType("Type");
        foliage->RestoreInstanceSnapshot(MakeInstances(10));
        const auto cached = [&] { return foliage->SerializeShared(); };
        const auto textOf = [](const std::vector<Property> &properties)
        {
            for (const auto &property : properties)
                if (property.sharedValue) return property.sharedValue;
            throw std::runtime_error("No heavy property");
        };

        const auto first = textOf(cached());
        Require(textOf(cached()) == first, "An unchanged save should reuse the encoded text");

        // A property edit keeps the text; an instance edit replaces it.
        foliage->DeserializeKeepingHeavyData(foliage->SerializeForInspector());
        Require(foliage->GetTotalInstanceCount() == 10, "Light properties must not clear instances");
        Require(textOf(cached()) == first, "A property edit should not re-encode instances");

        const auto editAndCheck = [&](const char *what, const std::function<void()> &edit)
        {
            const auto before = textOf(cached());
            edit();
            const auto after = textOf(cached());
            Entity freshEntity;
            auto *fresh = freshEntity.CreateComponent<FoliageComponent>();
            fresh->AddType("Type");
            fresh->RestoreInstanceSnapshot(foliage->CaptureInstanceSnapshot());
            Require(*textOf(fresh->SerializeShared()) == *after, std::string("Stale instance text after ") + what);
            Require(before != after, std::string("Revision did not move after ") + what);
        };
        auto instances = foliage->CaptureInstanceSnapshot()[0];
        editAndCheck("SetInstances", [&] { instances[0].position.x += 1.0f; foliage->SetInstances(std::span(instances).first(1)); });
        editAndCheck("SetSelectedTypeInstanceTransform", [&] { foliage->SetSelectedTypeInstanceTransform(1, {9, 9, 9}, {0, 0, 0}, {1, 1, 1}); });
        editAndCheck("RemoveSelectedTypeInstance", [&] { foliage->RemoveSelectedTypeInstance(0); });
        editAndCheck("ApplyInstanceScaleEdit", [&] { foliage->ApplyInstanceScaleEdit({.mode = FoliageScaleMode::Set, .scale = 2.0f}); });
        editAndCheck("RemoveInstances", [&] { const std::uint64_t id = foliage->GetInstances().front().id; foliage->RemoveInstances(std::span(&id, 1)); });
        editAndCheck("ClearSelectedTypeInstances", [&] { foliage->ClearSelectedTypeInstances(); });
    }

    void TestFoliageHeavyDataSurvivesPropertyEditAndHistory()
    {
        Fixture f;
        auto *entity = f.Find(f.otherId);
        auto *foliage = entity->CreateComponent<FoliageComponent>();
        foliage->AddType("Type");
        foliage->RestoreInstanceSnapshot(MakeInstances(20));
        f.history.Resynchronize();
        f.host.Select({f.otherId});
        const auto original = Text(*f.host.scene);
        f.host.captures = 0;

        f.EditSelected([&] { foliage->SetCastShadows(!foliage->GetCastShadows()); });
        Require(f.host.captures == 0, "A foliage property edit must not save");
        Require(f.Undo() && Text(*f.host.scene) == original, "Undo of a foliage property edit");
        Require(f.Find(f.otherId)->GetComponent<FoliageComponent>()->GetTotalInstanceCount() == 20, "Undo lost the instances");

        // Clearing heavy data is beyond what a RevisionOnly baseline holds, so it records as a snapshot.
        f.EditSelected([&] { foliage->ClearInstances(); });
        Require(foliage->GetTotalInstanceCount() == 0, "Clear");
        Require(f.Undo(), "Undo of clearing instances");
        Require(Text(*f.host.scene) == original, "Undoing a heavy edit should restore the instances");
    }

    void TestDeleteRestoresHeavyData()
    {
        Fixture f;
        auto *entity = f.Find(f.otherId);
        auto *foliage = entity->CreateComponent<FoliageComponent>();
        foliage->AddType("Type");
        foliage->RestoreInstanceSnapshot(MakeInstances(20));
        f.history.Resynchronize();
        const auto original = Text(*f.host.scene);
        f.history.ExecuteEdit("Delete", [&] { f.host.scene->RemoveEntity(f.Find(f.otherId)); }, SceneEditScope::Subtrees({f.otherId}));
        f.Undo();
        Require(Text(*f.host.scene) == original, "Undo of deleting foliage should restore its instances");
    }

    BakedProbeVolume MakeBake()
    {
        BakedProbeVolume bake;
        bake.origin = {-1.0f, 0.5f, 1.0f / 3.0f};
        bake.size = {4.0f, 2.0f, 2.0f};
        bake.resolution = {2, 1, 1};
        bake.irradiance = {{0.1f, 0.2f, 0.3f}, {1.0f / 7.0f, 1e-6f, 123.456f}};
        bake.positions = {{1.0f, 2.0f, 3.0f, 1.0f}, {4.0f, 5.0f, 6.0f, 0.0f}};
        bake.sphericalHarmonics.resize(2);
        bake.visibility.resize(2);
        bake.visibilityNormals.resize(2);
        bake.skyVisibility = {{0.1f, 0.2f, 0.3f, 0.4f}, {0.5f, 0.6f, 0.7f, 0.8f}};
        bake.skyIrradianceScale = 0.25f;
        bake.sphericalHarmonics[1][3] = {0.5f, 1.0f / 3.0f, -2.0f};
        bake.visibility[1][7] = 12.5f;
        bake.visibilityNormals[0][2] = {0.0f, 1.0f, 0.0f};
        return bake;
    }

    void TestAmbientBakeEncoding()
    {
        Entity entity, restoredEntity;
        auto *volume = entity.CreateComponent<AmbientLightVolumeComponent>();
        volume->SetBakedVolume(MakeBake());
        auto *restored = restoredEntity.CreateComponent<AmbientLightVolumeComponent>();
        restored->Deserialize(volume->Serialize());
        const auto &bake = restored->GetBakedVolume();
        const auto expected = MakeBake();
        Require(bake.irradiance == expected.irradiance && bake.positions == expected.positions &&
                bake.sphericalHarmonics == expected.sphericalHarmonics && bake.visibility == expected.visibility &&
                bake.visibilityNormals == expected.visibilityNormals && bake.skyVisibility == expected.skyVisibility &&
                bake.skyIrradianceScale == expected.skyIrradianceScale && bake.origin == expected.origin,
                "A bake should round-trip exactly");

        // The formats older files hold, as the stream encoder wrote them.
        Entity oldEntity;
        auto *old = oldEntity.CreateComponent<AmbientLightVolumeComponent>();
        old->Deserialize({{"Baked Data", PropertyType::String, "1 0 0 0 2 2 2 1 1 1 0 0 0.5 0.25 1e-05 "}});
        Require(old->GetBakedVolume().IsValid() && old->GetBakedVolume().irradiance[0].z == 1e-05f, "A version 1 bake should still read");
        old->Deserialize({{"Baked Data", PropertyType::String, "2 1 2 3 2 2 2 1 0 0 0 1 0 0 0 1 1 1 1 0 0 7 8 9 "}});
        Require(old->GetBakedVolume().origin == glm::vec3(1.0f, 2.0f, 3.0f) && old->GetBakedVolume().irradiance[0].x == 7.0f, "A version 2 bake should still read");
        // A truncated bake keeps the previous result.
        old->Deserialize({{"Baked Data", PropertyType::String, "3 1 2 3 2 2 2 1 0 0 0 1 0 0 0 1 2 1 1 0 0 0 0 1 "}});
        Require(old->GetBakedVolume().irradiance[0].x == 7.0f, "A truncated bake should be ignored");

        // A light property write keeps the bake and its text.
        const auto revision = volume->GetHeavyDataRevision();
        volume->DeserializeKeepingHeavyData(volume->SerializeForInspector());
        Require(volume->GetBakedVolume().IsValid(), "Light properties must not clear the bake");
        Require(volume->GetHeavyDataRevision() == revision, "A property write should keep the bake's revision");
        volume->SetBlendDistance(volume->GetBlendDistance() + 1.0f);
        Require(volume->GetHeavyDataRevision() == revision, "Blend distance is not part of the encoded bake");
        volume->ClearBake();
        Require(volume->GetHeavyDataRevision() != revision, "Clearing a bake should move its revision");
        Require(PropertyText(*restored, "Baked Data").starts_with("3 "), "Bakes are written as version 3");
    }

    void TestTerrainHeavyData()
    {
        Entity entity;
        auto *terrain = entity.CreateComponent<TerrainComponent>();
        const auto textOf = [&]
        {
            for (const auto &property : terrain->SerializeShared())
                if (property.name == "HeightSamples") return property.sharedValue;
            throw std::runtime_error("No height samples");
        };
        const auto samples = terrain->GetHeightSamples();
        const auto first = textOf();
        for (const auto &property : terrain->SerializeForInspector())
            Require(property.name != "HeightSamples", "The inspector view should leave out heights");
        // Writing the light properties back keeps the heights, as the inspector relies on.
        terrain->DeserializeKeepingHeavyData(terrain->SerializeForInspector());
        Require(terrain->GetHeightSamples() == samples, "Light properties must not change heights");
        Require(textOf() == first, "A property write should not re-encode heights");

        const auto order = terrain->Serialize();
        const auto heights = std::find_if(order.begin(), order.end(), [](const Property &property) { return property.name == "HeightSamples"; });
        Require(heights != order.end() && std::next(heights)->name == "MaterialAsset", "HeightSamples moved in the property order, which would churn scene files");

        terrain->SetSize(terrain->GetWidth() + 1, terrain->GetDepth());
        Require(textOf() != first, "Resizing should re-encode heights");
    }

    void TestIblCaptureHeavyData()
    {
        Entity entity;
        auto *capture = entity.CreateComponent<IblCaptureComponent>();
        capture->SetResolution(32);
        const auto revision = capture->GetHeavyDataRevision();
        const std::vector<float> pixels(static_cast<std::size_t>(32 * 32 * 4 * 6), 0.5f);
        Require(capture->SetCapturePixels(pixels), "Set capture pixels");
        Require(capture->GetHeavyDataRevision() != revision, "New pixels should move the revision");
        for (const auto &property : capture->SerializeForInspector())
            Require(property.name != "Capture Pixels", "The inspector view should leave out pixels");
        const auto encoded = PropertyText(*capture, "Capture Pixels");
        Require(!encoded.empty(), "Pixels should encode");

        capture->DeserializeKeepingHeavyData(capture->SerializeForInspector());
        Require(capture->GetCaptureTexture() != nullptr && PropertyText(*capture, "Capture Pixels") == encoded, "Light properties must keep the capture");
        capture->DiscardCaptureResult();
        Require(PropertyText(*capture, "Capture Pixels").empty(), "A discarded capture must not serve its old text");
    }

    // ------------------------------------------------------------------ Format selection queries

    void TestFormatQueriesMatchSerializedProperties()
    {
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        const auto root = std::filesystem::temp_directory_path() / "PlutoGE-history-format-query";
        std::filesystem::create_directories(root);
        struct Context
        {
            assets::AssetManager &assets;
            ~Context() { assets.ClearProjectContext(); }
        } context{assets};

        const auto meshMatches = [](const MeshComponent &mesh)
        {
            const auto properties = mesh.Serialize();
            const bool written = std::any_of(properties.begin(), properties.end(), [](const Property &property) { return property.name.ends_with(".InlineMaterialVersion"); });
            return written == mesh.HasInlineMaterialVersion();
        };
        const auto scriptMatches = [](const ScriptComponent &script)
        {
            const auto properties = script.Serialize();
            const bool written = std::any_of(properties.begin(), properties.end(), [](const Property &property) { return property.name.starts_with("__Pluto."); });
            return written == script.WritesManagedAssetFieldMetadata();
        };

        render::Material inlineMaterial(render::MaterialConfig{});
        for (const int version : {1, 5})
        {
            if (version == 1) assets.ClearProjectContext();
            else assets.SetProjectContext(root.string(), "Assets", version);
            MeshComponent empty{MeshComponentConfig{}};
            Require(meshMatches(empty), "Mesh query disagrees for a mesh with no material, version " + std::to_string(version));
            MeshComponent inlined{MeshComponentConfig{.material = &inlineMaterial}};
            Require(meshMatches(inlined), "Mesh query disagrees for an inline material, version " + std::to_string(version));
            ScriptComponent noClass;
            Require(scriptMatches(noClass), "Script query disagrees with no class, version " + std::to_string(version));
            ScriptComponent unknownClass{ScriptComponentConfig{.scriptClass = "NoSuchScriptClass"}};
            Require(scriptMatches(unknownClass), "Script query disagrees for an unknown class, version " + std::to_string(version));
        }
    }

    void TestSceneRoundTripsThroughLoad()
    {
        // Undo trusts this to skip reading the scene back after a restore.
        EntityID a, b, c;
        auto scene = Fixture::MakeScene(a, b, c);
        auto *foliage = scene->FindEntityByID(c)->CreateComponent<FoliageComponent>();
        foliage->AddType("Type");
        foliage->RestoreInstanceSnapshot(MakeInstances(10));
        scene->FindEntityByID(a)->CreateComponent<AmbientLightVolumeComponent>()->SetBakedVolume(MakeBake());
        scene->FindEntityByID(b)->CreateComponent<TerrainComponent>();
        // The first load settles defaults a new component leaves blank (terrain's material reference), as real scenes
        // already have. From then on a save and load must change nothing.
        std::string error;
        const auto settled = LoadSceneSnapshot(Text(*scene), error);
        Require(settled != nullptr, "Load failed: " + error);
        const auto text = Text(*settled);
        auto loaded = LoadSceneSnapshot(text, error);
        Require(loaded != nullptr, "Load failed: " + error);
        const auto reloaded = Text(*loaded);
        const auto difference = std::mismatch(text.begin(), text.end(), reloaded.begin(), reloaded.end());
        const auto offset = static_cast<std::size_t>(difference.first - text.begin());
        Require(reloaded == text, "Save(Load(x)) should equal x; differs at '" + text.substr(offset > 80 ? offset - 80 : 0, 160) + "' vs '" + reloaded.substr(offset > 80 ? offset - 80 : 0, 160) + "'");
    }

    // ------------------------------------------------------------------ Editor helpers

    void TestHierarchyRows()
    {
        EntityID a, b, c;
        const auto scene = Fixture::MakeScene(a, b, c);
        HierarchyExpansion expansion;
        HierarchyFilter filter;
        auto rows = BuildHierarchyRows(*scene, filter, expansion);
        Require(rows.size() == 2 && rows[0].id == a && rows[0].hasChildren && !rows[0].expanded && rows[1].id == c, "Collapsed rows");
        const auto before = expansion.GetRevision();
        expansion.SetExpanded(a, true);
        Require(expansion.GetRevision() != before, "Expanding should change the revision");
        expansion.SetExpanded(a, true);
        Require(expansion.GetRevision() == before + 1, "Expanding twice is one change");
        EntityID unusedA, unusedB, unusedC;
        const auto otherScene = Fixture::MakeScene(unusedA, unusedB, unusedC);
        Require(otherScene->GetStructureRevision() != scene->GetStructureRevision(), "Structure revisions should be unique across scenes");
        rows = BuildHierarchyRows(*scene, filter, expansion);
        Require(rows.size() == 3 && rows[1].id == b && rows[1].depth == 1 && rows[1].parentId == a && rows[2].depth == 0, "Expanded rows");

        filter.SetQuery("Child");
        filter.Refresh(scene.get(), 0.0);
        expansion.Clear();
        rows = BuildHierarchyRows(*scene, filter, expansion);
        Require(rows.size() == 2 && rows[0].id == a && rows[0].expanded && rows[1].id == b, "Filtering opens the path to a match");

        const auto structure = scene->GetStructureRevision();
        scene->FindEntityByID(c)->CreateComponent<LightComponent>();
        Require(scene->GetStructureRevision() != structure, "Adding a component should move the structure revision");
        const auto afterComponent = scene->GetStructureRevision();
        scene->FindEntityByID(b)->SetParent(nullptr);
        Require(scene->GetStructureRevision() != afterComponent, "Reparenting should move the structure revision");
    }

    void TestCommonComponents()
    {
        Scene scene;
        auto *a = scene.AddEntity(std::make_unique<Entity>());
        auto *b = scene.AddEntity(std::make_unique<Entity>());
        auto *c = scene.AddEntity(std::make_unique<Entity>());
        a->CreateComponent<LightComponent>()->SetIntensity(1.0f);
        b->CreateComponent<LightComponent>()->SetIntensity(2.0f);
        a->CreateComponent<FoliageComponent>()->AddType("Type");
        b->CreateComponent<FoliageComponent>()->AddType("Type");
        a->GetComponent<FoliageComponent>()->RestoreInstanceSnapshot(MakeInstances(5));
        b->GetComponent<FoliageComponent>()->RestoreInstanceSnapshot(MakeInstances(7));

        const std::vector<Entity *> selection{a, b};
        CommonComponentCache cache;
        auto *groups = &cache.Get(selection, 1, 1);
        Require(groups->size() == 2, "Two shared components");
        Require(cache.Get(selection, 1, 1).data() == groups->data(), "The cache should keep its groups while nothing changes");

        CommonComponent *lights = nullptr, *foliage = nullptr;
        for (auto &group : *groups) (group.name == "LightComponent" ? lights : foliage) = &group;
        Require(lights && foliage, "Expected light and foliage groups");
        Require(GetMixedProperties(*lights).contains("Intensity"), "Different intensities should be mixed");
        for (const auto &property : foliage->properties)
            Require(!property.name.ends_with(".Instances"), "Multi-select must not offer heavy data");

        // Editing a light field through the group must leave each entity's foliage alone.
        Property castShadows{"CastShadows", PropertyType::Bool, "true"};
        SetCommonProperty(*foliage, castShadows);
        Require(a->GetComponent<FoliageComponent>()->GetTotalInstanceCount() == 5 && b->GetComponent<FoliageComponent>()->GetTotalInstanceCount() == 7, "A common edit lost instances");
        Require(a->GetComponent<FoliageComponent>()->GetCastShadows(), "A common edit did not apply");

        Require(cache.Get({a, c}, 1, 1).empty() && !cache.Get(selection, 1, 2).empty(), "A new selection or edit should rebuild the groups");
    }

    void TestStateCaptureModes()
    {
        Entity entity;
        auto *foliage = entity.CreateComponent<FoliageComponent>();
        foliage->AddType("Type");
        foliage->RestoreInstanceSnapshot(MakeInstances(3));
        const auto light = CaptureEntityState(entity, HeavyDataCapture::RevisionOnly);
        const auto full = CaptureEntityState(entity, HeavyDataCapture::Full);
        const auto hasHeavy = [](const EntityState &state)
        {
            return std::any_of(state.components[0].properties.begin(), state.components[0].properties.end(),
                               [](const Property &property) { return property.sharedValue != nullptr; });
        };
        Require(light && full && !hasHeavy(*light) && hasHeavy(*full), "Capture modes should differ only in heavy data");

        EntityStates before{{entity.GetID(), *light}};
        foliage->ClearInstances();
        EntityStates after{{entity.GetID(), *CaptureEntityState(entity, HeavyDataCapture::RevisionOnly)}};
        Require(DiffEntityStates(before, after).requiresSnapshot, "A heavy change outside the capture must ask for a snapshot");
    }
}

int main()
{
    try
    {
        TestSavesPerEdit();
        TestCommandsAreLazy();
        TestCommandApplyDoesNotLeavePendingEdit();
        TestInspectorEditIsScoped();
        TestScopedEditCoalescesUntilFlush();
        TestNoOpEditPushesNothing();
        TestEditInSameFrameAsARecordedStep();
        TestUnreportedChangeJoinsTheNextStep();
        TestSelectionChangeRebaselines();
        TestComponentAddAndRemove();
        TestCreateAndDuplicate();
        TestDeleteRestoresSubtreeInPlace();
        TestReparentAndReorder();
        TestEditBeyondDeclaredScopeFallsBack();
        TestScopedEditRollsBackOnThrow();
        TestMixedHistoryCrossesBothKinds();
        TestLateWideEditMergesInsteadOfLosingData();
        TestIdleRefreshKeepsLateEditsSeparate();
        TestObservedStateCapturedAfterUndoingPastIt();
        TestLinkedSelectionFallsBackToSnapshot();
        TestSharedStatesDoubleDepth();
        TestDirtyTracksHistoryPosition();
        TestPlayModeParksHistory();
        TestVerificationModeFindsNothingInScriptedSequence();
        TestVerificationModeCatchesAMissedChange();
        TestFoliageEncodingRoundTripsExactly();
        TestFoliageCacheFollowsEveryWrite();
        TestFoliageHeavyDataSurvivesPropertyEditAndHistory();
        TestDeleteRestoresHeavyData();
        TestAmbientBakeEncoding();
        TestTerrainHeavyData();
        TestIblCaptureHeavyData();
        TestFormatQueriesMatchSerializedProperties();
        TestSceneRoundTripsThroughLoad();
        TestHierarchyRows();
        TestCommonComponents();
        TestStateCaptureModes();
        std::cout << "PASS: scene edit history\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
