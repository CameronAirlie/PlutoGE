#pragma once

#include "PlutoGE/platform/ContentDigest.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace PlutoGE::assetimport
{
    struct ArtifactInput
    {
        std::string identity;
        std::filesystem::path path;
        content::ContentDigest digest{};
    };

    struct ArtifactRecipe
    {
        std::string importer;
        std::uint32_t version = 0;
        std::string target;
        content::ContentDigest settings{};
        std::vector<ArtifactInput> inputs;
    };

    struct ArtifactOutput
    {
        std::filesystem::path relativePath;
        content::ContentDigest digest{};
    };

    struct ArtifactManifest
    {
        content::ContentDigest key{};
        ArtifactRecipe recipe;
        std::vector<ArtifactOutput> outputs;
    };

    enum class ArtifactCacheStatus { Hit, Missing, Corrupt, IoError };

    bool ComputeArtifactKey(const ArtifactRecipe &recipe, content::ContentDigest &key, std::string *errorMessage = nullptr);
    bool AreArtifactInputsCurrent(const ArtifactRecipe &recipe, std::string *errorMessage = nullptr);

    // Final read-only acceptance check against the published tree. Outputs
    // must remain ordinary files with the cached digests; symlink traversal is
    // rejected. Call under the project publication lock before accepting a journal.
    bool ValidateArtifactPublication(const ArtifactManifest &generation, const std::filesystem::path &outputRoot,
                                     std::string *errorMessage = nullptr);

    // Immutable, content-addressed import generations. This storage primitive
    // performs no editor registration, runtime loading, or source metadata edits.
    class ArtifactCache
    {
    public:
        explicit ArtifactCache(std::filesystem::path root) : m_root(std::move(root)) {}
        std::filesystem::path GetDirectory(const content::ContentDigest &key) const;
        ArtifactCacheStatus Find(const content::ContentDigest &key, ArtifactManifest &manifest,
                                 std::string *errorMessage = nullptr) const;
        // Enumerates validated generations deterministically. A caller-supplied
        // predicate applies importer-specific request matching; storage remains
        // independent of model settings and editor state. An index can replace
        // enumeration without changing the publication or predicate contract.
        ArtifactCacheStatus FindMatching(const std::function<bool(const ArtifactManifest &)> &matches,
                                         ArtifactManifest &manifest, std::string *errorMessage = nullptr) const;
        // Disposable immutable request hints narrow lookup before falling back
        // to a full search. Every candidate still passes the predicate and hashes.
        ArtifactCacheStatus FindMatchingForRequest(std::string_view requestIdentity,
            const std::function<bool(const ArtifactManifest &)> &matches,
            ArtifactManifest &manifest, std::string *errorMessage = nullptr) const;
        bool RememberRequestGeneration(std::string_view requestIdentity, const content::ContentDigest &key,
                                       std::string *errorMessage = nullptr) const;
        bool Store(const ArtifactRecipe &recipe, const std::filesystem::path &sourceRoot,
                   const std::vector<std::filesystem::path> &relativeOutputs, ArtifactManifest &manifest,
                   std::string *errorMessage = nullptr) const;
    private:
        ArtifactCacheStatus ReadGeneration(const content::ContentDigest &key, ArtifactManifest &manifest,
                                           bool verifyOutputs, std::string *errorMessage) const;
        std::filesystem::path m_root;
    };
}
