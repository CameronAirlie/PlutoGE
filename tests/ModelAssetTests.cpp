#include "PlutoGE/assets/ModelAsset.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main()
{
    using namespace PlutoGE::assets;

    const auto meshId = MakeModelSubAssetId(ProjectAssetType::Mesh, "Robot");
    assert(meshId != 0);
    assert(meshId == MakeModelSubAssetId(ProjectAssetType::Mesh, "Robot"));
    assert(meshId != MakeModelSubAssetId(ProjectAssetType::Material, "Robot"));

    ModelAsset source{
        .sourceReference = "project://Models/Robot.fbx",
        .sourceAssetId = "0123456789abcdef0123456789abcdef",
        .sourceContentHash = 0x123456789abcdef0ull,
        .importerVersion = 3,
        .objects = {{
            .localId = meshId,
            .type = ProjectAssetType::Mesh,
            .name = "Robot",
            .reference = "project://Models/Robot/Robot.plutomesh",
        }},
    };

    const auto path = std::filesystem::current_path() / "ModelAssetTests.plutomodel";
    std::string error;
    assert(SaveModelAsset(path.string(), source, &error));

    ModelAsset loaded;
    assert(LoadModelAsset(path.string(), loaded, &error));
    assert(loaded.sourceReference == source.sourceReference);
    assert(loaded.sourceAssetId == source.sourceAssetId);
    assert(loaded.sourceContentHash == source.sourceContentHash);
    assert(loaded.importerVersion == source.importerVersion);
    assert(loaded.objects.size() == 1);
    assert(loaded.objects[0].localId == meshId);
    assert(loaded.objects[0].type == ProjectAssetType::Mesh);
    assert(loaded.objects[0].reference == source.objects[0].reference);

    const auto projectRoot = std::filesystem::current_path() / "ModelAssetPathTests";
    const auto assetRoot = projectRoot / "Assets";
    const auto packageRoot = assetRoot / "SourceModels" / "Robot";
    std::filesystem::create_directories(packageRoot);
    const auto projectPath = projectRoot / "ModelAssetPathTests.plutoproject";
    Project project(projectPath, ProjectManifest{.assetDirectory = "Assets"});
    project.GetManifest().runtimeUpscaler = RuntimeUpscalerMode::Spatial;
    project.GetManifest().graphicsApi = PlutoGE::render::rhi::GraphicsApi::Vulkan;
    project.GetManifest().runtimeRenderScale = 0.75f;
    project.GetManifest().runtimeUpscaleSharpness = 0.4f;
    assert(project.Save(&error));
    auto reloadedProject = Project::Load(projectPath, &error);
    assert(reloadedProject);
    assert(reloadedProject->GetManifest().runtimeUpscaler == RuntimeUpscalerMode::Spatial);
    assert(reloadedProject->GetManifest().graphicsApi == PlutoGE::render::rhi::GraphicsApi::Vulkan);
    assert(reloadedProject->GetManifest().runtimeRenderScale == 0.75f);
    assert(reloadedProject->GetManifest().runtimeUpscaleSharpness == 0.4f);

    const auto legacyProjectPath = projectRoot / "LegacyProject.plutoproject";
    std::ofstream legacyProject(legacyProjectPath);
    legacyProject << "PLUTOPROJECT\t1\nNAME\tLegacy\n";
    legacyProject.close();
    auto reloadedLegacyProject = Project::Load(legacyProjectPath, &error);
    assert(reloadedLegacyProject);
    assert(reloadedLegacyProject->GetManifest().graphicsApi == PlutoGE::render::rhi::GraphicsApi::OpenGL);
    const std::string sourceReference = "project://SourceModels/Robot/Robot.fbx";
    assert(GetModelArtifactDirectory(project, sourceReference) == packageRoot);
    assert(GetModelManifestPath(project, sourceReference) == packageRoot / "Robot.plutomodel");
    assert(FindModelManifestPath(project, sourceReference) == packageRoot / "Robot.plutomodel");

    const auto legacyManifest = assetRoot / "Imported" / "Robot" / "Robot.plutomodel";
    std::filesystem::create_directories(legacyManifest.parent_path());
    std::ofstream(legacyManifest) << "legacy";
    assert(FindModelManifestPath(project, sourceReference) == legacyManifest);

    std::ofstream(packageRoot / "Robot.plutomodel") << "canonical";
    assert(FindModelManifestPath(project, sourceReference) == packageRoot / "Robot.plutomodel");

    std::filesystem::remove(path);
    std::filesystem::remove_all(projectRoot);
    return 0;
}
