#include "PlutoGE/assets/ArtifactGenerationLock.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace PlutoGE::assets
{
    struct ArtifactGenerationLock::State
    {
        std::filesystem::path projectRoot;
        content::ContentDigest generation{};
        ArtifactGenerationLockMode mode = ArtifactGenerationLockMode::SharedReader;
#ifdef _WIN32
        HANDLE handle = INVALID_HANDLE_VALUE;
        ~State() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
        int descriptor = -1;
        ~State() { if (descriptor >= 0) close(descriptor); }
#endif
    };
    namespace
    {
        void OrdinaryDirectory(const std::filesystem::path &path, bool create)
        {
            std::error_code error;
            auto status = std::filesystem::symlink_status(path, error);
            if (error == std::errc::no_such_file_or_directory) error.clear();
            if (!error && !std::filesystem::exists(status) && create)
            {
                std::filesystem::create_directory(path, error);
                if (!error) status = std::filesystem::symlink_status(path, error);
            }
            if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
                throw std::runtime_error("Generation lock requires ordinary directories: " + path.string());
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("Generation lock cannot traverse reparse points.");
#endif
        }
    }
    ArtifactGenerationLock::ArtifactGenerationLock() = default;
    ArtifactGenerationLock::~ArtifactGenerationLock() = default;

    bool ArtifactGenerationLock::Owns(const std::filesystem::path &root, const content::ContentDigest &generation,
        ArtifactGenerationLockMode mode) const
    {
        return m_state && m_state->generation == generation && m_state->mode == mode &&
            content::IsPathWithinDirectory(root, m_state->projectRoot, true) &&
            content::IsPathWithinDirectory(m_state->projectRoot, root, true);
    }

    bool ArtifactGenerationLock::TryAcquire(const std::filesystem::path &projectRoot, const content::ContentDigest &generation,
        ArtifactGenerationLockMode mode, std::string *errorMessage)
    {
        try
        {
            if (m_state) throw std::runtime_error("Generation lock is already held by this owner.");
            if (mode != ArtifactGenerationLockMode::SharedReader && mode != ArtifactGenerationLockMode::ExclusiveCollector)
                throw std::runtime_error("Invalid generation lock mode.");
            if (std::none_of(generation.begin(), generation.end(), [](auto byte) { return byte != 0; }))
                throw std::runtime_error("Generation lock requires a nonempty generation key.");
            const auto root = std::filesystem::canonical(projectRoot);
            OrdinaryDirectory(root, false);
            const auto library = root / "Library";
            OrdinaryDirectory(library, false);
            const auto locks = library / "GenerationLocks";
            OrdinaryDirectory(locks, true);
            const auto path = locks / (content::DigestToHex(generation) + ".lock");
            std::error_code error;
            const auto before = std::filesystem::symlink_status(path, error);
            if (error == std::errc::no_such_file_or_directory) error.clear();
            if (error || (std::filesystem::exists(before) && !std::filesystem::is_regular_file(before)))
                throw std::runtime_error("Generation lock path is not an ordinary file.");
            auto state = std::make_unique<State>();
#ifdef _WIN32
            state->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            BY_HANDLE_FILE_INFORMATION info{};
            OVERLAPPED offset{};
            const auto flags = LOCKFILE_FAIL_IMMEDIATELY |
                (mode == ArtifactGenerationLockMode::ExclusiveCollector ? LOCKFILE_EXCLUSIVE_LOCK : 0);
            if (state->handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(state->handle, &info) ||
                (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
                !LockFileEx(state->handle, flags, 0, 1, 0, &offset))
#else
            state->descriptor = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            struct stat info{};
            int status = -1;
            if (state->descriptor >= 0 && fstat(state->descriptor, &info) == 0 && S_ISREG(info.st_mode))
                do { status = flock(state->descriptor, (mode == ArtifactGenerationLockMode::ExclusiveCollector ? LOCK_EX : LOCK_SH) | LOCK_NB); }
                while (status < 0 && errno == EINTR);
            if (state->descriptor < 0 || status < 0)
#endif
                throw std::runtime_error("Generation is in use, being collected, or its lock is unavailable.");
            // Acquire first, then verify existence: a concurrent collector either
            // owns the exclusive lock or must wait until this reader releases it.
            OrdinaryDirectory(library / "Artifacts", false);
            const auto directory = library / "Artifacts" / content::DigestToHex(generation);
            OrdinaryDirectory(directory, false);
            const auto canonical = std::filesystem::canonical(directory);
            if (!content::IsPathWithinDirectory(canonical, root))
                throw std::runtime_error("Generation directory escapes its project.");
            state->projectRoot = root; state->generation = generation; state->mode = mode;
            m_state = std::move(state);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = error.what();
            return false;
        }
    }
}
