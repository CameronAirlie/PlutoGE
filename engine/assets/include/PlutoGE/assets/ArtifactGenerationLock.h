#pragma once

#include "PlutoGE/platform/ContentDigest.h"
#include <filesystem>
#include <memory>
#include <string>

namespace PlutoGE::assets
{
    enum class ArtifactGenerationLockMode { SharedReader, ExclusiveCollector };

    // OS-held, cross-process generation ownership. The lock file lives outside
    // the immutable generation and is never unlinked/recreated. Readers acquire
    // before opening payloads; collectors hold the project writer lock AND this
    // exclusive lock while revalidating references and removing a generation.
    class ArtifactGenerationLock
    {
    public:
        ArtifactGenerationLock();
        ~ArtifactGenerationLock();
        ArtifactGenerationLock(const ArtifactGenerationLock &) = delete;
        ArtifactGenerationLock &operator=(const ArtifactGenerationLock &) = delete;
        bool TryAcquire(const std::filesystem::path &projectRoot, const content::ContentDigest &generation,
            ArtifactGenerationLockMode mode, std::string *errorMessage = nullptr);
        bool IsHeld() const noexcept { return static_cast<bool>(m_state); }
        bool Owns(const std::filesystem::path &canonicalProjectRoot, const content::ContentDigest &generation,
            ArtifactGenerationLockMode mode) const;
    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
