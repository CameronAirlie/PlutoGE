#pragma once
#include "PlutoGE/assets/SceneFormat.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/ui/SceneHistory.h"
#include <algorithm>
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"

namespace PlutoGE::ui
{
    inline SceneGenerationRetention CaptureSceneGenerationRetention(const scene::Scene &scene)
    {
        SceneGenerationRetention retained;
        for (const auto &[root, instance] : scene.GetStaticModelInstances())
        {
            if (!instance.resources) continue;
            const auto catalog = instance.resources->GetAssetCatalog();
            const auto storage = instance.resources->GetAssetStorageMap();
            assets::AssetReference identity;
            if (!catalog || !storage || !assets::ParseAssetReference(instance.state.accepted.layout.meshReference, identity)) continue;
            const auto *object = catalog->Find(identity);
            const auto *entry = object ? storage->Find(object->location) : nullptr;
            if (entry && entry->generationLease && std::find(retained.begin(), retained.end(), entry->generationLease) == retained.end())
                retained.push_back(entry->generationLease);
        }
        return retained;
    }
    // Normal scene opening can salvage damaged files. History/recovery must not
    // silently replace the user's scene with a partially deserialized snapshot.
    inline std::unique_ptr<scene::Scene> LoadSceneSnapshot(const std::string &state, std::string &error)
    {
        error.clear();
        if (!assets::SceneFormatVersion(state))
        {
            error = "Invalid or unsupported scene snapshot header.";
            return nullptr;
        }
        auto restored = scene::SceneSerializer::LoadFromString(state, &error);
        if (!error.empty()) return nullptr;
        return restored;
    }
}
