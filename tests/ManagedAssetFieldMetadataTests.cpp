#include "PlutoGE/assets/ManagedAssetFieldMetadata.h"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace PlutoGE::assets;
namespace
{
    void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
}
int main()
try
{
    std::string error;
    ManagedAssetFieldMetadata output;
    const std::vector<ManagedAssetFieldRecord> legacy = {{"Source", "Missing.Script"}, {"Text", "asset://ordinary-text#0"}};
    Require(ReadManagedAssetFieldMetadata(legacy, output, &error) && !output.declared && output.fields.empty(),
        "Legacy component was mistaken for explicit asset fields");
    std::vector<ManagedAssetFieldRecord> fields = {
        {"Source", "Missing.Script"}, {kManagedAssetFieldSchema, "1"},
        {"__Pluto.AssetField.Template", "Prefab"}, {"Template", "asset://prefab-owner#0"},
        {"__Pluto.AssetField.Data", "ScriptableObject"}, {"Data", "project://Settings.plutoscriptable"},
        {"__Pluto.AssetField.Paint", "Material"}, {"Paint", "engine://builtin/material/default-shaded"},
        {"__Pluto.AssetField.Controls", "InputMapping"}, {"Controls", ""},
        {"__Pluto.AssetField.Spawns", "Prefab[]"}, {"Spawns", "project://A.plutoprefab\n\nasset://prefab-owner#0\n"},
        {"Text", "asset://ordinary-text#0"}, {"Count", "7", false}
    };
    Require(ReadManagedAssetFieldMetadata(fields, output, &error) && output.declared && output.fields.size() == 5 &&
        output.fields.at("Template") == ManagedAssetFieldRole{ProjectAssetType::Prefab} &&
        output.fields.at("Spawns") == ManagedAssetFieldRole{ProjectAssetType::Prefab, true} && !output.fields.contains("Text"),
        "Explicit metadata lost roles or promoted ordinary strings");
    for (const auto &[name, role] : output.fields)
    {
        (void)name;
        Require(ParseManagedAssetFieldRole(ManagedAssetFieldRoleName(role)) == role &&
            role.type != ProjectAssetType::Unknown, "Managed field role did not round trip");
    }
    const auto expected = output.fields;
    auto invalid = [&](const std::vector<ManagedAssetFieldRecord> &records) {
        Require(!ReadManagedAssetFieldMetadata(records, output, &error) && output.declared && output.fields == expected &&
            !error.empty(), "Malformed field metadata changed output or had no diagnostic");
    };
    auto broken = fields; broken[1].value = "2"; invalid(broken);
    broken = fields; broken.push_back(fields[1]); invalid(broken);
    broken = fields; broken.erase(broken.begin() + 1); invalid(broken);
    broken = fields; broken.push_back(fields[2]); invalid(broken);
    broken = fields; broken.push_back(fields[3]); invalid(broken);
    broken = fields; broken[2].value = "NotAnAssetType"; invalid(broken);
    broken = fields; broken[2].value = ""; invalid(broken);
    for (std::size_t index = 1; index < static_cast<std::size_t>(ProjectAssetType::Count); ++index)
    {
        for (const bool isArray : {false, true})
        {
            const ManagedAssetFieldRole role{static_cast<ProjectAssetType>(index), isArray};
            Require(ParseManagedAssetFieldRole(ManagedAssetFieldRoleName(role)) == role,
                "Every asset type must be usable as a managed field role and array role");
        }
    }
    auto texture = fields; texture[2].value = "Texture";
    ManagedAssetFieldMetadata textureOutput;
    Require(ReadManagedAssetFieldMetadata(texture, textureOutput, &error) && textureOutput.fields.at("Template").type == ProjectAssetType::Texture,
        "Typed asset field roles must not be limited to a fixed set of asset types");
    broken = fields; broken[10].value = "Prefab[][]"; invalid(broken);
    broken = fields; broken[10].value = "[]"; invalid(broken);
    broken = fields; broken[11].value = "project://A.plutoprefab"; invalid(broken);
    broken = fields; broken[2].isString = false; invalid(broken);
    broken = fields; broken[3].isString = false; invalid(broken);
    broken = fields; broken.erase(broken.begin() + 3); invalid(broken);
    broken = fields; broken[2].name = "__Pluto.AssetField."; invalid(broken);
    broken = fields; broken[2].name = "__Pluto.AssetField.Source"; invalid(broken);
    broken = fields; broken[2].name = "__Pluto.AssetField.Nested.Field"; invalid(broken);
    broken = fields; broken[2].name = "__Pluto.AssetField.Bad\tName"; invalid(broken);
    broken = fields; broken[2].name = "__Pluto.Future.Schema"; invalid(broken);
    const std::string tooLong(64 * 1024 + 1, 'x');
    broken = fields; broken[3].value = tooLong; invalid(broken);
    const std::vector<std::string> slots = {"project://A.plutoprefab", "", "asset://prefab-owner#0"};
    Require(DecodeAssetReferenceArray(EncodeAssetReferenceArray(slots)) == slots &&
        DecodeAssetReferenceArray(EncodeAssetReferenceArray(std::vector<std::string>{}))->empty() &&
        DecodeAssetReferenceArray(EncodeAssetReferenceArray(std::vector<std::string>{""}))->size() == 1,
        "Reference arrays must round trip, keeping empty slots distinct from an empty array");
    const ManagedAssetFieldRole spawns{ProjectAssetType::Prefab, true};
    Require(ManagedAssetFieldReferences(spawns, fields[11].value) == std::vector<std::string>{slots[0], slots[2]} &&
        ManagedAssetFieldReferences(ManagedAssetFieldRole{ProjectAssetType::Prefab}, "").empty(),
        "Dependency references must skip unassigned slots");
    const auto upper = TransformManagedAssetFieldValue(spawns, std::string(fields[11].value),
        [](const std::string &reference) { return reference + "!"; });
    Require(DecodeAssetReferenceArray(upper) == std::vector<std::string>{slots[0] + "!", "", slots[2] + "!"},
        "Per-element transforms must keep order and empty slots");
    std::reverse(fields.begin(), fields.end());
    Require(ReadManagedAssetFieldMetadata(fields, output, &error) && output.fields == expected,
        "Metadata required writer-specific property ordering");
    std::cout << "Managed asset field metadata checks passed.\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
