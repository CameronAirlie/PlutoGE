// Inspector component actions: copy and paste values, paste as new, reset and reordering within a type.

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/ui/ComponentActions.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE;

namespace
{
    void Require(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    bool Contains(const std::vector<std::string> &names, const std::string &name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }
}

int main()
{
    try
    {
        scene::Scene scene;
        auto *source = scene.AddEntity(std::make_unique<scene::Entity>());
        auto *target = scene.AddEntity(std::make_unique<scene::Entity>());
        auto *sourceCollider = source->CreateComponent<scene::ColliderComponent>();
        sourceCollider->SetRadius(3.5f);
        sourceCollider->SetTrigger(true);
        auto *targetCollider = target->CreateComponent<scene::ColliderComponent>();
        auto *script = target->CreateComponent<scene::ScriptComponent>();

        const auto copied = ui::CopyComponentValues(*sourceCollider);
        Require(copied && copied->typeName == "ColliderComponent" && !copied->properties.empty(), "Copy captures type and values");
        Require(ui::CanPasteComponentValues(*copied, *targetCollider) && !ui::CanPasteComponentValues(*copied, *script),
                "Values paste only onto the same type");

        const auto changed = ui::PasteComponentValues(*copied, *targetCollider);
        Require(targetCollider->GetRadius() == 3.5f && targetCollider->IsTrigger(), "Paste applies the values");
        Require(!changed.empty() && Contains(changed, "Radius"), "Paste reports the changed properties");
        Require(ui::PasteComponentValues(*copied, *targetCollider).empty(), "Pasting the same values changes nothing");
        Require(ui::PasteComponentValues(*copied, *script).empty(), "Pasting onto another type is refused");

        Require(ui::CanResetComponent(*targetCollider) && !ui::CanResetComponent(*script), "Scripts cannot be reset");
        const auto reset = ui::ResetComponentValues(*targetCollider);
        Require(targetCollider->GetRadius() != 3.5f && !targetCollider->IsTrigger() && Contains(reset, "Radius"), "Reset restores defaults");

        auto *added = ui::AddComponentFromValues(*copied, *target);
        auto *addedCollider = dynamic_cast<scene::ColliderComponent *>(added);
        Require(addedCollider && addedCollider->GetRadius() == 3.5f && addedCollider->GetOwner() == target, "Paste as new adds a copy");

        // Two colliders on one entity: reorder within the type, and the order survives saving.
        const auto first = ui::GetComponentSlot(*targetCollider);
        const auto second = ui::GetComponentSlot(*addedCollider);
        Require(first.count == 2 && first.index == 0 && second.index == 1, "Slots count components of the same type");
        Require(!target->MoveComponentWithinType(targetCollider, -1), "The first component cannot move up");
        Require(!target->MoveComponentWithinType(script, 1), "A lone component has nowhere to move");
        const auto revision = target->GetComponentRevision();
        Require(target->MoveComponentWithinType(addedCollider, -1), "Move up");
        Require(ui::GetComponentSlot(*addedCollider).index == 0 && target->GetComponentRevision() != revision, "Move changes order and revision");

        const auto path = std::filesystem::temp_directory_path() / "PlutoGEComponentActionsTests.plutoscene";
        std::string error;
        Require(scene::SceneSerializer::Save(scene, path.string(), &error), "Save scene");
        auto loaded = scene::SceneSerializer::Load(path.string(), &error);
        Require(loaded != nullptr, "Load scene");
        const auto *loadedTarget = loaded->FindEntityByID(target->GetID());
        const auto *firstLoaded = loadedTarget ? loadedTarget->GetComponent<scene::ColliderComponent>() : nullptr;
        Require(firstLoaded && firstLoaded->GetRadius() == 3.5f, "Component order is saved");
        std::filesystem::remove(path);
    }
    catch (const std::exception &error)
    {
        std::cerr << "ComponentActionsTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ComponentActionsTests passed\n";
    return 0;
}
