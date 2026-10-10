#include "PlutoGE/asset_import/ArtifactCollection.h"
#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace PlutoGE;
namespace {
void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path &path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary); file << text;
    Require(static_cast<bool>(file), "Fixture write failed");
}
void AgeGeneration(const std::filesystem::path &directory) {
#ifdef _WIN32
    // Open a directory handle explicitly when aging Windows fixtures.
    const auto handle = CreateFileW(directory.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    Require(handle != INVALID_HANDLE_VALUE, "Cannot open generation timestamp");
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER ticks{}; ticks.LowPart=now.dwLowDateTime; ticks.HighPart=now.dwHighDateTime;
    ticks.QuadPart -= 48ULL * 60 * 60 * 10000000;
    FILETIME old{ticks.LowPart,ticks.HighPart};
    const bool changed = SetFileTime(handle, nullptr, nullptr, &old);
    CloseHandle(handle); Require(changed, "Cannot age generation fixture");
#else
    std::filesystem::last_write_time(directory, std::filesystem::file_time_type::clock::now() - std::chrono::hours(48));
#endif
}
struct Scratch {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("PlutoGE-collection-" + assets::GenerateAssetId());
    Scratch() { std::filesystem::create_directories(root / "Assets"); }
    ~Scratch() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
};
}
int main() {
    try {
        Scratch scratch;
        assets::Project project(scratch.root / "Test.plutoproject", assets::ProjectManifest{});
        assetimport::ArtifactCollectionInspection empty;
        assetimport::ArtifactCollectionResult emptyResult;
        std::string error;
        Require(assetimport::InspectArtifactCache(project, empty, &error) && empty.entries.empty(), error);
        Require(assetimport::CollectArtifactCache(project, empty, emptyResult, &error) && emptyResult.collected.empty(), error);
        const auto source = scratch.root / "Fixture";
        Write(source / "input", "source"); Write(source / "output", "artifact");
        assetimport::ArtifactRecipe recipe{.importer="fixture", .version=1, .target="cpu",
            .settings=content::HashContent(std::as_bytes(std::span("settings", 8))), .inputs={}};
        Require(content::HashFileContent(source / "input", recipe.settings), "Fixture hashing failed");
        recipe.inputs.push_back({"input", source / "input", recipe.settings});
        assetimport::ArtifactCache cache(scratch.root / "Library/Artifacts");
        assetimport::ArtifactManifest manifest;
        Require(cache.Store(recipe, source, {"output"}, manifest, &error), error);
        const auto key = manifest.key;
        assetimport::ArtifactCollectionInspection inspection;
        Require(assetimport::InspectArtifactCache(project, inspection, &error), error);
        Require(inspection.entries.size()==1 && inspection.entries[0].disposition==assetimport::ArtifactCollectionDisposition::InUse,
            "Live manifest did not retain generation");
        manifest.generationLease.reset();
        Require(assetimport::InspectArtifactCache(project, inspection, &error) &&
            inspection.entries[0].disposition == assetimport::ArtifactCollectionDisposition::Recent, "New generation bypassed grace period");
        AgeGeneration(cache.GetDirectory(key));
        Require(assetimport::InspectArtifactCache(project, inspection, &error), error);
        Require(inspection.entries[0].disposition==assetimport::ArtifactCollectionDisposition::Eligible, "Unused generation not eligible");
        assetimport::ArtifactCollectionResult result;
        {
            assets::ProjectAssetLock writer;
            Require(writer.TryAcquire(scratch.root, &error), error);
            Require(!assetimport::CollectArtifactCache(project, inspection, result, &error), "Collection ignored project writer");
        }
        Write(scratch.root / ".pluto-import-transactions/pending", "transaction");
        Require(!assetimport::CollectArtifactCache(project, inspection, result, &error), "Collection ignored pending transaction");
        std::filesystem::remove(scratch.root / ".pluto-import-transactions/pending");
        auto wrongProject = inspection; wrongProject.projectRoot = source;
        Require(!assetimport::CollectArtifactCache(project, wrongProject, result, &error), "Foreign inspection was collected");
        Require(cache.Find(key, manifest, &error)==assetimport::ArtifactCacheStatus::Hit, error);
        Require(!assetimport::CollectArtifactCache(project, inspection, result, &error) && std::filesystem::exists(cache.GetDirectory(key)),
            "Reader acquired after inspection was deleted");
        manifest.generationLease.reset();
        auto duplicated=inspection; duplicated.entries.push_back(inspection.entries.front());
        Require(!assetimport::CollectArtifactCache(project, duplicated, result, &error) && std::filesystem::exists(cache.GetDirectory(key)),
            "Duplicate generation was collected");
        auto stale=inspection; ++stale.entries[0].bytes;
        Require(!assetimport::CollectArtifactCache(project, stale, result, &error) && std::filesystem::exists(cache.GetDirectory(key)),
            "Stale inventory was collected");
        Write(cache.GetDirectory(key)/"unexpected", "authored data");
        Require(!assetimport::CollectArtifactCache(project, inspection, result, &error), "Unexpected file was deleted");
        Require(assetimport::InspectArtifactCache(project, stale, &error) && stale.entries[0].disposition==assetimport::ArtifactCollectionDisposition::Invalid,
            "Unexpected file not reported");
        std::filesystem::remove(cache.GetDirectory(key)/"unexpected");
        Require(!assetimport::CollectArtifactCache(project, inspection, result, &error), "Recently touched generation bypassed grace period");
        AgeGeneration(cache.GetDirectory(key));
        std::stop_source stop; stop.request_stop();
        Require(!assetimport::CollectArtifactCache(project, inspection, result, &error, stop.get_token()), "Cancelled collection ran");
        Require(assetimport::CollectArtifactCache(project, inspection, result, &error), error);
        Require(result.collected.size()==1 && result.collected[0]==key && result.quarantined.empty() && !std::filesystem::exists(cache.GetDirectory(key)),
            "Eligible generation not collected");
        Require(std::filesystem::exists(source/"input") && std::filesystem::exists(source/"output"), "Source files removed");
        std::cout << "Artifact collection tests passed\n";
        return 0;
    } catch(const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
