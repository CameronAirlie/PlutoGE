#include "PlutoGE/ui/AssetReferencePicker.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE;
    using assets::ProjectAssetType;

    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    assets::Project MakeProject(const std::filesystem::path &root)
    {
        assets::ProjectManifest manifest;
        manifest.assetEntries = {
            {.reference = "project://Textures/Brick.png", .type = ProjectAssetType::Texture},
            {.reference = "project://Textures/Monitor.plutorendertexture", .type = ProjectAssetType::Texture},
            {.reference = "engine://textures/checker.png", .type = ProjectAssetType::Texture},
            {.reference = "project://Shaders/Water.plutoshadergraph", .type = ProjectAssetType::ShaderGraph},
        };
        return assets::Project(root / "Game.plutoproject", manifest);
    }

    void OnlyProjectAssetsAreOffered()
    {
        const auto root = std::filesystem::temp_directory_path() / "PlutoGE-picker-tests";
        const auto project = MakeProject(root);

        const auto textures = ui::CollectProjectAssetChoices(&project, ProjectAssetType::Texture);
        Require(textures.size() == 2, "Only the project's textures may be offered");
        for (const auto &choice : textures)
            Require(assets::Project::IsProjectAssetReference(choice.reference), "Offered " + choice.reference);

        const auto renderTextures = ui::CollectProjectAssetChoices(
            &project, ProjectAssetType::Texture,
            {.filter = [](const ui::AssetReferenceOption &option) { return option.reference.ends_with(".plutorendertexture"); }});
        Require(renderTextures.size() == 1 && renderTextures[0].displayName == "Textures/Monitor.plutorendertexture",
                "Filters must narrow choices and display project-relative names");

        const std::array builtins{ui::AssetReferenceOption{"engine://shaders/default.plutoshadergraph", "Default Lit"}};
        const auto shaders = ui::CollectProjectAssetChoices(&project, ProjectAssetType::ShaderGraph,
                                                            {.builtinOptions = builtins});
        Require(shaders.size() == 2 && shaders[0].displayName == "Default Lit" &&
                    shaders[1].reference == "project://Shaders/Water.plutoshadergraph",
                "Builtin choices come first, followed by project assets of the type");

        Require(ui::CollectProjectAssetChoices(nullptr, ProjectAssetType::Texture).empty(),
                "Without a project, no texture can be chosen");
    }

    void EngineAssetsAndMultipleTypes()
    {
        const auto root = std::filesystem::temp_directory_path() / "PlutoGE-picker-tests";
        const auto project = MakeProject(root);
        const auto has = [](const std::vector<ui::AssetReferenceOption> &choices, std::string_view reference) {
            return std::ranges::any_of(choices, [&](const auto &choice) { return choice.reference == reference; });
        };

        const auto withEngine = ui::CollectProjectAssetChoices(&project, ProjectAssetType::Texture, {.includeEngineAssets = true});
        Require(withEngine.size() == 3 && has(withEngine, "engine://textures/checker.png"),
                "Engine textures listed by the manifest must be offered when requested");

        // Built-ins remain selectable even when the manifest omits them.
        const std::string defaultMaterial(assets::Project::kBuiltinDefaultShadedMaterialReference);
        Require(has(ui::CollectProjectAssetChoices(&project, ProjectAssetType::Material, {.includeEngineAssets = true}), defaultMaterial),
                "Engine default materials must be offered when engine assets are allowed");
        Require(!has(ui::CollectProjectAssetChoices(&project, ProjectAssetType::Material), defaultMaterial),
                "Engine assets must not be offered by default");
        Require(has(ui::CollectProjectAssetChoices(nullptr, ProjectAssetType::Material, {.includeEngineAssets = true}), defaultMaterial),
                "Engine built-ins must be offered without a project");

        const std::array types{ProjectAssetType::ShaderGraph, ProjectAssetType::Texture};
        const auto mixed = ui::CollectProjectAssetChoices(&project, types);
        Require(mixed.size() == 3 && std::ranges::is_sorted(mixed, {}, &ui::AssetReferenceOption::displayName),
                "Several asset types must be merged into one sorted list");
    }

    void ReferencesNormalizeToTheProject()
    {
        const auto root = std::filesystem::temp_directory_path() / "PlutoGE-picker-tests";
        const auto project = MakeProject(root);
        const auto assetsDirectory = project.GetAssetDirectoryPath();

        Require(ui::ToProjectAssetReference(project, (assetsDirectory / "Textures" / "Brick.png").string()) ==
                    "project://Textures/Brick.png",
                "Absolute paths inside the project must become project references");
        Require(ui::ToProjectAssetReference(project, "Textures/Brick.png") == "project://Textures/Brick.png",
                "Paths persisted relative to the asset directory must become project references");
        Require(ui::ToProjectAssetReference(project, "project://Textures/Brick.png") == "project://Textures/Brick.png",
                "Project references must be unchanged");

        const auto outside = (root.parent_path() / "Elsewhere" / "Brick.png").string();
        Require(ui::ToProjectAssetReference(project, outside) == outside,
                "Files outside the project must stay as-is so the editor can flag them");
        Require(ui::ToProjectAssetReference(project, "").empty(), "An empty slot must stay empty");
    }
}

int main()
{
    try
    {
        OnlyProjectAssetsAreOffered();
        EngineAssetsAndMultipleTypes();
        ReferencesNormalizeToTheProject();
        std::cout << "PASS: material asset pickers offer and normalize project assets only\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
