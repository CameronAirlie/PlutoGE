#include "PlutoGE/scene/WearablePrefab.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/WearableComponent.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace PlutoGE::scene
{
    std::string GuessWearableSlot(std::string_view name)
    {
        std::string lower(name);
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // First match wins: "shoulder cape" is Back, not Body.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 20> keywords{{
            {"hood", "Head"}, {"hat", "Head"}, {"helm", "Head"}, {"cap", "Head"}, {"hair", "Head"},
            {"mask", "Face"}, {"glass", "Face"},
            {"scarf", "Neck"}, {"necklace", "Neck"},
            {"glove", "Hands"}, {"gauntlet", "Hands"},
            {"boot", "Feet"}, {"shoe", "Feet"}, {"sock", "Feet"},
            {"legging", "Legs"}, {"trouser", "Legs"}, {"pant", "Legs"}, {"skirt", "Legs"},
            {"backpack", "Back"}, {"cape", "Back"},
        }};
        for (const auto &[keyword, slot] : keywords)
            if (lower.find(keyword) != std::string::npos)
                return std::string(slot);
        return "Body";
    }

    bool SaveWearablePrefab(const std::string &meshReference, const std::string &name, const std::filesystem::path &path,
                            std::string *errorMessage)
    {
        Scene authoring;
        auto *root = authoring.AddEntity(std::make_unique<Entity>(EntityConfig{.name = name}));
        root->CreateComponent<WearableComponent>()->SetSlot(GuessWearableSlot(name));
        auto *mesh = root->CreateComponent<MeshComponent>(MeshComponentConfig{});
        mesh->Deserialize({{"MeshAssetReference", PropertyType::String, meshReference}});
        mesh->SetFollowParentSkeleton(true);
        if (!mesh->GetMesh() || !mesh->GetMesh()->HasSkeleton())
        {
            if (errorMessage)
                *errorMessage = "'" + meshReference + "' has no skinned mesh: a wearable must be rigged to the character's armature.";
            return false;
        }
        return Prefab::SaveFromEntity(*root, path, errorMessage);
    }
}
