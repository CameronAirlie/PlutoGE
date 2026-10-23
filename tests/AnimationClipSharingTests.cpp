#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    render::AnimationClip MakeClip()
    {
        render::AnimationChannel channel;
        channel.targetName = "Hips";
        channel.nodeIndex = 0;
        channel.path = render::AnimationTargetPath::Translation;
        channel.times = {0.0f, 1.0f};
        channel.values = {glm::vec4(0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)};
        render::AnimationClip clip{.name = "Walk", .duration = 1.0f, .channelCount = 1};
        clip.channels.push_back(channel);
        return clip;
    }

    // Copies read one allocation; a write detaches only the copy that writes.
    void CopiesShareUntilWritten()
    {
        const auto original = MakeClip();
        auto copy = original;
        Require(copy.channels.SharesStorageWith(original.channels), "Copying a clip copied its channels");
        Require(copy.channels[0].times.size() == 2 && copy.channels.size() == 1, "Shared channels read wrong");
        copy.channels.Mutable()[0].values[1].x = 5.0f;
        Require(!copy.channels.SharesStorageWith(original.channels), "Writing a shared clip did not detach it");
        Require(original.channels[0].values[1].x == 1.0f, "Writing a copy changed the original");
        Require(copy.channels[0].values[1].x == 5.0f, "Write to the detached copy was lost");
    }

    // The clip cache hands every loader the same channels, and a loader's writes never reach the cache.
    void CacheSharesLoadedClips()
    {
        const auto root = std::filesystem::temp_directory_path() / "PlutoGEAnimationClipSharingTests";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "Assets");
        assets::AssetManager manager;
        manager.SetProjectContext(root.string());
        std::string error;
        Require(manager.SaveAnimationClipAsset("project://Walk.plutoclip", MakeClip(), &error), "Saving the clip: " + error);
        render::AnimationClip first, second;
        Require(manager.LoadAnimationClipAsset("project://Walk.plutoclip", first) &&
                    manager.LoadAnimationClipAsset("project://Walk.plutoclip", second), "Loading the clip failed");
        Require(first.channels.size() == 1, "Loaded clip lost its channel");
        Require(first.channels.SharesStorageWith(second.channels), "Two loads of one clip copied its channels");
        second.channels.Mutable()[0].times[1] = 2.0f;
        render::AnimationClip third;
        Require(manager.LoadAnimationClipAsset("project://Walk.plutoclip", third) && third.channels[0].times[1] == 1.0f,
                "Writing a loaded clip changed the cached clip");
        std::filesystem::remove_all(root);
    }

    // Playing a clip must not take a private copy of it (spawning a character with a large graph did).
    void PlaybackKeepsClipsShared()
    {
        const auto source = MakeClip();
        Scene scene;
        auto *owner = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Walker"}));
        auto *animation = owner->CreateComponent<AnimationComponent>();
        animation->SetClipsFromImportedAnimations({source});
        for (int frame = 0; frame < 5; ++frame)
            animation->Update(0.1f);
        Require(animation->GetClips()[0].channels.SharesStorageWith(source.channels), "Playback copied the clip's channels");
    }
}

int main()
{
    try
    {
        CopiesShareUntilWritten();
        CacheSharesLoadedClips();
        PlaybackKeepsClipsShared();
        std::cout << "Animation clip sharing tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
