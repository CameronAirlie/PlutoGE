#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace PlutoGE::assets
{
    // OS-held lock: process termination releases ownership automatically. The
    // persistent lock file is never deleted, avoiding unlink/recreate races.
    class ProjectAssetLock
    {
    public:
        ProjectAssetLock();
        ~ProjectAssetLock();
        ProjectAssetLock(const ProjectAssetLock &) = delete;
        ProjectAssetLock &operator=(const ProjectAssetLock &) = delete;
        bool TryAcquire(const std::filesystem::path &projectRoot, std::string *errorMessage = nullptr);
    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
