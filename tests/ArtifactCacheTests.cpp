#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/assets/AssetMetadata.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    PlutoGE::content::ContentDigest Digest(std::string_view text)
    {
        return PlutoGE::content::HashContent(std::as_bytes(std::span(text.data(), text.size())));
    }
    void Write(const std::filesystem::path &path, std::string_view bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        Require(static_cast<bool>(output), "Cannot write fixture");
    }
    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-artifact-cache-" + PlutoGE::assets::GenerateAssetId());
        Scratch() { std::filesystem::create_directory(root); }
        ~Scratch() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    };
}

int main()
{
    using namespace PlutoGE;
    try
    {
        Require(content::DigestToHex(Digest("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Empty SHA-256 vector failed");
        Require(content::DigestToHex(Digest("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "ABC SHA-256 vector failed");
        const std::string boundary = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        Require(content::DigestToHex(Digest(boundary)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "Padding boundary SHA-256 vector failed");
        const std::string million(1000000, 'a');
        Require(content::DigestToHex(Digest(million)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "Long SHA-256 vector failed");
        content::ContentHasher streaming;
        for (const char value : boundary) streaming.Update(std::as_bytes(std::span(&value, 1)));
        Require(streaming.Finalize() == Digest(boundary) && streaming.Finalize() == Digest(boundary), "Streaming/finalization changed digest");
        content::ContentDigest decoded{};
        Require(content::ParseContentDigest(content::DigestToHex(Digest(boundary)), decoded) && decoded == Digest(boundary), "Digest hex round trip failed");
        const auto unchanged = decoded;
        Require(!content::ParseContentDigest(std::string(64, 'x'), decoded) && decoded == unchanged, "Invalid digest changed output");

        Scratch scratch;
        const auto sourceRoot = scratch.root / "Source";
        Write(sourceRoot / "model.fbx", "source-model");
        Write(sourceRoot / "texture.png", "source-texture");
        Write(sourceRoot / "nested/model.plutomesh", "generated-mesh");
        Write(sourceRoot / "material.plutomaterial", "generated-material");
        assetimport::ArtifactRecipe recipe{
            .importer = "model",
            .version = 1,
            .target = "test-cpu",
            .settings = Digest("settings"),
            .inputs = {{"source", sourceRoot / "model.fbx", Digest("source-model")},
                       {"texture", sourceRoot / "texture.png", Digest("source-texture")}},
        };
        content::ContentDigest key;
        std::string error;
        Require(assetimport::ComputeArtifactKey(recipe, key, &error), error);
        auto reordered = recipe;
        std::reverse(reordered.inputs.begin(), reordered.inputs.end());
        content::ContentDigest other;
        Require(assetimport::ComputeArtifactKey(reordered, other) && key == other, "Input order changed artifact identity");
        for (unsigned change = 0; change < 4; ++change)
        {
            auto changed = recipe;
            if (change == 0) changed.version++;
            if (change == 1) changed.target += "-other";
            if (change == 2) changed.settings = Digest("other-settings");
            if (change == 3) changed.inputs[1].digest = Digest("other-texture");
            Require(assetimport::ComputeArtifactKey(changed, other) && key != other, "Relevant import change did not invalidate key");
        }
        auto duplicate = recipe;
        duplicate.inputs.push_back(duplicate.inputs.front());
        Require(!assetimport::ComputeArtifactKey(duplicate, other, &error), "Duplicate input identity accepted");
        for (unsigned change = 0; change < 3; ++change)
        {
            auto invalid = recipe;
            if (change == 0) invalid.importer += "\ninvalid";
            if (change == 1) invalid.inputs.front().identity += "\rinvalid";
            if (change == 2) invalid.inputs.front().path = "relative.fbx";
            Require(!assetimport::ComputeArtifactKey(invalid, other), "Unserializable or relative recipe accepted");
        }
        assetimport::ArtifactCache cache(scratch.root / "Library/Artifacts");
        assetimport::ArtifactManifest manifest;
        Require(cache.Find(key, manifest) == assetimport::ArtifactCacheStatus::Missing, "Absent generation not missing");
        Require(cache.Store(recipe, sourceRoot, {"nested/model.plutomesh", "material.plutomaterial"}, manifest, &error), error);
        Require(manifest.key == key && manifest.outputs.size() == 2, "Stored manifest incomplete");
        assetimport::ArtifactManifest hit;
        Require(cache.Find(key, hit, &error) == assetimport::ArtifactCacheStatus::Hit, error);
        Require(assetimport::AreArtifactInputsCurrent(hit.recipe, &error), error);
        Require(cache.FindMatching([&](const auto &candidate) { return candidate.recipe.importer == "model" && assetimport::AreArtifactInputsCurrent(candidate.recipe); }, hit, &error)
                    == assetimport::ArtifactCacheStatus::Hit && hit.key == key, "Validated generation search failed");
        Require(cache.FindMatching([](const auto &) { return false; }, hit, &error) == assetimport::ArtifactCacheStatus::Missing && hit.key == key,
                "Unmatched cache search changed output");
        Require(cache.Store(recipe, sourceRoot, {"material.plutomaterial", "nested/model.plutomesh"}, manifest, &error), "Identical generation did not reuse cache");
        Write(sourceRoot / "texture.png", "changed-source");
        Require(!assetimport::AreArtifactInputsCurrent(hit.recipe, &error), "Changed dependency treated as current");
        Require(!cache.Store(recipe, sourceRoot, {"nested/model.plutomesh"}, manifest, &error), "Stale recipe stored");
        Write(sourceRoot / "texture.png", "source-texture");
        Write(cache.GetDirectory(key) / "Files/nested/model.plutomesh", "corrupt");
        Require(cache.Find(key, hit, &error) == assetimport::ArtifactCacheStatus::Corrupt, "Corrupt artifact accepted");
        Require(cache.FindMatching([](const auto &) { return true; }, hit) == assetimport::ArtifactCacheStatus::Missing,
                "Generation search returned corrupt artifact payload");
        Require(!cache.Store(recipe, sourceRoot, {"nested/model.plutomesh", "material.plutomaterial"}, manifest, &error), "Leased corrupt generation was displaced");
        manifest.generationLease.reset();
        hit.generationLease.reset();
        Require(cache.Store(recipe, sourceRoot, {"nested/model.plutomesh", "material.plutomaterial"}, manifest, &error), "Unleased corrupt generation did not rebuild");
        Require(cache.Find(key, hit) == assetimport::ArtifactCacheStatus::Hit, "Rebuilt generation invalid");
        auto unrelatedRecipe = recipe;
        unrelatedRecipe.importer = "unrelated-importer";
        assetimport::ArtifactManifest unrelated;
        Require(cache.Store(unrelatedRecipe, sourceRoot, {"nested/model.plutomesh", "material.plutomaterial"}, unrelated, &error), error);
        const bool remembered = cache.RememberRequestGeneration("owner/request", key, &error);
        Require(remembered, "Cannot remember request generation: " + error);
        unsigned unrelatedVisits = 0;
        auto indexedMatch = [&](const auto &candidate)
        {
            if (candidate.recipe.importer != "model") ++unrelatedVisits;
            return candidate.key == key;
        };
        Require(cache.FindMatchingForRequest("owner/request", indexedMatch, hit, &error) == assetimport::ArtifactCacheStatus::Hit && hit.key == key && unrelatedVisits == 0,
                "Indexed request searched unrelated generation manifests");
        Require(cache.FindMatchingForRequest("unindexed/request", indexedMatch, hit, &error) == assetimport::ArtifactCacheStatus::Hit && hit.key == key,
                "Missing request index did not fall back to generation search");
        const auto hintRoot = scratch.root / "Library/Artifacts/Requests-v1" / content::DigestToHex(Digest("owner/request")).substr(0, 32);
        std::filesystem::create_directory(hintRoot / "invalid-marker");
        std::filesystem::create_directory(hintRoot / std::string(64, '0'));
        Require(cache.FindMatchingForRequest("owner/request", indexedMatch, hit) == assetimport::ArtifactCacheStatus::Hit && hit.key == key,
                "Malformed or stale request marker blocked a valid generation");
        const auto retainedHit = hit.key;
        Require(cache.FindMatchingForRequest("owner/request", [](const auto &) { return false; }, hit) == assetimport::ArtifactCacheStatus::Missing && hit.key == retainedHit,
                "Unmatched request index changed output");
        Write(sourceRoot / "nested/model.plutomesh", "nondeterministic-output");
        Require(!cache.Store(recipe, sourceRoot, {"nested/model.plutomesh", "material.plutomaterial"}, manifest, &error), "Different output accepted for same key");
        Require(cache.Find(key, hit) == assetimport::ArtifactCacheStatus::Hit, "Nondeterministic producer damaged valid cache");
        Require(!cache.Store(recipe, sourceRoot, {"../outside"}, manifest, &error), "Escaping artifact output accepted");
        content::ContentDigest fileDigest;
        Require(content::HashFileContent(sourceRoot / "model.fbx", fileDigest) && fileDigest == Digest("source-model"), "File hashing differs from byte hashing");
        std::cout << "Artifact cache and content digest tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
