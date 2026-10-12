#include "PlutoGE/ui/AssetReferencePicker.h"

#include <algorithm>
#include <array>
#include <chrono>
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
        Require(ui::ToProjectAssetReference(project, "asset://model#7") == "asset://model#7",
                "Stable identities must never be treated as relative filesystem paths");
    }

    void CatalogBackedImportedChoices()
    {
        const auto project = MakeProject(std::filesystem::temp_directory_path() / "PlutoGE-picker-catalog");
        assets::AssetCatalog catalog;
        Require(catalog.Replace({
            {{"model", 7}, ProjectAssetType::Mesh, assets::AssetOwnership::Imported, "Body", "project://Models/Body.plutomesh"},
            {{"model", 8}, ProjectAssetType::Material, assets::AssetOwnership::Imported, "Paint", "project://Models/Paint.plutomaterial"},
            {{"texture", 0}, ProjectAssetType::Texture, assets::AssetOwnership::Authored, "Monitor", "project://Textures/Monitor.plutorendertexture"}
        }), "Catalog fixture must be valid");
        const auto meshes = ui::CollectProjectAssetChoices(&project, ProjectAssetType::Mesh, {.catalog = &catalog});
        Require(meshes.size() == 1 && meshes[0].displayName == "Models/Body.plutomesh",
                "Library-backed mesh choices must not require physical Assets files or manifest entries");
        Require(ui::ResolveAssetPickerReference(&catalog, "asset://model#7") == meshes[0].reference,
                "Stable IDs and virtual locations must select the same mesh");
        Require(ui::CollectProjectAssetChoices(&project, ProjectAssetType::Material, {.catalog = &catalog}).size() == 1,
                "Imported materials must be offered independently of meshes");
        const auto filtered = ui::CollectProjectAssetChoices(&project, ProjectAssetType::Texture,
            {.filter = [](const auto &option) { return option.reference.ends_with(".plutorendertexture"); }, .catalog = &catalog});
        Require(filtered.size() == 1, "Path-based type filters must work for catalog choices");
        Require(ui::CollectProjectAssetChoices(&project, ProjectAssetType::Texture, {.catalog = &catalog}).size() == 1,
                "A current catalog must supersede stale manifest choices");
        Require(ui::ResolveAssetPickerReference(&catalog, "asset://missing#7") == "asset://missing#7",
                "Unknown identities must stay unresolved rather than matching an unrelated asset");
        Require(catalog.Replace({{{"model", 7}, ProjectAssetType::Mesh, assets::AssetOwnership::Imported,
                                 "Renamed", "project://Models/Renamed.plutomesh"}}), "Replacement catalog must be valid");
        Require(ui::ResolveAssetPickerReference(&catalog, "asset://model#7") == "project://Models/Renamed.plutomesh",
                "Reimport and rename must use the replacement snapshot without stale picker caching");
        Require(ui::CollectProjectAssetChoices(&project, ProjectAssetType::Material, {.catalog = &catalog}).empty(),
                "Retired imported objects must disappear from choices");
    }

    void ClosedPickerLookup()
    {
        const auto project = MakeProject(std::filesystem::temp_directory_path() / "PlutoGE-picker-lookup");
        const std::array types{ProjectAssetType::Texture, ProjectAssetType::ShaderGraph, ProjectAssetType::Material};
        const std::array builtins{ui::AssetReferenceOption{"custom-default", "Custom Default"}};
        const ui::ProjectAssetPickerOptions options{.includeEngineAssets = true, .builtinOptions = builtins};
        for (const auto &choice : ui::CollectProjectAssetChoices(&project, types, options))
        {
            const auto found = ui::FindProjectAssetChoice(&project, types, choice.reference, options);
            Require(found && found->displayName == choice.displayName, "Closed preview must match the open list");
        }
        Require(!ui::FindProjectAssetChoice(&project, types, "project://Missing.png", options), "Missing asset accepted");
        Require(!ui::FindProjectAssetChoice(&project, types, "C:/outside.png", options), "External asset accepted");
        Require(!ui::FindProjectAssetChoice(&project, types, "project://Textures/Brick.png",
            {.filter = [](const auto &option) { return option.reference.ends_with(".plutorendertexture"); }}), "Filter ignored");

        assets::AssetCatalog catalog;
        std::vector<assets::AssetObjectDescriptor> objects;
        for (int i = 0; i < 5000; ++i)
            objects.push_back({{"texture" + std::to_string(i), 0}, ProjectAssetType::Texture, assets::AssetOwnership::Authored,
                "Texture", "project://Textures/" + std::to_string(i) + ".png"});
        Require(catalog.Replace(std::move(objects)), "Large catalog fixture failed");
        int filtered = 0;
        const ui::ProjectAssetPickerOptions indexed{.filter = [&](const auto &) { ++filtered; return true; }, .catalog = &catalog};
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i)
            Require(ui::FindProjectAssetChoice(&project, types, "asset://texture2500#0", indexed).has_value(), "Indexed identity lookup failed");
        Require(filtered == 1000, "Closed pickers evaluated filters for unrelated assets");
        const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "1000 closed picker lookups over 5000 assets: " << milliseconds << " ms\n";
        Require(!ui::FindProjectAssetChoice(&project, types, "project://Textures/Brick.png", {.catalog = &catalog}),
            "Closed picker used a stale manifest asset");
        Require(catalog.Replace({}), "Could not clear catalog");
        Require(!ui::FindProjectAssetChoice(&project, types, "asset://texture2500#0", {.catalog = &catalog}),
            "Retired identity remained selectable");
    }
}

int main()
{
    try
    {
        OnlyProjectAssetsAreOffered();
        EngineAssetsAndMultipleTypes();
        ReferencesNormalizeToTheProject();
        CatalogBackedImportedChoices();
        ClosedPickerLookup();
        std::cout << "PASS: material asset pickers offer and normalize project assets only\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
