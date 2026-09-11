#include "PlutoGE/platform/Window.h"

#include <GLFW/glfw3.h>

int main()
{
    using namespace PlutoGE::platform;

    Window window;
    if (!window.Create({.title = "PlutoGE no-client-API test",
                        .width = 64,
                        .height = 64,
                        .resizable = false,
                        .visible = false,
                        .clientApi = WindowClientApi::None}))
        return 1;

    if (window.GetClientApi() != WindowClientApi::None)
        return 2;
    if (glfwGetWindowAttrib(static_cast<GLFWwindow *>(window.GetWindow()), GLFW_CLIENT_API) != GLFW_NO_API)
        return 3;
    if (window.EnsureOpenGLContextCurrent())
        return 4;
    if (glfwGetCurrentContext() != nullptr)
        return 5;

    auto *native = static_cast<GLFWwindow *>(window.GetWindow());
    int x, y, width, height;
    glfwGetWindowPos(native, &x, &y);
    glfwGetWindowSize(native, &width, &height);
    window.SetFullscreen(true);
    if (!window.IsFullscreen() || glfwGetWindowMonitor(native) != nullptr) return 6;
    if (glfwGetWindowAttrib(native, GLFW_DECORATED)) return 7;
    window.SetFullscreen(true); // Idempotence must not overwrite the restore rectangle.
    window.SetFullscreen(false);
    int restoredX, restoredY, restoredWidth, restoredHeight;
    glfwGetWindowPos(native, &restoredX, &restoredY);
    glfwGetWindowSize(native, &restoredWidth, &restoredHeight);
    if (window.IsFullscreen() || !glfwGetWindowAttrib(native, GLFW_DECORATED)) return 8;
    if (x != restoredX || y != restoredY || width != restoredWidth || height != restoredHeight) return 9;
    if (glfwGetWindowAttrib(native, GLFW_CLIENT_API) != GLFW_NO_API) return 10;
    window.Close();
    if (!window.Create({.title = "PlutoGE borderless startup test", .width = 640, .height = 480,
                        .visible = false, .fullscreen = true, .clientApi = WindowClientApi::None})) return 11;
    native = static_cast<GLFWwindow *>(window.GetWindow());
    if (!window.IsFullscreen() || glfwGetWindowMonitor(native) || glfwGetWindowAttrib(native, GLFW_DECORATED)) return 12;
    window.SetFullscreen(false);
    glfwGetWindowSize(native, &restoredWidth, &restoredHeight);
    if (restoredWidth != 640 || restoredHeight != 480) return 13;
    window.Close();
    return 0;
}
