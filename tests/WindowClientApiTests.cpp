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

    window.Close();
    return 0;
}
