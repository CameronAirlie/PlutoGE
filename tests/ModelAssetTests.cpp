// Keep the checks and their setup calls active in distribution builds.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include <stdexcept>

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

    source.generatedFiles.push_back({source.objects.front().reference, {}});
    source.extensionRecords.push_back("FUTURE_RECORD\tkept");
    std::string encoded;
    std::string codecError;
    assert(SerializeModelAsset(source, encoded, &codecError));
    ModelAsset decoded;
    assert(ParseModelAsset(encoded, decoded, &codecError));
    assert(decoded.generatedFiles.size() == 1 && decoded.extensionRecords == source.extensionRecords);
    std::string crlf;
    for (const auto character : encoded) { if (character == '\n') crlf += '\r'; crlf += character; }
    assert(ParseModelAsset(crlf, decoded, &codecError));
    for (const auto &invalid : {
        std::string("PLUTOMODEL\t1\nSOURCE\tproject://Robot.fbx\nSOURCE_HASH\t12junk\n"),
        std::string("PLUTOMODEL\t1\nSOURCE\tproject://Robot.fbx\nOBJECT\t0\tMesh\tRobot\tproject://Robot.plutomesh\n"),
        encoded + "SOURCE\tproject://Duplicate.fbx\n",
        encoded + "OUTPUT_HASH\tproject://Invalid.plutomesh\tbad-digest\n",
        std::string("PLUTOMODEL\t2\nSOURCE\tproject://Robot.fbx\n")})
    {
        decoded.sourceReference = "sentinel";
        assert(!ParseModelAsset(invalid, decoded, &codecError) && decoded.sourceReference == "sentinel");
    }
    auto invalidObject = source;
    invalidObject.objects.push_back(source.objects.front());
    encoded = "sentinel";
    assert(!SerializeModelAsset(invalidObject, encoded, &codecError) && encoded == "sentinel");
    {
        const auto Require = [](bool value, const char *message) { if (!value) throw std::runtime_error(message); };
        AssetMetadata metadata{.id="owner", .extensionRecords={"CUSTOM\tkeep"}};
        std::string error;
        ModelAsset package;
        package.sourceReference = "project://Robot.fbx";
        package.sourceAssetId = metadata.id;
        package.importerVersion = 4;
        package.objects.push_back({42, ProjectAssetType::Mesh, "Robot", "project://Robot.plutomesh"});
        package.generatedFiles.push_back({"project://Robot.plutomesh", {}});
        package.extensionRecords.push_back("FUTURE_PACKAGE\tretained");
        Require(WriteModelSourcePackage(metadata, package, &error), "Source package write failed");
        ModelAsset restored;
        Require(ReadModelSourcePackage(metadata, restored, &error) == ModelSourcePackageStatus::Success &&
                restored.objects.front().localId == 42 && restored.generatedFiles.size() == 1 &&
                restored.extensionRecords == package.extensionRecords, "Source package round trip lost provenance");
        Require(metadata.extensionRecords.front() == "CUSTOM\tkeep", "Package update lost unrelated metadata");
        auto escapingPackage = package;
        escapingPackage.objects.front().reference = "project://../Outside.plutomesh";
        const auto validPackageRecords = metadata.extensionRecords;
        Require(!WriteModelSourcePackage(metadata, escapingPackage, &error) && metadata.extensionRecords == validPackageRecords, "Escaping package location was published");
        auto brokenPackage = metadata;
        brokenPackage.id = "other-owner";
        Require(ReadModelSourcePackage(brokenPackage, restored, &error) == ModelSourcePackageStatus::Invalid &&
                restored.sourceAssetId == metadata.id, "Package owner mismatch accepted or changed output");
        brokenPackage = metadata;
        brokenPackage.extensionRecords.push_back("MODEL_PACKAGE\t1");
        Require(ReadModelSourcePackage(brokenPackage, restored, &error) == ModelSourcePackageStatus::Invalid, "Duplicate package header accepted");
        brokenPackage = metadata;
        for (auto &record : brokenPackage.extensionRecords) if (record == "MODEL_PACKAGE\t1") record = "MODEL_PACKAGE\t2";
        const auto futurePackage = brokenPackage.extensionRecords;
        Require(ReadModelSourcePackage(brokenPackage, restored, &error) == ModelSourcePackageStatus::UnsupportedVersion &&
                !WriteModelSourcePackage(brokenPackage, package, &error) && brokenPackage.extensionRecords == futurePackage, "Future source package overwritten");
    }
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
    project.GetManifest().runtimeUpscaler = RuntimeUpscalerMode::Fsr2;
    project.GetManifest().runtimeUpscalerQuality = PlutoGE::render::rhi::UpscalerQuality::Balanced;
    project.GetManifest().graphicsApi = PlutoGE::render::rhi::GraphicsApi::Vulkan;
    project.GetManifest().runtimeRenderScale = 0.75f;
    project.GetManifest().runtimeUpscaleSharpness = 0.4f;
    project.GetManifest().loadingScreen = {"EMBERVAULT", {.95f, .48f, .12f}, "project://UI/loading.plutoloading"};
    assert(project.Save(&error));
    auto reloadedProject = Project::Load(projectPath, &error);
    assert(reloadedProject);
    assert(reloadedProject->GetManifest().loadingScreen.title == "EMBERVAULT");
    assert(reloadedProject->GetManifest().loadingScreen.assetReference == "project://UI/loading.plutoloading");
    assert(Project::GetAssetTypeForReference("project://UI/loading.plutoloading") == ProjectAssetType::LoadingScreen);
    assert(reloadedProject->GetManifest().loadingScreen.accent == project.GetManifest().loadingScreen.accent);
    assert(reloadedProject->GetManifest().runtimeUpscaler == RuntimeUpscalerMode::Fsr2);
    assert(reloadedProject->GetManifest().runtimeUpscalerQuality == PlutoGE::render::rhi::UpscalerQuality::Balanced);
    assert(reloadedProject->GetManifest().graphicsApi == PlutoGE::render::rhi::GraphicsApi::Vulkan);
    const auto temporalUpscaler = reloadedProject->GetManifest().GetTemporalUpscalerOptions();
    assert(temporalUpscaler.technology == PlutoGE::render::rhi::TemporalUpscaler::Fsr2);
    assert(temporalUpscaler.quality == PlutoGE::render::rhi::UpscalerQuality::Balanced);
    assert(temporalUpscaler.sharpness == 0.4f);
    assert(reloadedProject->GetManifest().runtimeRenderScale == 0.75f);
    assert(reloadedProject->GetManifest().runtimeUpscaleSharpness == 0.4f);

    const auto legacyProjectPath = projectRoot / "LegacyProject.plutoproject";
    std::ofstream legacyProject(legacyProjectPath);
    legacyProject << "PLUTOPROJECT\t1\nNAME\tLegacy\n";
    legacyProject.close();
    auto reloadedLegacyProject = Project::Load(legacyProjectPath, &error);
    assert(reloadedLegacyProject);
    assert(reloadedLegacyProject->GetManifest().loadingScreen.title.empty());
    assert(reloadedLegacyProject->GetManifest().loadingScreen.assetReference.empty());
    assert(reloadedLegacyProject->GetManifest().graphicsApi == PlutoGE::render::rhi::GraphicsApi::OpenGL);
    const std::string sourceReference = "project://SourceModels/Robot/Robot.fbx";
    assert(GetModelArtifactDirectory(project, sourceReference) == packageRoot);
    assert(GetModelManifestPath(project, sourceReference) == packageRoot / "Robot.plutomodel");
    assert(FindModelManifestPath(project, sourceReference) == packageRoot / "Robot.plutomodel");

    const auto legacyManifest = assetRoot / "Imported" / "Robot" / "Robot.plutomodel";
    std::filesystem::create_directories(legacyManifest.parent_path());
    std::ofstream(legacyManifest) << "legacy";
    assert(FindModelManifestPath(project, sourceReference) == legacyManifest);

    ModelAsset otherOwner{.sourceReference="project://Other/Robot.fbx", .sourceAssetId="other-owner"};
    assert(SaveModelAsset(legacyManifest.string(), otherOwner, &error));
    assert(FindModelManifestPath(project, sourceReference) == packageRoot / "Robot.plutomodel");

    std::ofstream(packageRoot / "Robot.plutomodel") << "canonical";
    assert(FindModelManifestPath(project, sourceReference) == packageRoot / "Robot.plutomodel");

    std::filesystem::remove(path);
    std::filesystem::remove_all(projectRoot);
    return 0;
}
