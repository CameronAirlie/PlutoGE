#pragma once

#include "PlutoGE/platform/ContentDigest.h"
#include <filesystem>
#include <chrono>
#include <stop_token>
#include <string>
#include <vector>

namespace PlutoGE::assets { class Project; }
namespace PlutoGE::assetimport
{
    inline constexpr std::size_t kMaxArtifactCollectionBatch = 256;
    inline constexpr auto kArtifactCollectionGracePeriod = std::chrono::hours(24);
    enum class ArtifactCollectionDisposition { Active, InUse, Recent, Eligible, Invalid };
    struct ArtifactCollectionEntry
    {
        content::ContentDigest generation{};
        content::ContentDigest manifestDigest{};
        std::uintmax_t bytes = 0;
        std::size_t files = 0;
        ArtifactCollectionDisposition disposition = ArtifactCollectionDisposition::Invalid;
        std::string diagnostic;
    };
    struct ArtifactCollectionInspection
    {
        std::filesystem::path projectRoot;
        std::vector<ArtifactCollectionEntry> entries;
    };
    struct ArtifactCollectionResult
    {
        std::vector<content::ContentDigest> collected;
        // A failed filesystem removal leaves only disposable cache bytes here.
        // Quarantine is inside Library and never includes source/authored files.
        std::vector<std::filesystem::path> quarantined;
    };

    // Explicit dry run. Acquires project/cache writer ownership, verifies complete
    // ordinary generation inventories and hashes, and probes exclusive leases.
    // Active and leased generations are retained. Failure preserves prior output.
    bool InspectArtifactCache(const assets::Project &project, ArtifactCollectionInspection &output,
        std::string *errorMessage = nullptr, std::stop_token stop = {});
    // Revalidates ALL eligible candidates before mutation under the same writer
    // locks plus exclusive per-generation locks. A stale active reference, lease,
    // manifest or inventory rejects the batch. Filesystem failure after removal
    // starts reports completed/quarantined work; this is disposable-cache cleanup,
    // not an authored-file transaction or a power-loss durability guarantee.
    bool CollectArtifactCache(const assets::Project &project, const ArtifactCollectionInspection &inspection,
        ArtifactCollectionResult &output, std::string *errorMessage = nullptr, std::stop_token stop = {});
}
