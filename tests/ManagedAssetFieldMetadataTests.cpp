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
        {"Text", "asset://ordinary-text#0"}, {"Count", "7", false}
    };
    Require(ReadManagedAssetFieldMetadata(fields, output, &error) && output.declared && output.fields.size() == 4 &&
        output.fields.at("Template") == ManagedAssetFieldKind::Prefab && !output.fields.contains("Text"),
        "Explicit metadata lost roles or promoted ordinary strings");
    for (const auto &[name, kind] : output.fields)
    {
        (void)name;
        Require(ParseManagedAssetFieldKind(ManagedAssetFieldKindName(kind)) == kind &&
            ManagedAssetFieldAssetType(kind) != ProjectAssetType::Unknown, "Managed field kind did not round trip");
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
    broken = fields; broken[2].value = "Texture"; invalid(broken);
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
