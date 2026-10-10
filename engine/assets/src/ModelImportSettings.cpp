#include "PlutoGE/assets/ModelImportSettings.h"
#include "PlutoGE/assets/ModelNodeAliases.h"

#include <algorithm>
#include <charconv>
#include <set>
#include <sstream>
#include <unordered_set>

namespace PlutoGE::assets
{
    namespace
    {
        std::vector<std::string_view> Fields(std::string_view line)
        {
            std::vector<std::string_view> fields;
            while (true)
            {
                const auto separator = line.find('\t');
                fields.push_back(line.substr(0, separator));
                if (separator == std::string_view::npos) return fields;
                line.remove_prefix(separator + 1);
            }
        }

        template <typename T> bool Number(std::string_view text, T &value)
        {
            if (text.empty()) return false;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }

        bool Known(std::string_view key)
        {
            return key == "MODEL_IMPORT" || key == "MODEL_OPTIONS" || key == "MODEL_OBJECT" || key == "MODEL_MATERIAL_REMAP" || key == "MODEL_NODE_ALIAS";
        }

        bool IsEngineMaterial(std::string_view value)
        {
            return value == "engine://builtin/material/default" || value == "engine://builtin/material/default-shaded";
        }

        bool ValidKey(std::string_view key)
        {
            return !key.empty() && key.find_first_of("\t\r\n\0", 0, 4) == std::string_view::npos;
        }
    }

