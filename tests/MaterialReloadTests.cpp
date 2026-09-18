#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/platform/Window.h"
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
        ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec);
            std::filesystem::remove(path.parent_path(), ec); std::filesystem::remove(path.parent_path().parent_path(), ec); }
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
    std::cout << "Material reload checks passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
