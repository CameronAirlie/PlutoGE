#include "PlutoGE/render/ScenePortrait.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <iostream>
#include <stdexcept>

int main() try
{
    using namespace PlutoGE;
    auto require = [](bool value, const char *message) { if (!value) throw std::runtime_error(message); };
    render::MeshConfig config;
    config.data.vertices.resize(3);
    config.data.vertices[1].position = {1, 0, 0};
    config.data.vertices[2].position = {0, 1, 0};
    for (auto &vertex : config.data.vertices) vertex.weights = {1, 0, 0, 0};
    config.data.indices = {0, 1, 2};
    config.skeleton.joints.push_back({.name="hand", .nodeIndex=0});
    config.animationNodes.push_back({.name="hand"});
    render::AnimationChannel channel;
    channel.jointIndex = channel.nodeIndex = 0;
    channel.targetName = "hand";
    channel.times = {0, 1};
    channel.values = {glm::vec4(0, -2, 0, 0), glm::vec4(0, -2, 0, 0)};
    render::AnimationClip idle;
    idle.name = "Idle"; idle.duration = 1; idle.channelCount = 1; idle.channels = {channel};
    auto attack = idle; attack.name = "Attack";
    attack.channels.front().values = {glm::vec4(3, 0, 0, 0), glm::vec4(3, 0, 0, 0)};
    config.animations = {attack, idle};
    render::Mesh body(config);
    config.animations.clear();
    render::Mesh armour(config);
    scene::Scene scene;
    auto *root = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name="Player"}));
    auto *bodyComponent = root->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh=&body});
    auto *live = root->CreateComponent<scene::AnimationComponent>();
    live->SetClipsFromImportedAnimations(body.GetAnimations());
    live->Play("Attack"); live->SetTime(.4f); live->SetPlaying(false);
    auto *child = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name="Armour"}), root);
    child->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh=&armour});
    const auto before = live->GetJointMatrices(body.GetSkeleton(), body.GetAnimationNodes());
    auto snapshot = render::BuildScenePortraitSnapshot(scene, root->GetID(), {});
    require(snapshot.draws.size() == 2, "Body and armour must both appear");
    for (const auto &draw : snapshot.draws)
    {
        require(draw.jointMatrices && draw.jointMatrices->size() == 1, "Portrait omitted skin palette");
        const auto position = draw.jointMatrices->front() * glm::vec4(0,0,0,1);
        require(glm::length(position - glm::vec4(0,-2,0,1)) < .0001f, "Portrait did not sample Idle");
    }
    require(live->GetCurrentClipIndex() == 0 && live->GetTime() == .4f && !live->IsPlaying(), "Portrait altered live playback");
    require(live->GetJointMatrices(body.GetSkeleton(), body.GetAnimationNodes()) == before, "Portrait altered live pose");
    bodyComponent->SetVisible(false);
    snapshot = render::BuildScenePortraitSnapshot(scene, root->GetID(), {});
    require(snapshot.draws.size() == 1 && snapshot.draws.front().jointMatrices, "Hidden skeleton owner must still supply child pose");
    auto *detached = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name="Detached wearable"}));
    detached->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh=&armour});
    const std::array attachments{detached->GetID()};
    snapshot = render::BuildScenePortraitSnapshot(scene, root->GetID(), attachments);
    require(snapshot.draws.size() == 2 && snapshot.draws.back().jointMatrices->front()[3].y == -2,
            "Detached wearable did not inherit portrait Idle");
    std::cout << "Portrait body and armour use Idle without changing paused gameplay animation\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
