#pragma once

#include "PlutoGE/assets/Project.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    struct AssetReferenceOption
    {
        std::string reference;
        std::string displayName;
    };

    // Manifest assets of `type` (project and engine), sorted by display name.
    // Without a project, the engine's built-in assets of that type. Cached
    // until the project's asset manifest changes.
    [[nodiscard]] const std::vector<AssetReferenceOption> &GetCachedAssetReferenceOptions(
        const assets::Project *project, assets::ProjectAssetType type);

    // The project:// reference naming `pathOrReference`, which may already be
    // a reference, an absolute path, or a path relative to the asset
    // directory. Anything outside the asset directory is returned unchanged.
    [[nodiscard]] std::string ToProjectAssetReference(const assets::Project &project, const std::string &pathOrReference);

    struct ProjectAssetPickerOptions
    {
        // Label of the empty choice; null when a value is required.
        const char *noneLabel = "None";
        // Non-asset choices listed first, such as engine default shaders.
        std::span<const AssetReferenceOption> builtinOptions;
        // Further narrows the offered project assets (for example by extension).
        std::function<bool(const AssetReferenceOption &)> filter;
    };

    // The choices a ProjectAssetPicker offers: builtin options, then project
    // assets of `type` that pass the filter. Engine and external files are
    // never offered.
    [[nodiscard]] std::vector<AssetReferenceOption> CollectProjectAssetChoices(
        const assets::Project *project, assets::ProjectAssetType type, const ProjectAssetPickerOptions &options = {});

    // Dropdown restricted to CollectProjectAssetChoices, with search and
    // drag-and-drop from the Content Browser. A current value outside those
    // choices is flagged so it can be replaced, but can never be chosen.
    // Returns true when `reference` changed.
    bool RenderProjectAssetPicker(const char *label, const assets::Project *project, assets::ProjectAssetType type,
                                  std::string &reference, const ProjectAssetPickerOptions &options = {});
}
