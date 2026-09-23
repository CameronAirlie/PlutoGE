#include "PlutoGE/scene/Scene.h"
#include "RhiDrawPreparationCache.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;
void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
bool Near(const glm::mat4 &a, const glm::mat4 &b)
{
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (std::abs(a[c][r] - b[c][r]) > 0.0001f) return false;
    return true;
}
int main() try
{
    // Cached decomposition must follow ancestor edits, reparenting, reflection,
    // and singular scales, even when the matrix was read before decomposition.
    scene::Entity parent, other, child;
    parent.AddChild(&child);
    parent.SetActive(false);
    Require(!child.IsActive() && !child.IsActiveInHierarchy() && child.IsSelfActive(), "Ancestor activity");
    parent.SetActive(true);
    Require(child.IsActiveInHierarchy(), "Ancestor reactivation");
    child.SetScale({2, 3, 4});
    Require(child.GetWorldScale() == glm::vec3(2, 3, 4), "Initial scale");
    parent.SetScale({-2, 2, 2});
    (void)child.GetWorldTransform();
    Require(child.GetWorldScale() == glm::vec3(-4, 6, 8), "Ancestor scale invalidation");
    other.SetScale({3, 3, 3});
    child.SetParent(&other);
    Require(child.GetWorldScale() == glm::vec3(6, 9, 12), "Reparent invalidation");
    other.SetRotation({0, 30, 0});
    const auto rotation = child.GetWorldRotation();
    Require(std::abs(rotation.y - 30) < .001f, "Rotation invalidation");
    child.SetScale({0, 3, 4});
    Require(child.GetWorldScale().x == 0, "Singular scale");
    child.SetParent(nullptr);

    // Shuffled import order, rotation interpolation, nonuniform reflected TRS,
    // repeat sampling and time reversal agree with the original matrix formula.
    scene::AnimationComponent animation;
    std::vector<render::AnimationNode> nodes(3);
    nodes[0].parentNodeIndex = 2;
    nodes[1].parentNodeIndex = -1;
    nodes[2].parentNodeIndex = 1;
    nodes[2].localBindTransform = glm::translate(glm::mat4(1), {0, 3, 0});
    render::AnimationClip clip;
    clip.name = "test"; clip.duration = 1;
    clip.channels.push_back({.nodeIndex = 0, .path = render::AnimationTargetPath::Translation,
        .times = {0, 1}, .values = {{0, 0, 0, 0}, {4, 2, 0, 0}}});
    const glm::quat end = glm::angleAxis(.8f, glm::vec3(0, 1, 0));
    clip.channels.push_back({.nodeIndex = 0, .path = render::AnimationTargetPath::Rotation,
        .times = {0, 1}, .values = {{0, 0, 0, 1}, {end.x, end.y, end.z, end.w}}});
    clip.channels.push_back({.nodeIndex = 0, .path = render::AnimationTargetPath::Scale,
        .times = {0}, .values = {{-2, 3, 4, 0}}});
    animation.SetClipsFromImportedAnimations({clip});
    animation.SetEditorPreviewMode(true);
    for (float time : {0.f, .25f, .8f, .1f, .5f})
    {
        animation.SetTime(time);
        const auto expected = nodes[2].localBindTransform *
            glm::translate(glm::mat4(1), glm::vec3(4, 2, 0) * time) *
            glm::mat4_cast(glm::normalize(glm::slerp(glm::quat(1,0,0,0), end, time))) *
            glm::scale(glm::mat4(1), glm::vec3(-2, 3, 4));
        Require(Near(animation.GetNodeMatrix(nodes, 0), expected), "Scheduled animation differs from reference");
    }

    // A changing actor must not evict stationary packets. Slot retention is
    // not authorization to reuse stale transforms/material/LOD/history.
    render::RhiDrawPreparationCache::List packets;
    std::vector<render::RenderCommand> commands(3);
    commands[1].model[3].x = 3;
    commands[2].submeshIndex = 1;
    const auto revision = [](const auto &) { return uint64_t{7}; };
    packets.Reconcile(commands, revision);
    render::BasicDraw draw;
    for (size_t i = 0; i < commands.size(); ++i)
        packets.entries[i].Store(commands[i], 7, &draw);
    auto *storage = packets.entries.data();
    commands[2].model[3].x = 5;
    packets.Reconcile(commands, revision);
    Require(packets.entries.data() == storage && packets.entries[0].Matches(commands[0], 7), "Stable packets were evicted");
    Require(!packets.entries[2].Matches(commands[2], 7), "Moved packet reused");
    std::swap(commands[0], commands[1]);
    packets.Reconcile(commands, revision);
    Require(!packets.entries[0].Matches(commands[0], 7), "Repeated-mesh reorder reused stale transform");
    Require(!packets.entries[1].Matches(commands[1], 8), "Material revision ignored");
    commands[0].previousModel[3].y = 4;
    Require(!packets.entries[0].Matches(commands[0], 7), "Motion history ignored");
    commands[0].lodIndex = 1;
    Require(!packets.entries[0].Matches(commands[0], 7), "LOD ignored");
    commands.resize(1);
    packets.Reconcile(commands, revision);
    Require(packets.entries.size() == 1, "Membership removal ignored");

    scene::Scene scene;
    auto add = [&](float z) {
        auto entity = std::make_unique<scene::Entity>();
        entity->SetPosition({0, 0, z});
        entity->CreateComponent<scene::ColliderComponent>();
        return scene.AddEntity(std::move(entity));
    };
    auto *far = add(0);
    scene::PhysicsRaycastHit hit;
    auto hitIs = [&](scene::Entity *entity) {
        scene.SynchronizePhysicsQueries();
        return scene.Raycast({0, 0, 10}, {0, 0, -1}, 20, hit) && hit.entityId == entity->GetID();
    };
    Require(hitIs(far), "Initial query");
    auto *near = add(5);
    Require(hitIs(near), "Added collider query");
    near->SetActive(false);
    Require(hitIs(far), "Deactivated collider retained in query world");
    near->SetActive(true);
    Require(hitIs(near), "Reactivated collider missing");
    near->GetComponent<scene::ColliderComponent>()->SetCenter({10, 0, 0});
    Require(hitIs(far), "Changed shape retained stale proxy");
    near->GetComponent<scene::ColliderComponent>()->SetCenter({0, 0, 0});
    Require(hitIs(near), "Restored shape missing");
    scene.DestroyEntity(near->GetID());
    Require(hitIs(far), "Pending deletion query");
    scene.RemoveEntity(near);
    Require(hitIs(far), "Destroyed collider left dangling reference");
    auto *invalid = add(4);
    invalid->GetComponent<scene::ColliderComponent>()->SetShape(scene::ColliderShape::Mesh);
    Require(hitIs(far) && hitIs(far), "Empty shape membership");
    invalid->GetComponent<scene::ColliderComponent>()->SetShape(scene::ColliderShape::Box);
    Require(hitIs(invalid), "Empty shape becoming valid");
    std::cout << "Scene CPU cache checks passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
