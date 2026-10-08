#include "PlutoGE/assets/ProjectAssetLock.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace PlutoGE::assets
{
    struct ProjectAssetLock::State
    {
#ifdef _WIN32
        HANDLE handle = INVALID_HANDLE_VALUE;
        ~State() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
        int descriptor = -1;
        ~State() { if (descriptor >= 0) close(descriptor); }
#endif
    };
    ProjectAssetLock::ProjectAssetLock() = default;
    ProjectAssetLock::~ProjectAssetLock() = default;

    bool ProjectAssetLock::TryAcquire(const std::filesystem::path &projectRoot, std::string *errorMessage)
    {
        if (m_state)
        {
            if (errorMessage) *errorMessage = "Asset lock is already held by this operation.";
            return false;
        }
        auto state = std::make_unique<State>();
        const auto path = projectRoot / ".pluto-import.lock";
#ifdef _WIN32
        state->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (state->handle == INVALID_HANDLE_VALUE)
#else
        state->descriptor = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        int status = -1;
        if (state->descriptor >= 0)
            do { status = flock(state->descriptor, LOCK_EX | LOCK_NB); } while (status < 0 && errno == EINTR);
        if (state->descriptor < 0 || status < 0)
#endif
        {
            if (errorMessage) *errorMessage = "Cannot acquire project asset lock; another import, cook or asset operation may be active, or the project is not writable: " + path.string();
            return false;
        }
        m_state = std::move(state);
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
