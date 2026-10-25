#include "PlutoGE/ui/SystemShell.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
#include <unistd.h>

#include <cerrno>

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

    ProcessResult RunProcess(const std::vector<std::string> &arguments)
    {
        ProcessResult result;
        if (arguments.empty())
        {
            return result;
        }
#ifdef _WIN32
        std::wstring commandLine;
        for (const auto &argument : arguments)
        {
            // CommandLineToArgvW quoting: wrap in quotes, escape quotes and the backslashes before them.
            const auto wide = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(argument.data()), argument.size())).wstring();
            commandLine += commandLine.empty() ? L"\"" : L" \"";
            std::size_t backslashes = 0;
            for (const wchar_t character : wide)
            {
                if (character == L'\\') { ++backslashes; continue; }
                if (character == L'"') commandLine.append(backslashes * 2 + 1, L'\\');
                else commandLine.append(backslashes, L'\\');
                backslashes = 0;
                commandLine += character;
            }
            commandLine.append(backslashes * 2, L'\\');
            commandLine += L'"';
        }
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return result;
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = writePipe;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        result.started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        CloseHandle(writePipe);
        if (result.started)
        {
            char buffer[4096];
            DWORD read = 0;
            while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) result.output.append(buffer, read);
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD exitCode = 0;
            GetExitCodeProcess(process.hProcess, &exitCode);
            result.exitStatus = static_cast<int>(exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
        CloseHandle(readPipe);
        return result;
#else
        int pipeEnds[2];
        if (pipe(pipeEnds) != 0)
        {
            return result;
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeEnds[0]);
        posix_spawn_file_actions_addclose(&actions, pipeEnds[1]);
        std::vector<char *> argv;
        for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
        argv.push_back(nullptr);
        pid_t pid = 0;
        result.started = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ) == 0;
        posix_spawn_file_actions_destroy(&actions);
        close(pipeEnds[1]);
        if (result.started)
        {
            char buffer[4096];
            for (ssize_t read; (read = ::read(pipeEnds[0], buffer, sizeof(buffer))) != 0;)
            {
                if (read < 0)
                {
                    if (errno == EINTR) continue;
                    break;
                }
                result.output.append(buffer, static_cast<std::size_t>(read));
            }
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            // posix_spawnp reports a missing program as exit status 127.
            if (WIFEXITED(status)) result.exitStatus = WEXITSTATUS(status);
            if (result.exitStatus == 127) result.started = false;
        }
        close(pipeEnds[0]);
        return result;
