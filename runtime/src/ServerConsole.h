#pragma once
#include "ServerConsolePanel.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
// windows.h must not define min/max macros for includers (they break std::numeric_limits<T>::max() in engine headers).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <conio.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace PlutoGE
{
    // Poll on the simulation thread: an unfinished line must never stall a tick.
    class ServerConsole
    {
    public:
        bool Interactive() const
        {
#ifdef _WIN32
            DWORD mode = 0;
            return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
#else
            return isatty(STDIN_FILENO) != 0;
#endif
        }

        void StartPanel() { panel_.Start(); panel_.Draw(pending_); }
        void Prompt() { if (panel_.Active()) panel_.Draw(pending_); else if (Interactive()) std::cout << "server> " << std::flush; }

        static std::string Normalize(std::string command)
        {
            const auto first = command.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            command = command.substr(first, command.find_last_not_of(" \t\r\n") - first + 1);
            std::transform(command.begin(), command.end(), command.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return command;
        }

        std::vector<std::string> Poll()
        {
            std::vector<std::string> lines;
            // Bound work even if stdin is fed continuously by another process.
            for (unsigned i = 0; i < 4096 && !closed_; ++i)
            {
                int ch = -1;
#ifdef _WIN32
                if (Interactive())
                {
                    if (!_kbhit()) break;
                    ch = _getch();
                    if (extended_)
                    {
                        extended_ = false;
                        if (ch == 72 && historyIndex_ > 0)
                        {
                            if (historyIndex_ == history_.size()) draft_ = pending_;
                            pending_ = history_[--historyIndex_];
                        }
                        else if (ch == 80 && historyIndex_ < history_.size())
                        { ++historyIndex_; pending_ = historyIndex_ == history_.size() ? draft_ : history_[historyIndex_]; }
                        continue;
                    }
                    if (ch == 0 || ch == 224) { extended_ = true; continue; }
                    if (ch == 3) { lines.emplace_back("quit"); break; }
                    if (ch == 8)
                    {
                        if (!pending_.empty()) { pending_.pop_back(); if (!panel_.Active()) std::cout << "\b \b" << std::flush; }
                        continue;
                    }
                    if (ch == '\r') { ch = '\n'; if (!panel_.Active()) std::cout << '\n'; }
                    else if (ch < 32 || ch >= 127) continue;
                    else if (!panel_.Active() && pending_.size() < 1024) std::cout << static_cast<char>(ch) << std::flush;
                }
                else
                {
                    const auto input = GetStdHandle(STD_INPUT_HANDLE);
                    const auto type = GetFileType(input);
                    if (type == FILE_TYPE_PIPE)
                    {
                        DWORD available = 0;
                        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) { closed_ = true; break; }
                        if (!available) break;
                    }
                    else if (type != FILE_TYPE_DISK) { closed_ = true; break; }
                    char byte = 0; DWORD count = 0;
                    if (!ReadFile(input, &byte, 1, &count, nullptr) || !count) { closed_ = true; break; }
                    ch = static_cast<unsigned char>(byte);
                }
#else
                pollfd input{STDIN_FILENO, POLLIN, 0};
                if (poll(&input, 1, 0) <= 0) break;
                if (!(input.revents & (POLLIN | POLLHUP))) { closed_ = true; break; }
                char byte = 0;
                if (read(STDIN_FILENO, &byte, 1) <= 0) { closed_ = true; break; }
                ch = static_cast<unsigned char>(byte);
#endif
                if (ch == '\n')
                {
                    lines.push_back(overflow_ ? "<command too long>" : Normalize(pending_));
                    if (panel_.Active() && !pending_.empty())
                    {
                        std::cout << "> " << pending_ << '\n';
                        if (history_.empty() || history_.back() != pending_) history_.push_back(pending_);
                        if (history_.size() > 100) history_.erase(history_.begin());
                        historyIndex_ = history_.size(); draft_.clear();
                    }
                    pending_.clear(); overflow_ = false;
                }
                else if (ch != '\r')
                {
                    if (pending_.size() < 1024) pending_ += static_cast<char>(ch);
                    else overflow_ = true;
                }
            }
            if (closed_ && (!pending_.empty() || overflow_))
            {
                lines.push_back(overflow_ ? "<command too long>" : Normalize(pending_));
                pending_.clear(); overflow_ = false;
            }
            panel_.Draw(pending_);
            return lines;
        }

    private:
        std::string pending_;
        ServerConsolePanel panel_;
        std::vector<std::string> history_;
        size_t historyIndex_ = 0;
        std::string draft_;
        bool closed_ = false, extended_ = false, overflow_ = false;
    };
}
