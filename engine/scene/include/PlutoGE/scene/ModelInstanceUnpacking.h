#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
namespace PlutoGE::assets { class AssetManager; }
namespace PlutoGE::scene
{
    class Scene;
    // Prepare an isolated scene after an exact accepted bundle has been extracted
    // and its authored catalog published. Live scene/history remain unchanged.
    // Preserves entity IDs, exact transforms and authored component overrides.
    bool PrepareStaticModelInstanceUnpacking(const Scene &source, std::uint32_t rootEntityId,
        const std::unordered_map<std::string, std::string> &authoredReferences,
        assets::AssetManager &sharedAssets, std::unique_ptr<Scene> &output,
        std::string *errorMessage = nullptr);
}