#endif
    }

    std::string PercentEncodePath(std::string_view utf8Path)
    {
        std::string encoded;
        encoded.reserve(utf8Path.size());
        for (const unsigned char character : utf8Path)
        {
            if (std::isalnum(character) || character == '/' || character == '-' || character == '_' ||
                character == '.' || character == '~' || character == ':')
            {
                encoded += static_cast<char>(character);
            }
            else
            {
                char escape[4];
                std::snprintf(escape, sizeof(escape), "%%%02X", character);
                encoded += escape;
            }
        }
        return encoded;
    }

    std::string MakeFileUri(const std::filesystem::path &path)
    {
        std::error_code error;
        const auto generic = std::filesystem::absolute(path, error).generic_u8string();
        const std::string_view text(reinterpret_cast<const char *>(generic.data()), generic.size());
#ifdef _WIN32
        return "file:///" + PercentEncodePath(text);
#else
        return "file://" + PercentEncodePath(text);
#endif
    }

    bool MoveToFreedesktopTrash(const std::filesystem::path &path, const std::filesystem::path &trashDirectory,
                                std::string *errorMessage)
    {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
        if (error || !std::filesystem::exists(std::filesystem::symlink_status(absolute, error)))
        {
            return Fail(errorMessage, "Path does not exist: " + Utf8(path));
        }
        const auto files = trashDirectory / "files";
        const auto info = trashDirectory / "info";
        std::filesystem::create_directories(files, error);
        if (!error) std::filesystem::create_directories(info, error);
        if (error)
        {
            return Fail(errorMessage, "Cannot create the trash directory: " + error.message());
        }

        // Reserve a unique name by creating its .trashinfo exclusively, as the
        // specification requires, so concurrent trashing never collides.
        const auto stem = absolute.filename().stem().u8string();
        const auto extension = absolute.filename().extension().u8string();
        std::filesystem::path infoPath;
        std::filesystem::path trashedPath;
        std::FILE *infoFile = nullptr;
        for (int index = 1; index < 10000 && !infoFile; ++index)
        {
            auto name = index == 1 ? absolute.filename().u8string() : stem + u8"." + std::u8string(reinterpret_cast<const char8_t *>(std::to_string(index).c_str())) + extension;
            trashedPath = files / std::filesystem::path(name);
            infoPath = info / std::filesystem::path(name + u8".trashinfo");
            if (std::filesystem::exists(std::filesystem::symlink_status(trashedPath, error)))
            {
                continue;
            }
#ifdef _WIN32
            infoFile = _wfopen(infoPath.c_str(), L"wx");
#else
            infoFile = std::fopen(infoPath.c_str(), "wx");
#endif
        }
        if (!infoFile)
        {
            return Fail(errorMessage, "Cannot reserve a name in the trash.");
        }

        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char deletionDate[32];
        std::strftime(deletionDate, sizeof(deletionDate), "%Y-%m-%dT%H:%M:%S", &local);
        const auto originalPath = absolute.generic_u8string();
        const std::string contents = "[Trash Info]\nPath=" +
                                     PercentEncodePath(std::string_view(reinterpret_cast<const char *>(originalPath.data()), originalPath.size())) +
                                     "\nDeletionDate=" + deletionDate + "\n";
        const bool written = std::fwrite(contents.data(), 1, contents.size(), infoFile) == contents.size();
        const bool closed = std::fclose(infoFile) == 0;
        if (!written || !closed)
        {
            std::filesystem::remove(infoPath, error);
            return Fail(errorMessage, "Cannot write trash information.");
        }

        std::filesystem::rename(absolute, trashedPath, error);
        if (error)
        {
            std::error_code cleanup;
            std::filesystem::remove(infoPath, cleanup);
            return Fail(errorMessage, error == std::errc::cross_device_link
                                          ? "The item is on a different drive from the trash."
                                          : "Cannot move the item to the trash: " + error.message());
        }
        return true;
    }

    bool MoveToTrash(const std::filesystem::path &path, std::string *errorMessage)
    {
        std::error_code error;
        if (!std::filesystem::exists(std::filesystem::symlink_status(path, error)))
        {
            return Fail(errorMessage, "Path does not exist: " + Utf8(path));
        }
#ifdef _WIN32
        // SHFileOperation needs a double-null-terminated list.
        std::wstring from = std::filesystem::absolute(path, error).wstring();
        from.push_back(L'\0');
        SHFILEOPSTRUCTW operation{};
        operation.wFunc = FO_DELETE;
        operation.pFrom = from.c_str();
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        return (SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted) ||
               Fail(errorMessage, "Windows could not move " + Utf8(path) + " to the Recycle Bin.");
#elif defined(__APPLE__)
        // AppleScript string literals escape backslashes and quotes.
        std::string quoted;
        for (const char character : Utf8(std::filesystem::absolute(path, error)))
        {
            if (character == '\\' || character == '"') quoted += '\\';
            quoted += character;
        }
        return Spawn({"osascript", "-e", "tell application \"Finder\" to delete POSIX file \"" + quoted + "\""}, true, errorMessage);
#else
        std::filesystem::path dataHome;
        if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg == '/') dataHome = xdg;
        else if (const char *home = std::getenv("HOME"); home && *home) dataHome = std::filesystem::path(home) / ".local" / "share";
        std::string trashError;
        if (!dataHome.empty() && MoveToFreedesktopTrash(path, dataHome / "Trash", &trashError))
        {
            return true;
        }
        // Items on other drives belong in that drive's own trash, which GIO
        // knows how to find; fall back to it before giving up.
        if (Spawn({"gio", "trash", "--", Utf8(path)}, true, nullptr))
        {
            return true;
        }
        return Fail(errorMessage, "Could not move " + Utf8(path) + " to the trash" + (trashError.empty() ? "." : ": " + trashError));
#endif
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
