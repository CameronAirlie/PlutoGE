#include "PlutoGE/assets/AssetMetadata.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace
{
    void Require(bool value, const char *message)
    {
        if (!value) throw std::runtime_error(message);
    }

    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     ("PlutoGE-metadata-" + PlutoGE::assets::GenerateAssetId());
        Scratch() { std::filesystem::create_directory(root); }
        ~Scratch()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };

    std::string Read(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(static_cast<bool>(input), "Cannot read fixture");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void Write(const std::filesystem::path &path, const std::string &text)
    {
        std::ofstream output(path, std::ios::binary);
        output << text;
        output.close();
        Require(static_cast<bool>(output), "Cannot write fixture");
    }
}

int main()
{
    using namespace PlutoGE::assets;
    try
    {
        Scratch scratch;
        std::string error;
        AssetMetadata metadata{.id = "previous", .importerVersion = 7};
        const std::string source = "PLUTOASSET\t1\r\nID\topaque-id\r\nIMPORTER_VERSION\t3\r\nCUSTOM\tvalue\twith-tabs\r\n\r\n#comment\r\n";
        Require(ParseAssetMetadata(source, metadata, &error) == AssetMetadataStatus::Success, "Valid CRLF metadata failed");
        Require(metadata.id == "opaque-id" && metadata.importerVersion == 3, "Known fields not parsed");
        Require(metadata.extensionRecords.size() == 3, "Unknown records lost");
        metadata.importerVersion = 4;
        std::string serialized;
        Require(SerializeAssetMetadata(metadata, serialized, &error), "Serialization failed");
        AssetMetadata roundTrip;
        Require(ParseAssetMetadata(serialized, roundTrip, &error) == AssetMetadataStatus::Success, "Round trip failed");
        Require(roundTrip.extensionRecords == metadata.extensionRecords && roundTrip.importerVersion == 4, "Extension records changed");
        for (const auto ownership : {AssetOwnership::Authored, AssetOwnership::Source})
        {
            auto explicitMetadata = metadata;
            explicitMetadata.ownership = ownership;
            Require(SerializeAssetMetadata(explicitMetadata, serialized, &error) &&
                    ParseAssetMetadata(serialized, roundTrip, &error) == AssetMetadataStatus::Success &&
                    roundTrip.ownership == ownership && roundTrip.extensionRecords == metadata.extensionRecords,
                    "Explicit ownership round trip lost metadata");
        }
        auto importedMetadata = metadata;
        importedMetadata.ownership = AssetOwnership::Imported;
        serialized = "unchanged";
        Require(!SerializeAssetMetadata(importedMetadata, serialized, &error) && serialized == "unchanged",
                "Metadata allowed importer-derived ownership to be authored");
        AssetMetadata legacy;
        Require(ParseAssetMetadata("PLUTOASSET\t1\nID\tlegacy\n", legacy, &error) == AssetMetadataStatus::Success &&
                legacy.importerVersion == 1, "Legacy default version failed");
        for (const auto &invalid : {
                 std::string("PLUTOASSET\t1\nID\tone\nOWNERSHIP\tIMPORTED\n"),
                 std::string("PLUTOASSET\t1\nID\tone\nOWNERSHIP\tAUTHORED\nOWNERSHIP\tSOURCE\n"),
                 std::string("PLUTOASSET\t1\n"),
                 std::string("PLUTOASSET\t1\nID\tone\nID\ttwo\n"),
                 std::string("PLUTOASSET\t1\nID\tone two\n"),
                 std::string("PLUTOASSET\t1\nID\tone\nIMPORTER_VERSION\t\n"),
                 std::string("PLUTOASSET\t1\nID\tone\nIMPORTER_VERSION\t2junk\n"),
                 std::string("PLUTOASSET\t1\nID\tone\nIMPORTER_VERSION\t4294967296\n"),
                 std::string("PLUTOASSET\t1\nID\tone\nIMPORTER_VERSION\t0\n"),
                 std::string("PLUTOASSET\t1\nID\tone\nIMPORTER_VERSION\t1\nIMPORTER_VERSION\t2\n"),
                 std::string(1024 * 1024 + 1, 'x')})
        {
            AssetMetadata unchanged{.id = "sentinel", .importerVersion = 8};
            Require(ParseAssetMetadata(invalid, unchanged, &error) == AssetMetadataStatus::Invalid, "Invalid metadata accepted");
            Require(unchanged.id == "sentinel" && unchanged.importerVersion == 8, "Parse failure changed output");
            Require(!error.empty(), "Parse failure omitted diagnostic");
        }
        Require(ParseAssetMetadata("PLUTOASSET\t2\nID\tfuture\n", legacy, &error) == AssetMetadataStatus::UnsupportedVersion,
                "Future version not distinguished");
        AssetMetadata injection{.id = "id\nCUSTOM\tbad"};
        serialized = "unchanged";
        Require(!SerializeAssetMetadata(injection, serialized, &error) && serialized == "unchanged", "Identity line injection accepted");
        injection = {.id = "valid", .extensionRecords = {"CUSTOM\tvalue\nID\tbad"}};
        Require(!SerializeAssetMetadata(injection, serialized, &error), "Extension line injection accepted");

        const auto path = GetAssetMetadataPath(scratch.root / "Robot.fbx");
        Require(LoadAssetMetadata(path, roundTrip, &error) == AssetMetadataStatus::Missing, "Missing file status incorrect");
        Require(SaveAssetMetadata(path, metadata, AssetMetadataWriteMode::CreateOnly, &error), "Atomic creation failed");
        const auto original = Read(path);
        Require(!SaveAssetMetadata(path, metadata, AssetMetadataWriteMode::CreateOnly, &error) && Read(path) == original,
                "Create-only replaced existing file");
        metadata.importerVersion = 5;
        Require(SaveAssetMetadata(path, metadata, AssetMetadataWriteMode::ReplaceExisting, &error), "Atomic replacement failed");
        Require(LoadAssetMetadata(path, roundTrip, &error) == AssetMetadataStatus::Success && roundTrip.importerVersion == 5 &&
                roundTrip.extensionRecords == metadata.extensionRecords, "Saved settings or unknown fields lost");
        const auto replaced = Read(path);
        metadata.id = "different-id";
        Require(!SaveAssetMetadata(path, metadata, AssetMetadataWriteMode::ReplaceExisting, &error) && Read(path) == replaced,
                "Replacement changed identity");
        Write(path, "PLUTOASSET\t99\nID\tfuture\n");
        Require(!SaveAssetMetadata(path, metadata, AssetMetadataWriteMode::ReplaceExisting, &error) &&
                Read(path) == "PLUTOASSET\t99\nID\tfuture\n", "Replacement damaged future metadata");
        Require(LoadAssetMetadata(scratch.root, roundTrip, &error) == AssetMetadataStatus::IoError, "Directory treated as metadata");
        Require(!SaveAssetMetadata(scratch.root / "missing" / "file", metadata, AssetMetadataWriteMode::CreateOnly, &error),
                "Missing parent unexpectedly accepted");

        const auto concurrent = scratch.root / "concurrent.plutometa";
        bool first = false;
        bool second = false;
        std::thread a([&] { first = SaveAssetMetadata(concurrent, AssetMetadata{.id = "first"}); });
        std::thread b([&] { second = SaveAssetMetadata(concurrent, AssetMetadata{.id = "second"}); });
        a.join();
        b.join();
        Require(first != second, "Concurrent create did not have exactly one winner");
        Require(LoadAssetMetadata(concurrent, roundTrip, &error) == AssetMetadataStatus::Success &&
                roundTrip.id == (first ? "first" : "second"), "Concurrent create published invalid winner");
        for (const auto &entry : std::filesystem::directory_iterator(scratch.root))
            Require(!entry.is_directory(), "Metadata staging directory leaked");
        std::cout << "Asset metadata tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
