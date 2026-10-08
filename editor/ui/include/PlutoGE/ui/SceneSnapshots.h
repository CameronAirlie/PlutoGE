#pragma once
#include "PlutoGE/assets/SceneFormat.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"

namespace PlutoGE::ui
{
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
