#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/asset_import/ModelArtifactSettings.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include <algorithm>
#include "PlutoGE/assets/ModelSourcePackage.h"

namespace PlutoGE::assetimport
{
    bool ComputeModelArtifactSettings(const assets::AssetMetadata &metadata, const MeshImportOptions &options,
                                      std::string_view sourceReference, const std::vector<std::string> &bindings,
                                      std::vector<std::filesystem::path> outputs, content::ContentDigest &digest,
                                      std::string *errorMessage, bool libraryStorage)
    {
        auto semanticMetadata = metadata;
        assets::ModelAsset package;
        const auto packageStatus = assets::ReadModelSourcePackage(metadata, package, errorMessage);
        if (packageStatus != assets::ModelSourcePackageStatus::Success && packageStatus != assets::ModelSourcePackageStatus::Missing) return false;
        assets::ModelHierarchyArtifact hierarchy;
        const auto hierarchyStatus = assets::ReadModelHierarchyArtifact(package, hierarchy, errorMessage);
        if (hierarchyStatus != assets::ModelHierarchyArtifactStatus::Success && hierarchyStatus != assets::ModelHierarchyArtifactStatus::Missing) return false;
        content::ContentDigest activeGeneration;
        const auto generationStatus = assets::ReadModelArtifactGeneration(semanticMetadata, activeGeneration, errorMessage);
        if (generationStatus != assets::ModelArtifactGenerationStatus::Success && generationStatus != assets::ModelArtifactGenerationStatus::Missing) return false;
        std::erase_if(semanticMetadata.extensionRecords, [](const auto &line)
        {
            const auto key = std::string_view(line).substr(0, line.find('\t'));
            return key == "MODEL_PACKAGE" || key == "MODEL_PACKAGE_RECORD" || key == "MODEL_ARTIFACT" || key == "MODEL_PACKAGE_ARTIFACT";
        });
        semanticMetadata.importerVersion = 1; // The recipe version carries importer compatibility.
        std::string metadataBytes;
        if (!assets::SerializeAssetMetadata(semanticMetadata, metadataBytes, errorMessage)) return false;
        content::ContentHasher hasher;
        auto field = [&](std::string_view value)
        {
            const auto size = std::to_string(value.size()) + ":";
            hasher.Update(std::as_bytes(std::span(size.data(), size.size())));
            hasher.Update(std::as_bytes(std::span(value.data(), value.size())));
        };
        field(libraryStorage ? "Library" : "Assets");
        field(metadataBytes);
        // Unknown package records affect preserved output bytes and therefore
        // remain semantic inputs; generated hashes and locations do not.
        for (const auto &record : package.extensionRecords) field(record);
        field(std::to_string(options.ToFlags()));
        field(sourceReference);
        for (const auto &binding : bindings) field(binding);
        std::sort(outputs.begin(), outputs.end());
        for (const auto &output : outputs) field(output.generic_string());
        digest = hasher.Finalize();
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
