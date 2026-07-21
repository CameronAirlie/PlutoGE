#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelAsset.h"

#include <cassert>
#include <filesystem>

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
            .reference = "project://Imported/Robot/Robot.plutomesh",
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

    // Cooked builds omit the editor metadata used to resolve stable model-object
    // IDs. The serialized generated mesh reference must remain usable there.
    AssetManager cookedAssetManager;
    const std::string cookedMeshReference = "project://Imported/Robot/Robot.plutomesh";
    assert(cookedAssetManager.ResolveModelObject(source.sourceAssetId, meshId, cookedMeshReference) == cookedMeshReference);

    std::filesystem::remove(path);
    return 0;
}
