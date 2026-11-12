#include "Win32RawMouseInput.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace PlutoGE::platform
{
    namespace
    {
        // NEXTRAWINPUTBLOCK without its QWORD dependency: reports in a GetRawInputBuffer block are 8-byte aligned.
        const RAWINPUT *NextReport(const RAWINPUT *report)
        {
            const auto next = reinterpret_cast<std::uintptr_t>(report) + report->header.dwSize;
            return reinterpret_cast<const RAWINPUT *>((next + 7) & ~std::uintptr_t{7});
        }

        // Reads raw mouse reports on a thread of its own, through a message-only window registered as the process's
        // raw mouse target, and drains them in batches with GetRawInputBuffer. The game window's queue then carries
        // only the coalesced legacy mouse messages, however fast the mouse reports.
        //
        // Raw input registration is per process: one target window per device class. While this source exists the
        // windowing library must not register its own raw mouse motion, or it would take the reports from here.
        class Win32RawMouseInput final : public RelativeMouseInput
        {
        public:
            ~Win32RawMouseInput() override { Stop(); }

            bool Start()
            {
                m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!m_stop)
                    return false;
                std::promise<bool> started;
                auto ready = started.get_future();
                m_thread = std::thread([this, started = std::move(started)]() mutable { Run(started); });
                if (ready.get())
                    return true;
                Stop();
                return false;
            }

            void SetCapturing(bool capturing) override
            {
                if (m_capturing.exchange(capturing) == capturing)
                    return;
                // Motion from before a capture began, or after it ended, never reaches the next one.
                std::lock_guard lock(m_mutex);
                m_pending = {};
                m_resetAbsolute = true;
            }
            [[nodiscard]] bool IsCapturing() const override { return m_capturing.load(); }

            Motion Take() override
            {
                std::lock_guard lock(m_mutex);
                const auto motion = m_pending;
                m_pending = {};
                return motion;
            }

        private:
            void Stop()
            {
                if (m_thread.joinable())
                {
                    SetEvent(m_stop);
                    m_thread.join();
                }
                if (m_stop)
                {
                    CloseHandle(m_stop);
                    m_stop = nullptr;
                }
            }

            void Run(std::promise<bool> &started)
            {
                const HINSTANCE instance = GetModuleHandleW(nullptr);
                const std::wstring className = L"PlutoGERawMouseInput" + std::to_wstring(reinterpret_cast<std::uintptr_t>(this));
                WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
                windowClass.lpfnWndProc = DefWindowProcW;
                windowClass.hInstance = instance;
                windowClass.lpszClassName = className.c_str();
                if (!RegisterClassExW(&windowClass))
                {
                    started.set_value(false);
                    return;
                }
                const HWND window = CreateWindowExW(0, className.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
                // Generic desktop page, mouse usage. INPUTSINK: the message-only window is never in the foreground, so
                // reports arrive regardless and SetCapturing gates them on the game window's focus instead.
                RAWINPUTDEVICE device{0x01, 0x02, RIDEV_INPUTSINK, window};
                if (!window || !RegisterRawInputDevices(&device, 1, sizeof(device)))
                {
                    if (window)
                        DestroyWindow(window);
                    UnregisterClassW(className.c_str(), instance);
                    started.set_value(false);
                    return;
                }
                started.set_value(true);

                // Room for a few hundred reports per read; a frame's worth at 8 kHz is well under that.
                std::vector<std::uint64_t> buffer(64 * 1024 / sizeof(std::uint64_t));
                for (;;)
                {
                    const DWORD woke = MsgWaitForMultipleObjects(1, &m_stop, FALSE, INFINITE, QS_RAWINPUT);
                    if (woke != WAIT_OBJECT_0 + 1)
                        break;
                    // Clears the queue's new-input state, so the next wait sleeps until more reports arrive.
                    (void)GetQueueStatus(QS_RAWINPUT);
                    Drain(window, buffer);
                }

                device.dwFlags = RIDEV_REMOVE;
                device.hwndTarget = nullptr;
                RegisterRawInputDevices(&device, 1, sizeof(device));
                DestroyWindow(window);
                UnregisterClassW(className.c_str(), instance);
            }

            void Drain(HWND window, std::vector<std::uint64_t> &buffer)
            {
                Motion batch;
                for (;;)
                {
                    UINT size = static_cast<UINT>(buffer.size() * sizeof(std::uint64_t));
                    auto *first = reinterpret_cast<RAWINPUT *>(buffer.data());
                    const UINT count = GetRawInputBuffer(first, &size, sizeof(RAWINPUTHEADER));
                    if (count == 0 || count == static_cast<UINT>(-1))
                        break;
                    const RAWINPUT *report = first;
                    for (UINT index = 0; index < count; ++index)
                    {
                        Accumulate(*report, batch);
                        report = NextReport(report);
                    }
                }
                // Reports the buffered read did not take (it can leave some queued) are read one at a time.
                MSG message;
                while (PeekMessageW(&message, window, WM_INPUT, WM_INPUT, PM_REMOVE))
                {
                    RAWINPUT report{};
                    UINT size = sizeof(report);
                    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(message.lParam), RID_INPUT, &report, &size,
                                        sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1))
                        Accumulate(report, batch);
                    DefWindowProcW(window, message.message, message.wParam, message.lParam);
                }
                if (batch.reports == 0 || !m_capturing.load())
                    return;
                std::lock_guard lock(m_mutex);
                m_pending.x += batch.x;
                m_pending.y += batch.y;
                m_pending.reports += batch.reports;
            }

            void Accumulate(const RAWINPUT &report, Motion &batch)
            {
                if (report.header.dwType != RIM_TYPEMOUSE)
                    return;
                const RAWMOUSE &mouse = report.data.mouse;
                if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
                {
                    // Tablets and virtual machines report positions in 0..65535 across the (virtual) screen.
                    const bool virtualDesktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
                    const double width = GetSystemMetrics(virtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
                    const double height = GetSystemMetrics(virtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
                    const double x = mouse.lLastX / 65535.0 * width, y = mouse.lLastY / 65535.0 * height;
                    if (m_resetAbsolute.exchange(false) == false && m_hasAbsolute)
                    {
                        batch.x += x - m_lastAbsoluteX;
                        batch.y += y - m_lastAbsoluteY;
                    }
                    m_lastAbsoluteX = x;
                    m_lastAbsoluteY = y;
                    m_hasAbsolute = true;
                }
                else
                {
                    batch.x += mouse.lLastX;
                    batch.y += mouse.lLastY;
                }
                ++batch.reports;
            }

            HANDLE m_stop = nullptr;
            std::thread m_thread;
            std::atomic<bool> m_capturing{false};
            std::atomic<bool> m_resetAbsolute{true};
            std::mutex m_mutex;
            Motion m_pending;
            // Reader thread only.
            double m_lastAbsoluteX = 0.0, m_lastAbsoluteY = 0.0;
            bool m_hasAbsolute = false;
        };
    }

    std::unique_ptr<RelativeMouseInput> CreateWin32RawMouseInput()
    {
        auto source = std::make_unique<Win32RawMouseInput>();
        if (!source->Start())
            return nullptr;
        return source;
    }
}
