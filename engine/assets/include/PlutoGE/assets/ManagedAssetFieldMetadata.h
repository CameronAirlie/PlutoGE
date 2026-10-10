#pragma once
#include "PlutoGE/assets/AssetType.h"
#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace PlutoGE::assets
{
    enum class ManagedAssetFieldKind { Prefab, ScriptableObject, Material, InputMapping };
    inline constexpr std::string_view kManagedAssetFieldSchema = "__Pluto.AssetFields.Version";
    inline constexpr std::string_view kManagedAssetFieldPrefix = "__Pluto.AssetField.";
    inline constexpr std::size_t kMaxManagedAssetFields = 4096;

    inline std::string_view ManagedAssetFieldKindName(ManagedAssetFieldKind kind)
    {
        switch (kind)
        {
        case ManagedAssetFieldKind::Prefab: return "Prefab";
        case ManagedAssetFieldKind::ScriptableObject: return "ScriptableObject";
        case ManagedAssetFieldKind::Material: return "Material";
        case ManagedAssetFieldKind::InputMapping: return "InputMapping";
        }
        return {};
    }
    inline std::optional<ManagedAssetFieldKind> ParseManagedAssetFieldKind(std::string_view name)
    {
        for (const auto kind : {ManagedAssetFieldKind::Prefab, ManagedAssetFieldKind::ScriptableObject,
            ManagedAssetFieldKind::Material, ManagedAssetFieldKind::InputMapping})
            if (ManagedAssetFieldKindName(kind) == name) return kind;
        return std::nullopt;
    }
    inline ProjectAssetType ManagedAssetFieldAssetType(ManagedAssetFieldKind kind)
    {
        switch (kind)
        {
        case ManagedAssetFieldKind::Prefab: return ProjectAssetType::Prefab;
        case ManagedAssetFieldKind::ScriptableObject: return ProjectAssetType::ScriptableObject;
        case ManagedAssetFieldKind::Material: return ProjectAssetType::Material;
        case ManagedAssetFieldKind::InputMapping: return ProjectAssetType::InputMapping;
        }
        return ProjectAssetType::Unknown;
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
        std::map<std::string, ManagedAssetFieldKind, std::less<>> fields;
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
            const auto kind = ParseManagedAssetFieldKind(record.value);
            if (field.empty() || field.size() > 1024 || field == "Source" || field.find('.') != std::string_view::npos ||
                std::any_of(field.begin(), field.end(), [](unsigned char c) { return c < 32; }) || !kind ||
                parsed.fields.size() >= kMaxManagedAssetFields || !parsed.fields.emplace(std::string(field), *kind).second)
                return fail("Invalid or duplicate managed asset field role.");
        }
        if (any && (!parsed.declared || duplicateProperties || properties.size() > kMaxManagedAssetFields + 1)) return fail("Managed asset field schema is missing or its fields are duplicated.");
        for (const auto &[name, kind] : parsed.fields)
        {
            (void)kind;
            const auto property = properties.find(name);
            if (property == properties.end() || !property->second->isString || property->second->value.size() > 64 * 1024)
                return fail("Managed asset role must identify one bounded string field.");
        }
        output = std::move(parsed);
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
