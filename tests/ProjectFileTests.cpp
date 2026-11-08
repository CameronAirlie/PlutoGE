// Project manifest persistence: personal editor state is kept out of the shared project file.

#include "PlutoGE/assets/Project.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace PlutoGE;

namespace
{
    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    std::string ReadFile(const std::filesystem::path &path)
    {
        std::ifstream input(path);
        std::ostringstream text;
        text << input.rdbuf();
        return text.str();
    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("PlutoGE-project-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    try
    {
        std::string error;
        auto project = assets::Project::Create(root / "Demo.plutoproject", "Demo", &error);
        Require(project != nullptr, "Create: " + error);
        const auto manifestPath = project->GetManifestPath();

        project->GetManifest().editorCamera.positionX = 12.5f;
        project->GetManifest().editorCamera.yawDegrees = 45.0f;
        Require(project->Save(&error), "Save: " + error);
        Require(ReadFile(manifestPath).find("EDITOR_") == std::string::npos, "Editor state was written into the shared project file");
        const auto statePath = assets::Project::GetEditorStatePath(manifestPath);
        Require(ReadFile(statePath).find("EDITOR_CAMERA_POSITION") != std::string::npos, "Editor state file missing");

        auto reloaded = assets::Project::Load(manifestPath, &error);
        Require(reloaded && reloaded->GetManifest().editorCamera.positionX == 12.5f &&
                    reloaded->GetManifest().editorCamera.yawDegrees == 45.0f,
                "Editor camera did not round-trip through the editor state file");

        // A project saved by an older version keeps editor lines in the manifest and has no state file.
        std::filesystem::remove(statePath);
        {
            std::ofstream append(manifestPath, std::ios::app);
            append << "EDITOR_CAMERA_POSITION\t1\t2\t3\n";
        }
        auto legacy = assets::Project::Load(manifestPath, &error);
        Require(legacy && legacy->GetManifest().editorCamera.positionX == 1.0f, "Legacy manifest editor state was not read");
        Require(legacy->Save(&error), "Legacy save: " + error);
        Require(ReadFile(manifestPath).find("EDITOR_") == std::string::npos, "Legacy editor lines were not moved out on save");
        auto migrated = assets::Project::Load(manifestPath, &error);
        Require(migrated && migrated->GetManifest().editorCamera.positionX == 1.0f, "Migrated editor state lost");
    }
    catch (const std::exception &exception)
    {
        std::cerr << "ProjectFileTests failed: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "ProjectFileTests passed\n";
    return 0;
}
