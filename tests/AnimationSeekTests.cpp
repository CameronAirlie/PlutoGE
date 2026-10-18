#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() try
{
    using namespace PlutoGE;
    using namespace PlutoGE::scene;
    Scene scene;
    auto *entity = scene.AddEntity(std::make_unique<Entity>());
    auto *animation = entity->CreateComponent<AnimationComponent>();
    animation->SetClipsFromImportedAnimations({render::AnimationClip{.name = "Climb", .duration = 2}});
    animation->GetGraphStates()[0].loop = false;
    animation->SetCurrentStateIndex(0);
    animation->SetSpeed(0);
    animation->Play();
    const auto near = [&](float expected)
    { if (std::abs(animation->GetTime() - expected) > .0001f) throw std::runtime_error("Graph time did not preserve seek"); };
    animation->Seek(1.2f);
    animation->Update(.1f);
    near(1.2f);
    animation->SetSpeed(1);
    animation->Update(.1f);
    near(1.3f);
    animation->Seek(std::numeric_limits<float>::quiet_NaN());
    near(1.3f);
    animation->Seek(8);
    near(2);
    animation->GetGraphStates()[0].loop = true;
    animation->Seek(-.25f);
    near(1.75f);
    animation->Seek(2.25f);
    animation->SetSpeed(0);
    animation->Update(.1f);
    near(.25f);
    animation->SetClipsFromImportedAnimations({
        render::AnimationClip{.name = "Source", .duration = 2},
        render::AnimationClip{.name = "Target", .duration = 3}});
    animation->GetGraphStates().clear();
    const int source = animation->AddState("Source", 0);
    const int target = animation->AddState("Target", 1);
    animation->SetCurrentStateIndex(source);
    animation->Play();
    animation->SetSpeed(0);
    if (!animation->CrossFadeState("Target", .16f, .5f)) throw std::runtime_error("Crossfade rejected valid state");
    near(.5f);
    animation->Update(.05f);
    animation->Seek(.6f, true);
    if (animation->GetCurrentStateIndex() != source) throw std::runtime_error("Seeking committed entry blend");
    near(.6f);
    animation->Update(.05f);
    animation->Seek(.7f, true);
    animation->Update(.07f);
    if (animation->GetCurrentStateIndex() != target) throw std::runtime_error("Entry blend did not finish while pose clock was held");
    near(.7f);
    if (animation->CrossFadeState("Missing", .16f) || animation->CrossFadeState("Source", -1))
        throw std::runtime_error("Invalid crossfade accepted");
    if (!animation->CrossFadeState("Source", 0, .25f) || animation->GetCurrentStateIndex() != source)
        throw std::runtime_error("Zero-duration crossfade failed");
    near(.25f);
    animation->CrossFadeState("Target", .16f, .5f);
    animation->Seek(1.0f);
    if (animation->GetCurrentStateIndex() != target) throw std::runtime_error("Ordinary seek no longer commits transitions");
    near(1.0f);
    std::cout << "Animation graph seek checks passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
