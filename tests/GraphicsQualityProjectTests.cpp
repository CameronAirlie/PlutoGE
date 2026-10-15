#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/RhiRenderService.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace PlutoGE;
namespace
{
    void Check(bool value, const char *message)
    { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        const std::filesystem::path parent = std::filesystem::absolute(std::filesystem::temp_directory_path()).lexically_normal();
        const std::filesystem::path root = parent / ("PlutoGE-quality-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root); }
        ~Scratch()
        {
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-quality-"))
            { std::error_code error; std::filesystem::remove_all(root, error); }
        }
    };
    std::string Read(const std::filesystem::path &path)
    { std::ifstream file(path); return {(std::istreambuf_iterator<char>(file)), {}}; }
}
int main(int argc, char **argv)
{
    try
    {
        Scratch scratch;
        const auto path = scratch.root / "Quality.plutoproject";
        std::string error;
        { std::ofstream file(path); file << "PLUTOPROJECT\t3\nNAME\tOld project\n"; }
        auto project = assets::Project::Load(path, &error);
        Check(project != nullptr, "Legacy project load failed");
        Check(project->GetManifest().graphicsQualityPresets == render::GraphicsQualityPresets{}, "Missing tiers did not retain engine defaults");
        auto &presets = project->GetManifest().graphicsQualityPresets;
        for (std::size_t i = 0; i < presets.tiers.size(); ++i)
        {
            auto &q = presets.tiers[i];
            q.enabled = i != 2;
            q.shadowResolution = 768 + static_cast<unsigned>(i) * 256;
            q.shadowCascades = 4 - static_cast<unsigned>(i);
            q.shadowDistance = 123.45678f + static_cast<float>(i);
            q.cascadedShadows = q.globalIllumination = q.volumetrics = q.depthOfField = i % 2 == 0;
            q.ambientOcclusion = q.reflections = q.bloom = q.motionBlur = i % 2 != 0;
        }
        Check(project->Save(&error), "Custom preset save failed");
        auto loaded = assets::Project::Load(path, &error);
        Check(loaded && loaded->GetManifest().graphicsQualityPresets == presets, "Quality fields or float precision were lost on reload");
        // Runtime/export manifests are copied from the project manifest.
        assets::Project exported(scratch.root / "Game.plutoproject", loaded->GetManifest());
        Check(exported.Save(&error), "Runtime manifest save failed");
        const auto exportedLoaded = assets::Project::Load(exported.GetManifestPath(), &error);
        Check(exportedLoaded && exportedLoaded->GetManifest().graphicsQualityPresets == presets, "Runtime manifest lost tier definitions");
        render::RhiRenderService service;
        Check(service.SetGraphicsQualityPresets(presets), "Runtime rejected project presets");
        Check(service.GetGraphicsPreset(render::GraphicsPreset::Low) == presets.tiers[0], "Runtime lookup ignored project definitions");
        Check(!service.GetGraphicsQuality().enabled, "Loading definitions changed active player quality");
        auto invalid = presets;
        invalid.tiers[3].shadowDistance = std::numeric_limits<float>::quiet_NaN();
        Check(!service.SetGraphicsQualityPresets(invalid), "Invalid definitions accepted");
        Check(service.GetGraphicsPreset(render::GraphicsPreset::Low) == presets.tiers[0], "Invalid definitions partially changed runtime table");
        const auto saved = Read(path);
        presets = invalid;
        Check(!project->Save(&error) && Read(path) == saved, "Invalid save overwrote project");
        Check(service.SetGraphicsQualityPresets({}), "Project reset failed");
        Check(service.GetGraphicsPreset(render::GraphicsPreset::Low) == render::GraphicsQuality::FromPreset(render::GraphicsPreset::Low), "Previous project's tiers leaked into defaults");
        for (const auto *record : {
            "QUALITY_PRESET\t0\t1\t512\t1\tnan\t1\t0\t0\t0\t0\t0\t0\t0",
            "QUALITY_PRESET\t0\t1\t255\t1\t40\t1\t0\t0\t0\t0\t0\t0\t0",
            "QUALITY_PRESET\t4\t1\t512\t1\t40\t1\t0\t0\t0\t0\t0\t0\t0",
            "QUALITY_PRESET\t0\t1\t512\t1\t40\t1\t2\t0\t0\t0\t0\t0\t0",
            "QUALITY_PRESET\t0"})
        {
            { std::ofstream file(path); file << "PLUTOPROJECT\t3\n" << record << '\n'; }
            Check(!assets::Project::Load(path, &error) && error.find("QUALITY_PRESET") != std::string::npos, "Malformed quality definition was not rejected");
        }
        if (argc > 1)
        {
            const auto activeProject = assets::Project::Load(argv[1], &error);
            Check(activeProject && activeProject->GetManifest().graphicsQualityPresets.IsValid(), "Active project manifest failed quality validation");
            Check(service.SetGraphicsQualityPresets(activeProject->GetManifest().graphicsQualityPresets), "Active project definitions were not installed");
        }
        std::cout << "PASS: legacy defaults, tier roundtrip, runtime manifest, runtime lookup, invalid-save preservation and malformed records\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
