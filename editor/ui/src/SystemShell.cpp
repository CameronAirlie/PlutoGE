#include "PlutoGE/ui/SystemShell.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>

#include <thread>
extern char **environ;
#endif

namespace PlutoGE::ui
{
    namespace
    {
        bool Fail(std::string *errorMessage, std::string message)
        {
            if (errorMessage) *errorMessage = std::move(message);
            return false;
        }

#ifndef _WIN32
        // Starts `arguments[0]` from PATH. With `wait`, returns whether it exited with status 0.
        bool Spawn(const std::vector<std::string> &arguments, bool wait, std::string *errorMessage)
        {
            std::vector<char *> argv;
            argv.reserve(arguments.size() + 1);
            for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
            argv.push_back(nullptr);

            pid_t pid = 0;
            if (const int error = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ); error != 0)
            {
                return Fail(errorMessage, "Could not start " + arguments[0] + ": " + std::strerror(error));
            }
            int status = 0;
            if (wait)
            {
                if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
                {
                    return Fail(errorMessage, arguments[0] + " did not complete successfully.");
                }
                return true;
            }
            // Openers such as xdg-open exit once the target application starts;
            // reap them off the UI thread so they never linger as zombies.
            std::thread([pid]()
                        {
                            int exitStatus = 0;
                            waitpid(pid, &exitStatus, 0);
                        })
                .detach();
            return true;
        }
#endif

        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }
    }

    std::string MakeFileUri(const std::filesystem::path &path)
    {
        std::string uri = "file://";
        std::error_code error;
        const auto generic = std::filesystem::absolute(path, error).generic_u8string();
        const std::string_view text(reinterpret_cast<const char *>(generic.data()), generic.size());
#ifdef _WIN32
        uri += '/';
#endif
        for (const unsigned char character : text)
        {
            if (std::isalnum(character) || character == '/' || character == '-' || character == '_' ||
                character == '.' || character == '~' || character == ':')
            {
                uri += static_cast<char>(character);
            }
            else
            {
                char encoded[4];
                std::snprintf(encoded, sizeof(encoded), "%%%02X", character);
                uri += encoded;
            }
        }
        return uri;
    }

    bool OpenWithSystem(const std::filesystem::path &path, std::string *errorMessage)
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error))
        {
            return Fail(errorMessage, "Path does not exist: " + Utf8(path));
        }
#ifdef _WIN32
        const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        return result > 32 || Fail(errorMessage, "Windows could not open " + Utf8(path));
#elif defined(__APPLE__)
        return Spawn({"open", Utf8(path)}, false, errorMessage);
#else
        return Spawn({"xdg-open", Utf8(path)}, false, errorMessage);
#endif
    }

    bool RevealInFileManager(const std::filesystem::path &path, std::string *errorMessage)
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error))
        {
            return Fail(errorMessage, "Path does not exist: " + Utf8(path));
        }
#ifdef _WIN32
        const std::wstring arguments = L"/select,\"" + path.wstring() + L"\"";
        const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
        return result > 32 || Fail(errorMessage, "Explorer could not show " + Utf8(path));
#elif defined(__APPLE__)
        return Spawn({"open", "-R", Utf8(path)}, false, errorMessage);
#else
        // ShowItems selects the item in Nautilus, Dolphin, Nemo, Thunar and others.
        // It returns quickly, so waiting tells us whether a file manager handled it.
        if (Spawn({"dbus-send", "--session", "--print-reply", "--dest=org.freedesktop.FileManager1",
                   "--type=method_call", "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1.ShowItems",
                   "array:string:" + MakeFileUri(path), "string:"},
                  true, nullptr))
        {
            return true;
        }
        return OpenWithSystem(std::filesystem::is_directory(path, error) ? path : path.parent_path(), errorMessage);
#endif
    }
}
