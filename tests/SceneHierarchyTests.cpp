#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EntityLocks.h"
#include "PlutoGE/ui/EntitySelection.h"
#include "PlutoGE/ui/hierarchy/HierarchyDropPlacement.h"
#include "PlutoGE/ui/hierarchy/HierarchyEntityIcons.h"
#include "PlutoGE/ui/hierarchy/HierarchyExpansion.h"
#include "PlutoGE/ui/hierarchy/HierarchyFilter.h"
#include "PlutoGE/ui/hierarchy/HierarchyNavigation.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace PlutoGE;

    void Require(bool value, const char *message)
    {
        if (!value) throw std::runtime_error(message);
    }

    scene::Entity *Add(scene::Scene &scene, const char *name, scene::Entity *parent = nullptr)
    {
        return scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = name}), parent);
    }

    std::string Names(const std::vector<scene::Entity *> &entities)
    {
        std::string names;
        for (const auto *entity : entities) names += entity->GetName();
        return names;
    }

    void TestSiblingIndex()
    {
        scene::Scene scene;
        auto *a = Add(scene, "A");
        Add(scene, "B");
        auto *c = Add(scene, "C");
        Require(c->GetSiblingIndex() == 2, "Root sibling index must follow the scene root order");
        c->SetSiblingIndex(0);
        Require(Names(scene.GetRootEntities()) == "CAB", "Moving a root earlier must keep the others in order");
        c->SetSiblingIndex(99);
        Require(Names(scene.GetRootEntities()) == "ABC", "Indices past the end must clamp to the last slot");
        auto *x = Add(scene, "X", a);
        auto *y = Add(scene, "Y", a);
        x->SetSiblingIndex(1);
        Require(Names(a->GetChildren()) == "YX" && y->GetSiblingIndex() == 0, "Child order must be editable");
    }

    void TestDropPlacement()
    {
        using ui::HierarchyDropZone;
        Require(ui::ClassifyHierarchyDrop(0.0f, 20.0f, 2.0f) == HierarchyDropZone::Before &&
                    ui::ClassifyHierarchyDrop(0.0f, 20.0f, 10.0f) == HierarchyDropZone::Inside &&
                    ui::ClassifyHierarchyDrop(0.0f, 20.0f, 18.0f) == HierarchyDropZone::After,
                "Row edges must reorder and the middle must parent");

        scene::Scene scene;
        auto *a = Add(scene, "A");
        auto *b = Add(scene, "B");
        auto *c = Add(scene, "C");
        Require(ui::MoveEntityTo(*c, ui::ResolveHierarchyDrop(*a, HierarchyDropZone::Before, false)) &&
                    Names(scene.GetRootEntities()) == "CAB",
                "Dropping before a row must insert above it");
        Require(ui::MoveEntityTo(*a, ui::ResolveHierarchyDrop(*b, HierarchyDropZone::After, false)) &&
                    Names(scene.GetRootEntities()) == "CBA",
                "Moving later in the same list must account for the removed slot");
        Require(ui::MoveEntityTo(*b, ui::ResolveHierarchyDrop(*a, HierarchyDropZone::Inside, false)) &&
                    Names(scene.GetRootEntities()) == "CA" && Names(a->GetChildren()) == "B",
                "Dropping inside a row must append it as a child");
        Require(!ui::MoveEntityTo(*a, ui::ResolveHierarchyDrop(*b, HierarchyDropZone::Inside, false)) &&
                    b->GetParent() == a && Names(scene.GetRootEntities()) == "CA",
                "A parent must not move beneath its own descendant");

        auto *d = Add(scene, "D", a);
        // A, expanded with children B and D: dropping after it means its first child slot.
        Require(ui::MoveEntityTo(*c, ui::ResolveHierarchyDrop(*a, HierarchyDropZone::After, true)) &&
                    Names(a->GetChildren()) == "CBD",
                "Dropping below an expanded parent must insert as its first child");

        auto *x = Add(scene, "X");
        auto *y = Add(scene, "Y");
        const std::vector<scene::Entity *> block{x, y, a};
        Require(ui::MoveEntitiesRelativeTo(block, *d, HierarchyDropZone::Before, false) == 2 &&
                    Names(a->GetChildren()) == "CBXYD",
                "Block moves must stay contiguous and skip entities that contain the target");
    }

    void TestNavigation()
    {
        using ui::HierarchyNavKey;
        // R1 (expanded) > C1, C2 (collapsed, has children); R2.
        const std::vector<ui::HierarchyRow> rows{
            {.id = 1, .parentId = 0, .hasChildren = true, .expanded = true},
            {.id = 2, .parentId = 1},
            {.id = 3, .parentId = 1, .hasChildren = true, .expanded = false},
            {.id = 4, .parentId = 0},
        };
        const auto nav = [&](std::uint32_t current, HierarchyNavKey key) { return ui::NavigateHierarchy(rows, current, key); };
        Require(nav(0, HierarchyNavKey::Down).select == 1 && nav(0, HierarchyNavKey::Up).select == 4,
                "Navigation without a current row must start at an end");
        Require(nav(1, HierarchyNavKey::Down).select == 2 && nav(1, HierarchyNavKey::Up).select == 0 &&
                    nav(4, HierarchyNavKey::Down).select == 0,
                "Up/Down must move one row and stop at the ends");
        Require(nav(1, HierarchyNavKey::Left).collapse == 1 && nav(2, HierarchyNavKey::Left).select == 1 &&
                    nav(4, HierarchyNavKey::Left).select == 0,
                "Left must collapse an open row, otherwise move to the parent");
        Require(nav(3, HierarchyNavKey::Right).expand == 3 && nav(1, HierarchyNavKey::Right).select == 2 &&
                    nav(2, HierarchyNavKey::Right).select == 0 && nav(2, HierarchyNavKey::Right).expand == 0,
                "Right must expand a closed row, otherwise move to the first child");
        Require(nav(3, HierarchyNavKey::Home).select == 1 && nav(2, HierarchyNavKey::End).select == 4,
                "Home/End must jump to the first and last rows");
        Require(ui::NavigateHierarchy({}, 1, HierarchyNavKey::Down).select == 0, "Empty trees must not navigate");
    }

    void TestExpansion()
    {
        scene::Scene scene;
        auto *root = Add(scene, "Root");
        auto *branch = Add(scene, "Branch", root);
        auto *leaf = Add(scene, "Leaf", branch);
        ui::HierarchyExpansion expansion;
        expansion.SetSubtreeExpanded(*root, true);
        Require(expansion.IsExpanded(root->GetID()) && expansion.IsExpanded(branch->GetID()) &&
                    !expansion.IsExpanded(leaf->GetID()),
                "Subtree expansion must only record rows that have children");
        expansion.SetSubtreeExpanded(*root, false);
        expansion.ExpandAncestors(*leaf);
        Require(expansion.IsExpanded(root->GetID()) && expansion.IsExpanded(branch->GetID()) &&
                    !expansion.IsExpanded(leaf->GetID()),
                "Revealing an entity must expand exactly its ancestors");
    }

    void TestFilter()
    {
        scene::Scene scene;
        auto *car = Add(scene, "Car");
        auto *wheel = Add(scene, "Front Wheel", car);
        auto *body = Add(scene, "Body", car);
        auto *other = Add(scene, "Lamp");

        ui::HierarchyFilter filter;
        Require(filter.IsVisible(other->GetID()) && !filter.IsMatch(other->GetID()), "An inactive filter must show everything");
        filter.SetQuery("wHeE");
        filter.Refresh(&scene, 1.0);
        Require(filter.IsMatch(wheel->GetID()) && !filter.IsMatch(car->GetID()) && filter.IsVisible(car->GetID()) &&
                    !filter.IsVisible(body->GetID()) && !filter.IsVisible(other->GetID()),
                "Matches must be case-insensitive and keep their ancestors visible");

        body->SetName("Rear Wheel");
        filter.Refresh(&scene, 1.1);
        Require(!filter.IsVisible(body->GetID()), "Results must be cached within the refresh interval");
        filter.Refresh(&scene, 1.0 + ui::HierarchyFilter::kRefreshSeconds);
        Require(filter.IsMatch(body->GetID()), "Results must refresh once the interval elapses");
        other->SetName("Spare Wheel");
        filter.Invalidate();
        filter.Refresh(&scene, 1.3);
        Require(filter.IsMatch(other->GetID()), "Invalidation must force a rebuild");
        filter.SetQuery("nothing");
        filter.Refresh(&scene, 1.3);
        Require(!filter.HasMatches(), "Changing the query must rebuild immediately");
        Require(ui::HierarchyFilter::ContainsCaseInsensitive("Directional Light", "") &&
                    !ui::HierarchyFilter::ContainsCaseInsensitive("Light", "lights"),
                "Substring matching must handle empty and longer queries");
    }

    void TestSelectionAndLocks()
    {
        ui::EntitySelection selection;
        const auto start = selection.Revision();
        selection.Click(7, false, false);
        const auto clicked = selection.Revision();
        selection.Prune([](std::uint32_t) { return true; });
        Require(clicked > start && selection.Revision() == clicked, "Only real selection changes may advance the revision");
        selection.Prune([](std::uint32_t) { return false; });
        Require(selection.Revision() > clicked && selection.Ids().empty(), "Pruning entities must advance the revision");

        scene::Scene scene;
        auto *parent = Add(scene, "Parent");
        auto *child = Add(scene, "Child", parent);
        ui::EntityLocks locks;
        locks.SetLocked(parent->GetID(), true);
        Require(locks.IsLockedInHierarchy(child) && !locks.IsLocked(child->GetID()), "Locks must apply to descendants");
        locks.SetLocked(parent->GetID(), false);
        Require(!locks.IsLockedInHierarchy(child), "Unlocking must release descendants");
    }

    void TestIcons()
    {
        scene::Scene scene;
        auto *empty = Add(scene, "Empty");
        auto *sun = Add(scene, "Sun");
        sun->CreateComponent<scene::LightComponent>()->SetLightType(scene::LightType::Directional);
        auto *lamp = Add(scene, "Lamp");
        lamp->CreateComponent<scene::LightComponent>();
        // Glyphs are compared by content: literal addresses may differ between translation units.
        const auto icon = [](const scene::Entity &entity) { return std::string_view(ui::GetHierarchyEntityIcon(entity)); };
        Require(icon(*empty) == ui::icons::EntityEmpty && icon(*sun) == ui::icons::EntityDirectionalLight &&
                    icon(*lamp) == ui::icons::EntityPointLight,
                "Icons must follow the most specific matching rule");
    }
}

int main()
try
{
    TestSiblingIndex();
    TestDropPlacement();
    TestNavigation();
    TestExpansion();
    TestFilter();
    TestSelectionAndLocks();
    TestIcons();
    std::cout << "Scene hierarchy tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
