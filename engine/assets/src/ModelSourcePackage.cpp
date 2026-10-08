#include "PlutoGE/assets/ModelSourcePackage.h"
#include <algorithm>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view Header = "MODEL_PACKAGE";
        constexpr std::string_view Record = "MODEL_PACKAGE_RECORD";
        std::string_view Key(std::string_view line) { return line.substr(0, line.find('\t')); }
        bool Known(std::string_view line) { const auto key = Key(line); return key == Header || key == Record; }
        bool ValidLocation(std::string_view reference)
        {
            if (!Project::IsProjectAssetReference(reference)) return false;
            const std::filesystem::path relative(reference.substr(Project::kProjectAssetScheme.size()));
            if (relative.empty() || relative.is_absolute()) return false;
            for (const auto &part : relative) if (part == "..") return false;
            return true;
        }
    }

    ModelSourcePackageStatus ReadModelSourcePackage(const AssetMetadata &metadata, ModelAsset &package, std::string *errorMessage)
    {
        bool header = false, any = false;
        std::string text;
        auto invalid = [&] {
            if (errorMessage) *errorMessage = "Invalid persistent model source package.";
            return ModelSourcePackageStatus::Invalid;
        };
        for (const auto &line : metadata.extensionRecords)
        {
            if (!Known(line)) continue;
            any = true;
            const auto separator = line.find('\t');
            if (separator == std::string::npos) return invalid();
            const std::string_view value(line.data() + separator + 1, line.size() - separator - 1);
            if (Key(line) == Header)
            {
                if (header) return invalid();
                if (value != "1")
                {
                    if (errorMessage) *errorMessage = "Unsupported persistent model source package version.";
                    return ModelSourcePackageStatus::UnsupportedVersion;
                }
                header = true;
            }
            else
            {
                if (value.size() > 16 * 1024 * 1024 || text.size() > 16 * 1024 * 1024 - value.size()) return invalid();
                text.append(value); text += '\n';
            }
        }
        if (!any) { if (errorMessage) errorMessage->clear(); return ModelSourcePackageStatus::Missing; }
        if (!header) return invalid();
        ModelAsset parsed;
        if (!ParseModelAsset(text, parsed, errorMessage)) return ModelSourcePackageStatus::Invalid;
        if (metadata.id.empty() || parsed.sourceAssetId != metadata.id || !ValidLocation(parsed.sourceReference)) return invalid();
        for (const auto &object : parsed.objects) if (!ValidLocation(object.reference)) return invalid();
        for (const auto &file : parsed.generatedFiles) if (!ValidLocation(file.reference)) return invalid();
        package = std::move(parsed);
        if (errorMessage) errorMessage->clear();
        return ModelSourcePackageStatus::Success;
    }

    bool LoadModelSourcePackage(const Project &project, std::string_view sourceReference, ModelAsset &package, std::string *errorMessage)
    {
        if (!Project::IsProjectAssetReference(sourceReference) || Project::GetAssetTypeForReference(sourceReference) != ProjectAssetType::Model)
        { if (errorMessage) *errorMessage = "A project model source is required."; return false; }
        AssetMetadata metadata;
        const auto status = LoadAssetMetadata(GetAssetMetadataPath(project.ResolveAssetReference(sourceReference)), metadata, errorMessage);
        if (status != AssetMetadataStatus::Success && status != AssetMetadataStatus::Missing) return false;
        ModelAsset parsed;
        const auto packageStatus = ReadModelSourcePackage(metadata, parsed, errorMessage);
        if (packageStatus != ModelSourcePackageStatus::Success && packageStatus != ModelSourcePackageStatus::Missing) return false;
        if (packageStatus == ModelSourcePackageStatus::Missing && !LoadModelAsset(FindModelManifestPath(project, sourceReference).string(), parsed, errorMessage)) return false;
        parsed.sourceReference = sourceReference;
        package = std::move(parsed);
        if (errorMessage) errorMessage->clear();
        return true;
    }

    bool WriteModelSourcePackage(AssetMetadata &metadata, const ModelAsset &package, std::string *errorMessage)
    {
        ModelAsset existing;
        const auto status = ReadModelSourcePackage(metadata, existing, errorMessage);
        if (status != ModelSourcePackageStatus::Missing && status != ModelSourcePackageStatus::Success) return false;
        std::string text;
        if (metadata.id.empty() || package.sourceAssetId != metadata.id)
        { if (errorMessage) *errorMessage = "Model source package owner does not match its metadata."; return false; }
        if (!SerializeModelAsset(package, text, errorMessage)) return false;
        auto updated = metadata;
        std::erase_if(updated.extensionRecords, [](const auto &line) { return Known(line); });
        updated.extensionRecords.emplace_back("MODEL_PACKAGE\t1");
        std::string_view remaining(text);
        while (!remaining.empty())
        {
            const auto end = remaining.find('\n');
            auto line = remaining.substr(0, end);
            if (line.ends_with('\r')) line.remove_suffix(1);
            updated.extensionRecords.push_back("MODEL_PACKAGE_RECORD\t" + std::string(line));
            if (end == std::string_view::npos) break;
            remaining.remove_prefix(end + 1);
        }
        std::string validated;
        ModelAsset checked;
        if (!SerializeAssetMetadata(updated, validated, errorMessage) ||
            ReadModelSourcePackage(updated, checked, errorMessage) != ModelSourcePackageStatus::Success) return false;
        metadata = std::move(updated);
        return true;
    }
}
