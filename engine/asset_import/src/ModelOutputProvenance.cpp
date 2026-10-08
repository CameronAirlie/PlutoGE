#include "ModelOutputProvenance.h"
#include "PlutoGE/assets/AssetReference.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace PlutoGE::assetimport
{
    bool ValidateModelSourceCorrespondence(const assets::ModelAsset &previous, const assets::ModelImportSettings &settings,
                                           std::string *errorMessage)
    {
        std::unordered_set<std::uint64_t> active;
        for (const auto &object : settings.objects) if (!object.retired) active.insert(object.localId);
        for (const auto &object : previous.objects)
            if (!active.contains(object.localId))
            {
                if (errorMessage) *errorMessage = "Persistent model correspondence does not contain active object " +
                    std::to_string(object.localId) + ". Restore the source metadata before reimporting.";
                return false;
            }
        return true;
    }

    bool ValidateGeneratedFileBaselines(const assets::Project &project, const assets::ModelAsset &previous,
                                        const assets::ModelImportSettings &settings, assets::AssetManager &reader,
                                        std::string *errorMessage, bool disposableStorage)
    {
        for (const auto &baseline : previous.generatedFiles)
        {
            const auto path = project.ResolveAssetReference(baseline.reference);
            const auto relative = path.lexically_relative(project.GetAssetDirectoryPath());
            if (!assets::Project::IsProjectAssetReference(baseline.reference) || relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            { if (errorMessage) *errorMessage = "Generated file baseline escapes the asset root."; return false; }
            // Immutable Library bytes are disposable; authored Assets outputs
            // still require extraction before an importer may overwrite them.
            if (disposableStorage) continue;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (status.type() == std::filesystem::file_type::not_found || error == std::errc::no_such_file_or_directory) continue;
            content::ContentDigest current;
            if (error || !std::filesystem::is_regular_file(status) || !content::HashFileContent(path, current, errorMessage))
            { if (errorMessage && error) *errorMessage = "Cannot inspect generated file baseline: " + error.message(); return false; }
            if (current == baseline.digest) continue;
            // A material explicitly extracted and remapped has preserved this
            // precise edited generation outside the importer-owned location.
            bool preserved = false;
            for (const auto &object : previous.objects)
            {
                if (object.reference != baseline.reference || object.type != assets::ProjectAssetType::Material) continue;
                const auto remap = std::find_if(settings.materialRemaps.begin(), settings.materialRemaps.end(),
                    [&](const auto &entry) { return entry.materialLocalId == object.localId && !entry.authoredMaterial.IsEmpty(); });
                if (remap == settings.materialRemaps.end()) continue;
                const auto authored = reader.ResolveStableAssetId(remap->authoredMaterial.assetId);
                assets::AssetMetadata metadata;
                if (authored.empty() || assets::LoadAssetMetadata(assets::GetAssetMetadataPath(reader.ResolveAssetPath(authored)), metadata) != assets::AssetMetadataStatus::Success) continue;
                std::string origin;
                if (!assets::SerializeAssetReference({previous.sourceAssetId, object.localId}, origin)) continue;
                const auto &records = metadata.extensionRecords;
                preserved = metadata.ownership == assets::AssetOwnership::Authored &&
                    std::find(records.begin(), records.end(), "EXTRACTED_FROM\t" + origin) != records.end() &&
                    std::find(records.begin(), records.end(), "EXTRACTED_CONTENT_HASH\t" + content::DigestToHex(current)) != records.end();
                if (preserved) break;
            }
            if (!preserved)
            {
                if (errorMessage) *errorMessage = "Generated asset was modified outside import: " + baseline.reference +
                    ". Extract and preserve the edits before reimporting; force reimport does not discard them.";
                return false;
            }
        }
        return true;
    }
}
