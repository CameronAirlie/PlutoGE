#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetCatalogSerialization.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool value, const char *message)
    {
        if (!value) throw std::runtime_error(message);
    }
}

int main()
{
    using namespace PlutoGE::assets;
    try
    {
        const std::filesystem::path policyRoot = "ProjectRoot";
        Require(IsAssetInfrastructurePath(policyRoot, policyRoot / "Library/Artifacts/cache") &&
                IsAssetInfrastructurePath(policyRoot, policyRoot / ".pluto-import.lock") &&
                IsAssetInfrastructurePath(policyRoot, policyRoot / ".git/objects/data") &&
                IsAssetInfrastructurePath(policyRoot, policyRoot / "Assets/.pluto-metadata-stage/metadata"), "Infrastructure scan policy omitted reserved paths");
        Require(!IsAssetInfrastructurePath(policyRoot, policyRoot / "Assets/Library/Artwork.png") &&
                !IsAssetInfrastructurePath(policyRoot, policyRoot / "LibraryArtwork.png") &&
                !IsAssetInfrastructurePath(policyRoot, std::filesystem::path("OtherRoot/Library/cache")), "Infrastructure policy hid authored or external paths");
        std::string text;
        std::string error;
        for (const auto &reference : {AssetReference{}, AssetReference{"owner", 0},
                 AssetReference{"owner/#%:unicode-\xc3\xa9", std::numeric_limits<std::uint64_t>::max()}})
        {
            Require(SerializeAssetReference(reference, text, &error), "Reference serialization failed");
            AssetReference loaded{"sentinel", 3};
            Require(ParseAssetReference(text, loaded, &error) && loaded == reference, "Reference round trip failed");
        }
        Require(SerializeAssetReference({"owner", 42}, text) && text == "asset://owner#42", "Canonical syntax changed");
        for (const auto *invalid : {"asset://#1", "asset://owner", "asset://owner#", "asset://owner#01",
                 "asset://owner#-1", "asset://owner#18446744073709551616", "asset://owner#1junk",
                 "asset://owner#1#2", "asset://a%GG#0", "asset://a%#0", "asset://a%00#0", "asset://a%20b#0",
                 "asset://a/b#0", "project://owner", "engine://builtin/mesh/cube"})
        {
            AssetReference unchanged{"sentinel", 4};
            Require(!ParseAssetReference(invalid, unchanged, &error), "Malformed reference accepted");
            Require(unchanged == AssetReference{"sentinel", 4}, "Failed parse changed output");
        }
        text = "sentinel";
        Require(!SerializeAssetReference({"", 2}, text) && text == "sentinel", "Ownerless local ID accepted");
        AssetCatalog catalog;
        std::vector<AssetObjectDescriptor> objects{
            {.identity = {"owner", 42}, .type = ProjectAssetType::Mesh, .ownership = AssetOwnership::Imported,
             .name = "Robot", .location = "project://Robot.plutomesh"},
            {.identity = {"owner", 0}, .type = ProjectAssetType::Model, .ownership = AssetOwnership::Source,
             .name = "Robot.fbx", .location = "project://Robot.fbx"},
        };
        Require(catalog.Replace(objects, &error), "Catalog build failed");
        Require(catalog.GetObjects().front().identity.localObjectId == 0, "Catalog order not deterministic");
        const auto *mesh = catalog.Find({"owner", 42});
        Require(mesh && mesh->ownership == AssetOwnership::Imported && mesh->location == "project://Robot.plutomesh", "Object lookup failed");
        Require(!catalog.Find({"owner", 43}) && !catalog.Find({"missing", 0}), "Missing object resolved");
        std::string encodedCatalog;
        Require(SerializeAssetCatalog(catalog, encodedCatalog, &error), "Catalog serialization failed");
        AssetCatalog decodedCatalog;
        Require(ParseAssetCatalog(encodedCatalog, decodedCatalog, &error), "Catalog parse failed");
        Require(decodedCatalog.Find({"owner", 42}) && decodedCatalog.Find({"owner", 42})->location == mesh->location,
                "Catalog round trip lost logical mapping");
        std::string reencoded;
        Require(SerializeAssetCatalog(decodedCatalog, reencoded) && reencoded == encodedCatalog, "Catalog encoding not deterministic");
        std::string crlf;
        for (const char character : encodedCatalog) { if (character == '\n') crlf += '\r'; crlf += character; }
        Require(ParseAssetCatalog(crlf, decodedCatalog), "CRLF catalog rejected");
        const auto *retained = decodedCatalog.Find({"owner", 42});
        for (const auto &invalid : {std::string("PLUTOCATALOG\t99\n"), encodedCatalog + "OBJECT broken\n",
                                   encodedCatalog + encodedCatalog.substr(encodedCatalog.find("OBJECT"))})
            Require(!ParseAssetCatalog(invalid, decodedCatalog) && decodedCatalog.Find({"owner", 42}) == retained,
                    "Invalid/newer/duplicate catalog replaced valid mapping");
        AssetCatalog alias;
        Require(alias.Replace({{.identity={"alias",0}, .location="asset://owner#42"}}), "Cannot create alias fixture");
        Require(!SerializeAssetCatalog(alias, reencoded), "Catalog identity alias serialized as storage location");
        Require(catalog.FindIdentityByLocation("project://Robot.plutomesh") == AssetReference{"owner",42}, "Reverse catalog lookup lost sub-object identity");
        AssetCatalog sharedLocation;
        std::vector<AssetObjectDescriptor> aliases{
            {.identity={"standalone",0}, .type=ProjectAssetType::Texture, .location="project://Shared.png"},
            {.identity={"first-model",12}, .type=ProjectAssetType::Texture, .ownership=AssetOwnership::Imported, .location="project://Shared.png"},
        };
        Require(sharedLocation.Replace(aliases) && sharedLocation.FindIdentityByLocation("project://Shared.png") == AssetReference{"first-model",12},
                "Unique source-owned object did not take precedence");
        aliases.push_back({.identity={"second-model",23}, .type=ProjectAssetType::Texture, .ownership=AssetOwnership::Imported, .location="project://Shared.png"});
        Require(sharedLocation.Replace(aliases) && sharedLocation.FindIdentityByLocation("project://Shared.png") == AssetReference{"standalone",0},
                "Shared imported aliases did not use standalone identity");
        aliases.erase(aliases.begin());
        Require(sharedLocation.Replace(aliases) && !sharedLocation.FindIdentityByLocation("project://Shared.png"), "Ambiguous location selected arbitrary owner");
        Require(!catalog.FindIdentityByLocation("project://Missing.png"), "Missing location returned identity");
        objects.push_back(objects.front());
        Require(!catalog.Replace(objects, &error) && catalog.Find({"owner", 42}) == mesh, "Duplicate replaced catalog snapshot");
        Require(error.find("asset://owner#42") != std::string::npos, "Duplicate diagnostic omitted identity");
        objects = {{.identity = {"invalid", 0}}};
        Require(!catalog.Replace(objects, &error) && catalog.Find({"owner", 42}) == mesh, "Invalid location replaced catalog");
        std::cout << "Asset catalog tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
