#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/platform/Window.h"
#include "EmissionMaterialChecks.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() try
{
    using namespace PlutoGE;
    const auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    platform::Window window;
    require(window.Create({.title="Material reload test", .width=64, .height=64, .visible=false}) &&
        window.EnsureOpenGLContextCurrent(true), "Could not create material shader context");
    const auto root = std::filesystem::temp_directory_path() /
        ("plutoge-material-reload-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = root / "Assets" / "interior.plutomaterial";
    std::filesystem::create_directories(path.parent_path());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path.parent_path().parent_path(), ec); }
    } cleanup{path};
    const auto write = [&](const char *mode, float cutoff) {
        std::ofstream file(path);
        file << "Color=1,1,1,1\nSurfaceType=Standard\nAlphaMode=" << mode << "\nAlphaCutoff=" << cutoff
             << "\nShaderGraph=engine://builtin/shadergraph/default-lit\n";
    };
    assets::AssetManager assets;
    assets.SetProjectContext(root.string());
    constexpr auto reference = "project://interior.plutomaterial";
    write("Blend", .5f);
    auto *material = assets.LoadMaterialAsset(reference);
    require(material && material->GetConfig().alphaMode == render::AlphaMode::Blend, "Failed to load blended material");
    render::Material uniqueOverride(material->GetConfig());
    auto *builtin = assets.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
    write("Mask", .4f);
    // This is the stale state encountered when a scene is reopened: ordinary
    // asset resolution deliberately reuses its cache until explicitly refreshed.
    require(assets.LoadMaterialAsset(reference) == material && material->GetConfig().alphaMode == render::AlphaMode::Blend,
        "Ordinary material cache lookup changed unexpectedly");
    assets.ReloadMaterialAssets();
    require(assets.LoadMaterialAsset(reference) == material && assets.LoadMaterialAsset(path.string()) == material,
        "Reload invalidated scene references or split the material cache");
    require(material->GetConfig().alphaMode == render::AlphaMode::Mask && material->GetConfig().alphaCutoff == .4f,
        "Scene material reload retained stale blended opacity");
    require(uniqueOverride.GetConfig().alphaMode == render::AlphaMode::Blend,
        "Reload overwrote a scene-owned material override");
    require(assets.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)) == builtin,
        "Reload replaced a built-in material");
    write("Opaque", .5f);
    assets.ReloadMaterialAssets();
    require(material->GetConfig().alphaMode == render::AlphaMode::Opaque, "Second reload retained stale cutout mode");
    std::filesystem::remove(path);
    assets.ReloadMaterialAssets();
    require(assets.LoadMaterialAsset(reference) == material && material->GetConfig().alphaMode == render::AlphaMode::Opaque,
        "A missing material file invalidated the last loaded material");
    const auto generationA = root / "Library" / "A" / "interior.plutomaterial";
    const auto generationB = root / "Library" / "B" / "interior.plutomaterial";
    for (const auto &generation : {generationA, generationB})
        std::filesystem::create_directories(generation.parent_path());
    { std::ofstream file(generationA); file << "Color=1,1,1,1\nAlphaMode=Blend\n"; }
    { std::ofstream file(generationB); file << "Color=1,1,1,1\nAlphaMode=Mask\nAlphaCutoff=0.3\n"; }
    auto catalog = std::make_shared<assets::AssetCatalog>();
    require(catalog->Replace({{{"material-owner", 1}, assets::ProjectAssetType::Material,
        assets::AssetOwnership::Imported, "Interior", reference}}), "Could not create material catalog");
    auto storageA = std::make_shared<assets::AssetStorageMap>();
    auto storageB = std::make_shared<assets::AssetStorageMap>();
    require(storageA->Replace({{reference, generationA}}) && storageB->Replace({{reference, generationB}}),
        "Could not create material storage snapshots");
    assets.SetAssetSnapshot(catalog, storageA);
    auto *borrowed = assets.LoadMaterialAsset("asset://material-owner#1");
    require(borrowed && borrowed->GetConfig().alphaMode == render::AlphaMode::Blend,
        "Could not load first material generation");
    render::Material privateCopy(borrowed->GetConfig());
    auto unavailable = std::make_shared<assets::AssetStorageMap>();
    require(unavailable->Replace({{reference, generationA, {}, false}}), "Cannot create unavailable snapshot");
    assets.SetAssetSnapshot(catalog, unavailable);
    require(assets.ResolveAssetPath("asset://material-owner#1").empty() &&
        borrowed->GetConfig().alphaMode == render::AlphaMode::Blend,
        "Unavailable generation discarded the last borrowed material");
    assets.SetAssetSnapshot(catalog, storageB);
    require(assets.LoadMaterialAsset("asset://material-owner#1") == borrowed &&
        assets.LoadMaterialAsset(generationB.string()) == borrowed &&
        borrowed->GetConfig().alphaMode == render::AlphaMode::Mask,
        "Relocated generation did not preserve and refresh borrowed material");
    require(privateCopy.GetConfig().alphaMode == render::AlphaMode::Blend,
        "Generation publication changed a scene-owned material copy");
    require(assets.LoadMaterialAsset(generationA.string()) != borrowed,
        "Old physical generation still aliases the new material");
    CheckEmissionMaterial(assets, root);
    std::cout << "Material reload and emissive import checks passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