    bool ModelNodeAliasIndex::Build(const ModelImportSettings &settings, std::string *errorMessage)
    {
        const auto fail = [&](const char *message)
        { if (errorMessage) *errorMessage = message; return false; };
        if (settings.nodeAliases.size() > 4096) return fail("Model node alias inventory exceeds its limit.");
        std::unordered_set<std::string_view> primary;
        std::set<std::uint64_t> ids;
        for (const auto &object : settings.objects)
            if (!object.localId || !ValidKey(object.sourceKey) || !primary.insert(object.sourceKey).second ||
                !ids.insert(object.localId).second) return fail("Invalid model object correspondence table.");
        const auto nodeKey = [](std::string_view key)
        {
            if (key.starts_with("node/path/v1/")) key.remove_prefix(std::string_view("node/path/v1/").size());
            else if (key.starts_with("node/source/v1/")) key.remove_prefix(std::string_view("node/source/v1/").size());
            else return false;
            return key.size() == 64 && std::all_of(key.begin(), key.end(),
                [](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); });
        };
        std::map<std::string, std::string, std::less<>> staged;
        for (const auto &alias : settings.nodeAliases)
            if (!nodeKey(alias.sourceKey) || !nodeKey(alias.canonicalSourceKey) ||
                alias.sourceKey == alias.canonicalSourceKey || primary.contains(alias.sourceKey) ||
                !primary.contains(alias.canonicalSourceKey) || !staged.emplace(alias.sourceKey, alias.canonicalSourceKey).second)
                return fail("Node aliases must uniquely target primary node keys without owning another object ID.");
        m_aliases = std::move(staged);
        if (errorMessage) errorMessage->clear();
        return true;
    }

    std::string_view ModelNodeAliasIndex::Resolve(std::string_view key) const
    {
        const auto found = m_aliases.find(key);
        return found == m_aliases.end() ? key : std::string_view(found->second);
    }

    ModelImportSettingsStatus ReadModelImportSettings(const AssetMetadata &metadata, ModelImportSettings &settings,
                                                      std::string *errorMessage)
    {
        ModelImportSettings parsed;
        bool header = false;
        unsigned version = 0;
        bool options = false;
        bool any = false;
        std::unordered_set<std::string> keys;
        std::set<std::uint64_t> ids;
        std::set<std::uint64_t> remaps;
        auto invalid = [&]
        {
            if (errorMessage) *errorMessage = "Invalid or duplicate model import settings record.";
            return ModelImportSettingsStatus::Invalid;
        };
        for (const auto &line : metadata.extensionRecords)
        {
            const auto fields = Fields(line);
            if (!Known(fields[0])) continue;
            any = true;
            if (fields[0] == "MODEL_IMPORT")
            {
                if (header || fields.size() != 2) return invalid();
                if (fields[1] != "1" && fields[1] != "2")
                {
                    if (errorMessage) *errorMessage = "Unsupported model import settings version.";
                    return ModelImportSettingsStatus::UnsupportedVersion;
                }
                version = fields[1] == "2" ? 2 : 1;
                header = true;
            }
            else if (fields[0] == "MODEL_OPTIONS")
            {
                std::uint32_t flags = 0;
                if (options || fields.size() != 2 || !Number(fields[1], flags) || (flags & ~7u) != 0) return invalid();
                parsed.meshOptions = {(flags & 1u) != 0, (flags & 2u) != 0, (flags & 4u) != 0};
                options = true;
            }
            else if (fields[0] == "MODEL_OBJECT")
            {
                ModelObjectIdentity object;
                unsigned retired = 0;
                if (fields.size() != 4 || !Number(fields[1], object.localId) || object.localId == 0 ||
                    !Number(fields[2], retired) || retired > 1 || !ValidKey(fields[3])) return invalid();
                object.sourceKey = fields[3];
                object.retired = retired != 0;
                if (!keys.insert(object.sourceKey).second || !ids.insert(object.localId).second) return invalid();
                parsed.objects.push_back(std::move(object));
            }
            else
            {
                if (fields[0] == "MODEL_NODE_ALIAS")
                {
                    if (fields.size() != 3) return invalid();
                    parsed.nodeAliases.push_back({std::string(fields[1]), std::string(fields[2])});
                    continue;
                }
                ModelMaterialRemap remap;
                if (fields.size() != 3 || !Number(fields[1], remap.materialLocalId) || remap.materialLocalId == 0 ||
                    !remaps.insert(remap.materialLocalId).second) return invalid();
                if (IsEngineMaterial(fields[2])) remap.engineMaterial = fields[2];
                else if (!ParseAssetReference(fields[2], remap.authoredMaterial) || remap.authoredMaterial.IsEmpty() ||
                         remap.authoredMaterial.localObjectId != 0) return invalid();
                parsed.materialRemaps.push_back(std::move(remap));
            }
        }
        if (!any)
        {
            if (errorMessage) errorMessage->clear();
            return ModelImportSettingsStatus::Missing;
        }
        if (!header || !options) return invalid();
        for (const auto &remap : parsed.materialRemaps)
            if (!ids.contains(remap.materialLocalId)) return invalid();
        if (version < 2 && !parsed.nodeAliases.empty()) return invalid();
        ModelNodeAliasIndex aliases;
        if (!aliases.Build(parsed, errorMessage)) return ModelImportSettingsStatus::Invalid;
        settings = std::move(parsed);
        if (errorMessage) errorMessage->clear();
        return ModelImportSettingsStatus::Success;
    }

    bool WriteModelImportSettings(AssetMetadata &metadata, const ModelImportSettings &settings, std::string *errorMessage)
    {
        ModelImportSettings existing;
        const auto status = ReadModelImportSettings(metadata, existing, errorMessage);
        if (status != ModelImportSettingsStatus::Missing && status != ModelImportSettingsStatus::Success) return false;
        AssetMetadata updated = metadata;
        std::erase_if(updated.extensionRecords, [](const auto &line) { return Known(Fields(line)[0]); });
        updated.extensionRecords.push_back(settings.nodeAliases.empty() ? "MODEL_IMPORT\t1" : "MODEL_IMPORT\t2");
        updated.extensionRecords.push_back("MODEL_OPTIONS\t" + std::to_string(settings.meshOptions.ToFlags()));
        auto objects = settings.objects;
        std::sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) { return a.sourceKey < b.sourceKey; });
        for (const auto &object : objects)
        {
            if (!ValidKey(object.sourceKey))
            {
                if (errorMessage) *errorMessage = "Invalid model object source key.";
                return false;
            }
            updated.extensionRecords.push_back("MODEL_OBJECT\t" + std::to_string(object.localId) + "\t" +
                                               (object.retired ? "1\t" : "0\t") + object.sourceKey);
        }
        auto remaps = settings.materialRemaps;
        std::sort(remaps.begin(), remaps.end(), [](const auto &a, const auto &b) { return a.materialLocalId < b.materialLocalId; });
        for (const auto &remap : remaps)
        {
            std::string reference;
            if (!remap.engineMaterial.empty())
            {
                if (!IsEngineMaterial(remap.engineMaterial) || !remap.authoredMaterial.IsEmpty())
                {
                    if (errorMessage) *errorMessage = "Invalid engine material remap.";
                    return false;
                }
                reference = remap.engineMaterial;
            }
            else if (!SerializeAssetReference(remap.authoredMaterial, reference, errorMessage)) return false;
            updated.extensionRecords.push_back("MODEL_MATERIAL_REMAP\t" + std::to_string(remap.materialLocalId) + "\t" + reference);
        }
        auto aliases = settings.nodeAliases;
        std::sort(aliases.begin(), aliases.end(), [](const auto &a, const auto &b) { return a.sourceKey < b.sourceKey; });
        for (const auto &alias : aliases)
            updated.extensionRecords.push_back("MODEL_NODE_ALIAS\t" + alias.sourceKey + "\t" + alias.canonicalSourceKey);
        ModelImportSettings validated;
        if (ReadModelImportSettings(updated, validated, errorMessage) != ModelImportSettingsStatus::Success) return false;
        metadata = std::move(updated);
        return true;
    }

    void BeginModelObjectImport(ModelImportSettings &settings)
    {
        for (auto &object : settings.objects) object.retired = true;
    }

    std::uint64_t ResolveModelObjectId(ModelImportSettings &settings, std::string_view sourceKey,
                                       std::uint64_t legacyId, std::string *errorMessage)
    {
        if (!ValidKey(sourceKey))
        {
            if (errorMessage) *errorMessage = "Model object source key is required and must be a single field.";
            return 0;
        }
        if (const auto alias = std::find_if(settings.nodeAliases.begin(), settings.nodeAliases.end(),
            [&](const auto &value) { return value.sourceKey == sourceKey; }); alias != settings.nodeAliases.end())
        {
            ModelNodeAliasIndex aliases;
            if (!aliases.Build(settings, errorMessage)) return 0;
            if (!ValidKey(alias->canonicalSourceKey) || std::none_of(settings.objects.begin(), settings.objects.end(),
                [&](const auto &value) { return value.sourceKey == alias->canonicalSourceKey && value.localId; }))
            { if (errorMessage) *errorMessage = "Node alias has no primary identity."; return 0; }
            sourceKey = alias->canonicalSourceKey;
        }
        const auto found = std::find_if(settings.objects.begin(), settings.objects.end(),
                                        [&](const auto &object) { return object.sourceKey == sourceKey; });
        if (found != settings.objects.end())
        {
            found->retired = false;
            return found->localId;
        }
        auto used = [&](std::uint64_t id)
        {
            return std::any_of(settings.objects.begin(), settings.objects.end(), [&](const auto &object) { return object.localId == id; });
        };
        auto id = legacyId;
        if (id != 0 && used(id))
        {
            if (errorMessage) *errorMessage = "Legacy object identity is ambiguous; explicit remapping is required.";
            return 0;
        }
        for (int attempt = 0; id == 0 && attempt < 8; ++attempt)
        {
            const auto random = GenerateAssetId();
            std::from_chars(random.data(), random.data() + 16, id, 16);
            if (used(id)) id = 0;
        }
        if (id == 0)
        {
            if (errorMessage) *errorMessage = "Cannot allocate unique model object identity.";
            return 0;
        }
        settings.objects.push_back({std::string(sourceKey), id, false});
        return id;
    }
}
