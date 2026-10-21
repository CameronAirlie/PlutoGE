#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
}

// Audio occlusion as the audio update measures it: walls between a listener and a sound muffle it, while non-blocking
// colliders, triggers and the surface a sound plays on (a footstep, a bullet impact) do not.
int main()
{
    try
    {
        using namespace PlutoGE;
        scene::Scene scene;
        auto *wall = scene.AddEntity(std::make_unique<scene::Entity>());
        wall->SetPosition({0.0f, 1.0f, -5.0f});
        auto *collider = wall->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.size = {4.0f, 2.0f, 0.2f}});
        scene.SynchronizePhysicsQueries();

        const glm::vec3 listener{0.0f, 1.0f, 0.0f}, forward{0.0f, 0.0f, -1.0f}, up{0.0f, 1.0f, 0.0f};
        const auto occlusion = [&](const glm::vec3 &emitter) { return scene.ComputeAudioOcclusion(listener, forward, up, emitter); };

        Require(occlusion({0.0f, 1.0f, -4.0f}) == 0.0f, "A sound in front of the wall was occluded");
        const float behind = occlusion({0.0f, 1.0f, -8.0f});
        Require(behind > 0.5f, "A sound behind the wall was not occluded (" + std::to_string(behind) + ")");
        // The wall's face is at z = -4.9: sounds playing on it are not blocked by it.
        Require(occlusion({0.0f, 1.0f, -4.85f}) == 0.0f, "A sound on the wall's near face was occluded by the wall");
        Require(occlusion({0.0f, 1.0f, -4.95f}) == 0.0f, "A sound just inside the wall's surface was occluded by it");
        Require(occlusion({0.0f, 1.0f, -5.6f}) > 0.5f, "A sound just behind the wall was not occluded");
        // Emitter and listener hierarchies never block their own sound.
        Require(scene.ComputeAudioOcclusion(listener, forward, up, {0.0f, 1.0f, -8.0f}, wall->GetID()) == 0.0f,
                "The emitter's own collider occluded it");

        collider->SetBlocksAudio(false);
        scene.SynchronizePhysicsQueries();
        Require(occlusion({0.0f, 1.0f, -8.0f}) == 0.0f, "A collider with Blocks Audio off occluded a sound");
        collider->SetBlocksAudio(true);
        collider->SetTrigger(true);
        scene.SynchronizePhysicsQueries();
        Require(occlusion({0.0f, 1.0f, -8.0f}) == 0.0f, "A trigger occluded a sound");
        std::cout << "Audio occlusion tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
