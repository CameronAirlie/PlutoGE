#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace PlutoGE::scene
{
    // Making a wearable (docs/SKELETAL_IK.md, "Clothing and outfits") from a skinned model: a prefab whose root holds a
    // WearableComponent and the model's mesh, set to follow its parent's skeleton. The editor's Content Browser
    // "Create Wearable" action writes one; an OutfitComponent puts it on a character.

    // The slot a model's name suggests ("Leather_Boots" -> "Feet"), or "Body".
    [[nodiscard]] std::string GuessWearableSlot(std::string_view name);

    // Writes the prefab for `meshReference` (a model or mesh asset reference) to `path`, its root named `name`.
    bool SaveWearablePrefab(const std::string &meshReference, const std::string &name, const std::filesystem::path &path,
                            std::string *errorMessage = nullptr);
}
