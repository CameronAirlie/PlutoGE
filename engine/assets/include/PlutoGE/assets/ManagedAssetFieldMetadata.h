#pragma once
#include "PlutoGE/assets/AssetType.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PlutoGE::assets
{
    // What a typed managed field references: one asset of `type`, or an array of
    // them. Roles persist as the stable ProjectAssetTypeId ("Prefab", "InputMapping")
    // with a "[]" suffix for arrays, so new asset types need no edits here.
    struct ManagedAssetFieldRole
    {
        ProjectAssetType type = ProjectAssetType::Unknown;
        bool isArray = false;
        friend bool operator==(const ManagedAssetFieldRole &, const ManagedAssetFieldRole &) = default;
    };
    inline constexpr std::string_view kManagedAssetFieldSchema = "__Pluto.AssetFields.Version";
    inline constexpr std::string_view kManagedAssetFieldPrefix = "__Pluto.AssetField.";
    inline constexpr std::string_view kManagedAssetArraySuffix = "[]";
    inline constexpr std::size_t kMaxManagedAssetFields = 4096;
    inline constexpr std::size_t kMaxManagedAssetReferenceBytes = 64 * 1024;
    inline constexpr std::size_t kMaxManagedAssetArrayBytes = 1024 * 1024;

    inline std::string ManagedAssetFieldRoleName(const ManagedAssetFieldRole &role)
    {
        std::string name(ProjectAssetTypeId(role.type));
        if (role.isArray) name += kManagedAssetArraySuffix;
        return name;
    }
    inline std::optional<ManagedAssetFieldRole> ParseManagedAssetFieldRole(std::string_view name)
    {
        const bool isArray = name.ends_with(kManagedAssetArraySuffix);
        if (isArray) name.remove_suffix(kManagedAssetArraySuffix.size());
        const auto type = ParseProjectAssetTypeId(name);
        return type ? std::optional<ManagedAssetFieldRole>(ManagedAssetFieldRole{*type, isArray}) : std::nullopt;
    }

    // An array persists as one string with every element followed by '\n'. References
    // never contain newlines (any are dropped), and the terminator keeps an array
    // holding one empty slot distinct from an empty array.
    inline constexpr char kAssetReferenceArrayTerminator = '\n';
    inline std::string EncodeAssetReferenceArray(std::span<const std::string> references)
    {
        std::string encoded;
        for (const auto &reference : references)
        {
            std::copy_if(reference.begin(), reference.end(), std::back_inserter(encoded),
                         [](char c) { return c != kAssetReferenceArrayTerminator; });
            encoded.push_back(kAssetReferenceArrayTerminator);
        }
        return encoded;
    }
    // nullopt when `text` is not an encoded array (it does not end with a terminator).
    inline std::optional<std::vector<std::string>> DecodeAssetReferenceArray(std::string_view text)
    {
        std::vector<std::string> references;
        if (text.empty()) return references;
        if (text.back() != kAssetReferenceArrayTerminator) return std::nullopt;
        for (std::size_t start = 0; start < text.size();)
        {
            const auto end = text.find(kAssetReferenceArrayTerminator, start);
            references.emplace_back(text.substr(start, end - start));
            start = end + 1;
        }
        return references;
    }

    // The non-empty references a typed field value holds, whatever its shape.
    inline std::vector<std::string> ManagedAssetFieldReferences(const ManagedAssetFieldRole &role, std::string_view value)
    {
        std::vector<std::string> references;
        if (!role.isArray) references.emplace_back(value);
        else if (auto decoded = DecodeAssetReferenceArray(value)) references = std::move(*decoded);
        std::erase_if(references, [](const std::string &reference) { return reference.empty(); });
        return references;
    }
    // Rewrites each reference of a typed field value (for example to a persistent
    // identity), keeping array order and empty slots. Scalars pass through `transform`.
    template <typename Transform>
    std::string TransformManagedAssetFieldValue(const ManagedAssetFieldRole &role, const std::string &value, Transform &&transform)
    {
        if (!role.isArray) return transform(value);
        auto references = DecodeAssetReferenceArray(value);
        if (!references) return value;
        for (auto &reference : *references)
            if (!reference.empty()) reference = transform(reference);
        return EncodeAssetReferenceArray(*references);
    }
    // Dots cannot occur in C# field identifiers, so these records cannot collide
    // with ordinary managed fields. Unknown engine metadata fails closed.
    inline bool IsManagedAssetFieldMetadata(std::string_view name)
    { return name.starts_with("__Pluto."); }

    struct ManagedAssetFieldRecord
    {
        std::string_view name, value;
        bool isString = true;
    };
    struct ManagedAssetFieldMetadata
    {
        bool declared = false;
        std::map<std::string, ManagedAssetFieldRole, std::less<>> fields;
    };

    // Shared by scene persistence, dependency scanning and migration. Legacy
    // components without metadata remain distinguishable. On failure, output is
    // unchanged. Explicit metadata binds each role to exactly one string field,
    // independently of whether the original script assembly can be loaded.
    inline bool ReadManagedAssetFieldMetadata(std::span<const ManagedAssetFieldRecord> records,
        ManagedAssetFieldMetadata &output, std::string *errorMessage = nullptr)
    {
        auto fail = [&](const char *message) { if (errorMessage) *errorMessage = message; return false; };
        if (records.size() > kMaxManagedAssetFields * 2 + 2 &&
            std::any_of(records.begin(), records.end(), [](const auto &record) { return IsManagedAssetFieldMetadata(record.name); }))
            return fail("Managed asset field inventory exceeds its limit.");
        ManagedAssetFieldMetadata parsed;
        bool any = false;
        std::map<std::string_view, const ManagedAssetFieldRecord *, std::less<>> properties;
        bool duplicateProperties = false;
        for (const auto &record : records)
        {
            if (!IsManagedAssetFieldMetadata(record.name))
            {
                if (!properties.emplace(record.name, &record).second) duplicateProperties = true;
                continue;
            }
            any = true;
            if (!record.isString) return fail("Managed asset field metadata must be a string property.");
            if (record.name == kManagedAssetFieldSchema)
            {
                if (parsed.declared || record.value != "1") return fail("Unsupported or duplicate managed asset field schema.");
                parsed.declared = true;
                continue;
            }
            if (!record.name.starts_with(kManagedAssetFieldPrefix)) return fail("Unknown managed asset field metadata.");
            const auto field = record.name.substr(kManagedAssetFieldPrefix.size());
            const auto role = ParseManagedAssetFieldRole(record.value);
            if (field.empty() || field.size() > 1024 || field == "Source" || field.find('.') != std::string_view::npos ||
                std::any_of(field.begin(), field.end(), [](unsigned char c) { return c < 32; }) || !role ||
                parsed.fields.size() >= kMaxManagedAssetFields || !parsed.fields.emplace(std::string(field), *role).second)
                return fail("Invalid or duplicate managed asset field role.");
        }
        if (any && (!parsed.declared || duplicateProperties || properties.size() > kMaxManagedAssetFields + 1)) return fail("Managed asset field schema is missing or its fields are duplicated.");
        for (const auto &[name, role] : parsed.fields)
        {
            const auto property = properties.find(name);
            const auto limit = role.isArray ? kMaxManagedAssetArrayBytes : kMaxManagedAssetReferenceBytes;
            if (property == properties.end() || !property->second->isString || property->second->value.size() > limit)
                return fail("Managed asset role must identify one bounded string field.");
            if (role.isArray && !DecodeAssetReferenceArray(property->second->value))
                return fail("Managed asset array field is malformed.");
        }
        output = std::move(parsed);
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
