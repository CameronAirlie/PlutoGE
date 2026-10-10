#include "PlutoGE/scripting/ScriptBuildProcess.h"
#include <array>
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace PlutoGE::scripting
{
    namespace
    {
        void Append(ProcessOutput &result, const char *data, std::size_t count)
        {
            constexpr std::size_t maximum = 4 * 1024 * 1024;
            if (result.text.size() < maximum) result.text.append(data, std::min(count, maximum - result.text.size()));
        }
#ifdef _WIN32
        struct Handle
        {
            HANDLE value = nullptr;
            ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
        };
        std::wstring Wide(const std::string &text)
        {
            if (text.empty()) return {};
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (!size) throw std::runtime_error("Invalid UTF-8 process argument");
            std::wstring result(size, 0);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
            return result;
        }
        std::wstring Quote(const std::wstring &text)
        {
            std::wstring result = L"\"";
            std::size_t slashes = 0;
            for (wchar_t c : text)
            {
                if (c == L'\\') { ++slashes; continue; }
                result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
                slashes = 0;
                if (c == L'"') result += L'\\';
                result += c;
            }
            result.append(slashes * 2, L'\\');
            result += L'"';
            return result;
        }
#endif
    }
    ProcessOutput RunBuildProcess(const std::vector<std::string> &arguments)
    {
        ProcessOutput result;
        if (arguments.empty()) return result;
        for (const auto &argument : arguments) if (argument.find('\0') != std::string::npos) throw std::invalid_argument("Null process argument");
        std::array<char, 4096> buffer{};
#ifdef _WIN32
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle read, write, input;
        if (!CreatePipe(&read.value, &write.value, &attributes, 0) || !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0)) return result;
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr);
        if (input.value == INVALID_HANDLE_VALUE) return result;
        std::wstring command;
        for (const auto &argument : arguments) { if (!command.empty()) command += L' '; command += Quote(Wide(argument)); }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = startup.hStdError = write.value;
        startup.hStdInput = input.value;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        {
            result.text = "Cannot start dotnet (Windows error " + std::to_string(GetLastError()) + ").";
            return result;
        }
        Handle processHandle{process.hProcess}, threadHandle{process.hThread};
        CloseHandle(write.value); write.value = nullptr;
        DWORD count = 0;
        while (ReadFile(read.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count)
            Append(result, buffer.data(), count);
        WaitForSingleObject(processHandle.value, INFINITE);
        DWORD code = 0;
        if (GetExitCodeProcess(processHandle.value, &code)) result.exitCode = static_cast<int>(code);
#else
        int pipeHandles[2];
        if (pipe(pipeHandles) != 0) return result;
        // Prepare argv before fork: the editor has other threads.
        std::vector<char *> argv;
        for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
        argv.push_back(nullptr);
        const pid_t child = fork();
        if (child == 0)
        {
            close(pipeHandles[0]);
            dup2(pipeHandles[1], STDOUT_FILENO); dup2(pipeHandles[1], STDERR_FILENO);
            close(pipeHandles[1]);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        close(pipeHandles[1]);
        if (child < 0) { close(pipeHandles[0]); return result; }
        ssize_t count;
        while (true)
        {
            count = read(pipeHandles[0], buffer.data(), buffer.size());
            if (count > 0) Append(result, buffer.data(), static_cast<std::size_t>(count));
            else if (count < 0 && errno == EINTR) continue;
            else break;
        }
        close(pipeHandles[0]);
        int status = 0;
        while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return result;
        if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
#endif
        return result;
    }
}
