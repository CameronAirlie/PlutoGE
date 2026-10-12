#pragma once

#include <deque>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace PlutoGE
{
    // Owns a separate console screen. Redirected output keeps ordinary line logs.
    class ServerConsolePanel : public std::streambuf
    {
    public:
        ~ServerConsolePanel() { Stop(); }
        bool Active() const { return active_; }
        void Start()
        {
#ifdef _WIN32
            DWORD mode;
            original_ = GetStdHandle(STD_OUTPUT_HANDLE);
            if (active_ || !GetConsoleMode(original_, &mode) ||
                !GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) ||
                !GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode)) return;
            screen_ = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
            if (screen_ == INVALID_HANDLE_VALUE) return;
            if (!SetConsoleActiveScreenBuffer(screen_)) { CloseHandle(screen_); screen_ = INVALID_HANDLE_VALUE; return; }
            active_ = true;
            cout_ = std::cout.rdbuf(this); cerr_ = std::cerr.rdbuf(this); clog_ = std::clog.rdbuf(this);
#endif
        }
        void Stop()
        {
#ifdef _WIN32
            if (!active_) return;
            std::cout.rdbuf(cout_); std::cerr.rdbuf(cerr_); std::clog.rdbuf(clog_);
            SetConsoleActiveScreenBuffer(original_);
            CloseHandle(screen_); screen_ = INVALID_HANDLE_VALUE; active_ = false;
            // Preserve diagnostics when returning to the caller's terminal.
            for (const auto &line : logs_) std::clog << line << '\n';
            if (!partial_.empty()) std::clog << partial_ << '\n';
#endif
        }
        void Draw(const std::string &input)
        {
#ifdef _WIN32
            if (!active_) return;
            std::lock_guard lock(mutex_);
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (!GetConsoleScreenBufferInfo(screen_, &info)) return;
            const int width = info.srWindow.Right - info.srWindow.Left + 1;
            const int height = info.srWindow.Bottom - info.srWindow.Top + 1;
            if (width < 16 || height < 6) return;
            if (!dirty_ && input == lastInput_ && width == width_ && height == height_) return;
            lastInput_ = input; width_ = width; height_ = height; dirty_ = false;
            constexpr WORD normal = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            constexpr WORD cyan = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            constexpr WORD dim = FOREGROUND_INTENSITY;
            std::vector<CHAR_INFO> cells(static_cast<size_t>(width) * height);
            for (auto &cell : cells) { cell.Char.UnicodeChar = L' '; cell.Attributes = normal; }
            const auto put = [&](int row, int column, const std::string &text, WORD color)
            {
                for (size_t i = 0; i < text.size() && column + static_cast<int>(i) < width; ++i)
                {
                    auto &cell = cells[row * width + column + i];
                    const auto ch = static_cast<unsigned char>(text[i]);
                    cell.Char.UnicodeChar = ch >= 32 ? ch : ' '; cell.Attributes = color;
                }
            };
            put(0, 0, " PLUTOGE / DEDICATED SERVER", cyan);
            put(1, 0, std::string(width, '-'), dim);
            // Wrap log lines and retain only the visible tail.
            std::deque<std::string> visible;
            const auto wrap = [&](const std::string &line)
            {
                for (size_t offset = 0; offset < (line.empty() ? size_t{1} : line.size()); offset += width)
                {
                    visible.push_back(line.substr(offset, width));
                    if (visible.size() > static_cast<size_t>(height - 5)) visible.pop_front();
                }
            };
            for (const auto &line : logs_) wrap(line);
            if (!partial_.empty()) wrap(partial_);
            int row = 2;
            for (const auto &line : visible)
            {
                const auto color = line.find("[Script 2]") != std::string::npos ? WORD(FOREGROUND_RED | FOREGROUND_INTENSITY) : normal;
                put(row++, 0, line, color);
            }
            put(height - 3, 0, "+" + std::string(width - 2, '-') + "+", cyan);
            put(height - 3, 2, " COMMAND ", cyan);
            put(height - 2, 0, "| > ", cyan);
            const auto capacity = static_cast<size_t>(width - 6);
            const auto tail = input.substr(input.size() > capacity ? input.size() - capacity : 0);
            put(height - 2, 4, tail, normal);
            put(height - 2, width - 1, "|", cyan);
            put(height - 1, 0, "+" + std::string(width - 2, '-') + "+", cyan);
            put(height - 1, 2, " Enter: run | Up/Down: history | help ", dim);
            auto region = info.srWindow;
            WriteConsoleOutputW(screen_, cells.data(), {static_cast<SHORT>(width), static_cast<SHORT>(height)}, {0, 0}, &region);
            SetConsoleCursorPosition(screen_, {static_cast<SHORT>(info.srWindow.Left + 4 + tail.size()),
                static_cast<SHORT>(info.srWindow.Top + height - 2)});
#endif
        }
    protected:
        std::streamsize xsputn(const char *text, std::streamsize count) override
        {
            std::lock_guard lock(mutex_);
            for (std::streamsize i = 0; i < count; ++i)
            {
                if (text[i] == '\n')
                {
                    logs_.push_back(std::move(partial_)); partial_.clear();
                    if (logs_.size() > 500) logs_.pop_front();
                }
                else if (text[i] != '\r' && partial_.size() < 4096) partial_ += text[i];
            }
            dirty_ = true;
            return count;
        }
        int_type overflow(int_type ch) override
        {
            if (!traits_type::eq_int_type(ch, traits_type::eof()))
            { const char byte = traits_type::to_char_type(ch); xsputn(&byte, 1); }
            return traits_type::not_eof(ch);
        }
        int sync() override { return 0; }
    private:
        bool active_ = false, dirty_ = true;
        int width_ = 0, height_ = 0;
        std::mutex mutex_;
        std::deque<std::string> logs_;
        std::string partial_, lastInput_;
        std::streambuf *cout_ = nullptr, *cerr_ = nullptr, *clog_ = nullptr;
#ifdef _WIN32
        HANDLE original_ = INVALID_HANDLE_VALUE, screen_ = INVALID_HANDLE_VALUE;
#endif
    };
}
