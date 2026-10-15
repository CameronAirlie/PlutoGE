#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/MapBake.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Headless top-down map bake, the command-line twin of the editor's Tools > Bake Map.
// Writes Assets/<folder>/<Scene>.tga and <Scene>.plutomap for each scene and registers them.
//   PlutoGEBakeMap <project.plutoproject> <scene>... [--folder Maps] [--size 1024] [--padding 4]
//                  [--exclude-tag Tag]... [--no-nav-crop]
int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    if (argc < 3)
        throw std::invalid_argument("Usage: PlutoGEBakeMap <project.plutoproject> <scene>... [--folder Maps] [--size 1024] "
                                    "[--padding 4] [--exclude-tag Tag]... [--no-nav-crop]");
    std::string error;
    auto project = assets::Project::Load(std::filesystem::absolute(argv[1]), &error);
    if (!project) throw std::runtime_error(error);
    core::Engine::GetInstance().GetAssetManager().SetProjectContext(
        project->GetRootDirectory().string(), project->GetManifest().assetDirectory, project->GetManifest().assetPipelineVersion);

    scene::MapBakeSettings settings;
    std::string folder = "Maps";
    std::vector<std::string> scenes;
    for (int argument = 2; argument < argc; ++argument)
    {
        const std::string option(argv[argument]);
        const auto next = [&]() -> std::string {
            if (argument + 1 >= argc) throw std::invalid_argument(option + " needs a value");
            return argv[++argument];
        };
        if (option == "--folder") folder = next();
        else if (option == "--size") settings.size = std::stoi(next());
        else if (option == "--padding") settings.padding = std::stof(next());
        else if (option == "--exclude-tag") settings.excludeTags.push_back(next());
        else if (option == "--no-nav-crop") settings.cropToNavigation = false;
        else if (option.rfind("--", 0) == 0) throw std::invalid_argument("Unknown option " + option);
        else scenes.push_back(option);
    }

    int failures = 0;
    for (const auto &name : scenes)
    {
        const auto reference = project->FindSceneAssetReference(name);
        if (reference.empty()) { std::cerr << name << ": scene not found\n"; ++failures; continue; }
        const auto path = project->ResolveAssetReference(reference);
        auto world = scene::SceneSerializer::Load(path.string(), &error);
        if (!world) { std::cerr << name << ": " << error << '\n'; ++failures; continue; }
        const auto result = scene::BakeTopDownMap(*world, settings);
        scene::MapBakeOutput output;
        if (!scene::SaveMapBake(*project, result, path.stem().string(), folder, &output, &error))
        {
            std::cerr << name << ": " << error << '\n';
            ++failures;
            continue;
        }
        std::cout << path.stem().string() << ": " << result.message << " -> " << output.layoutReference << '\n';
    }
    return failures == 0 ? 0 : 1;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
