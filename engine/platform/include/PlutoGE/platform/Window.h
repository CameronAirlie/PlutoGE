#pragma once

#include <string>
#include <filesystem>
#include <functional>
#include <array>
#include <memory>
#include <vector>
#include "PlutoGE/platform/CustomWindowFrame.h"
#include "PlutoGE/platform/InputState.h"

struct GLFWwindow;

namespace PlutoGE::platform
{
    enum class WindowClientApi
    {
        OpenGL,
        None,
    };

    struct WindowConfig
    {
        std::string title = "PlutoGE Window";
        int width = 800;
        int height = 600;
        // Minimum client size in screen coordinates; 0 leaves that axis unconstrained.
        int minWidth = 0;
        int minHeight = 0;
        bool resizable = true;
        bool visible = true;
        bool fullscreen = false;
        // The application draws the title bar through GetCustomFrame(). Ignored
        // where CustomWindowFrame is unsupported, leaving the system caption.
        bool customTitleBar = false;
        WindowClientApi clientApi = WindowClientApi::OpenGL;
        std::function<void(int, int)> resizeCallback = nullptr;
    };

    struct WindowExtents
    {
        int width;
        int height;
    };

    class Window
    {
    public:
        bool Create(const WindowConfig &config);
        void PollEvents();
        bool SetGamepadVibration(int gamepad, float low, float high, float seconds);
        void StopGamepadVibration();
        void Close();
        void RequestClose();
        void SetFullscreen(bool fullscreen);
        // Logical window coordinates, not framebuffer pixels. Borderless retains its restore size.
        [[nodiscard]] WindowExtents GetWindowedSize() const;
        [[nodiscard]] WindowExtents GetWindowedSizeLimit() const;
        bool SetWindowedSize(int width, int height);
        [[nodiscard]] bool IsFullscreen() const { return m_config.fullscreen; }
        void SetTitle(const std::string &title);

        [[nodiscard]] bool IsOpen() const;
        [[nodiscard]] bool ShouldClose() const;
        [[nodiscard]] WindowExtents GetExtents() const;
        [[nodiscard]] void *GetWindow() const;
        [[nodiscard]] WindowClientApi GetClientApi() const { return m_config.clientApi; }
        // Null unless WindowConfig::customTitleBar was requested and attached.
        [[nodiscard]] CustomWindowFrame *GetCustomFrame() const { return m_customFrame.get(); }
        [[nodiscard]] InputState &GetInputState() { return m_inputState; }
        [[nodiscard]] bool IsCursorLocked() const;
        [[nodiscard]] bool IsCursorLockRequested() const { return m_requestedScriptCursorLocked; }
        [[nodiscard]] bool IsCursorLockOverridden() const { return m_forceCursorVisible; }
        [[nodiscard]] bool IsScriptInputEnabled() const { return m_isScriptInputEnabled; }
        // Files dropped onto the window from the OS during the last PollEvents.
        // Valid until the next poll; consumers check where the cursor is.
        [[nodiscard]] const std::vector<std::filesystem::path> &GetDroppedPaths() const { return m_droppedPaths; }
        void ClearDroppedPaths() { m_droppedPaths.clear(); }
        // System clipboard as UTF-8; empty when unavailable.
        [[nodiscard]] std::string GetClipboardText() const;
        void SetClipboardText(const std::string &text) const;
        // Delivers OS file drops on another native window (such as a detached
        // editor viewport) through this window's GetDroppedPaths.
        void ForwardDropsFrom(GLFWwindow *window);
        void StopForwardingDropsFrom(GLFWwindow *window);

        void SetResizeCallback(const std::function<void(int, int)> &callback);
        void SetCursorLocked(bool locked);
        void SetEditorCursorLocked(bool locked);
        void SetCursorLockOverride(bool forceVisible);
        void SetScriptInputEnabled(bool enabled);

        bool EnsureOpenGLContextCurrent(bool reloadFunctions = false);

        std::function<void(int, int)> GetResizeCallback() const
        {
            return m_config.resizeCallback;
        }

    private:
        friend void GLFWFramebufferResizeCallback(GLFWwindow *window, int width, int height);
        friend void GLFWDropCallback(GLFWwindow *window, int count, const char **paths);

        std::array<int, MaxGamepads> m_gamepadJoysticks = [] { std::array<int, MaxGamepads> ids{}; ids.fill(-1); return ids; }();
        std::array<double, 16> m_rumbleUntil{};
        WindowConfig m_config;
        InputState m_inputState;
        std::vector<std::filesystem::path> m_droppedPaths;
        GLFWwindow *m_window = nullptr;
        std::unique_ptr<CustomWindowFrame> m_customFrame;
        int m_clientWidth = 0;
        int m_clientHeight = 0;
        bool m_resizePending = false;
        int m_windowedX = 0, m_windowedY = 0, m_windowedWidth = 800, m_windowedHeight = 600;
        bool m_windowedMaximized = false;
        bool m_isCursorLocked = false;
        bool m_isScriptInputEnabled = true;
        bool m_requestedScriptCursorLocked = false;
        bool m_requestedEditorCursorLocked = false;
        bool m_forceCursorVisible = false;

        void ApplyCursorMode();
    };
}
