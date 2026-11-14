#pragma once
// A hidden OpenGL window running the editor's ImGui setup (fonts, theme) for inspector tests: draw frames with
// Frame(), and Capture() a frame to a .ppm for visual review. Create() fails without an OpenGL context.
#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTheme.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace PlutoGE::ui::testing
{
    constexpr int kWidth = 900;
    constexpr int kHeight = 900;

    struct Window
    {
        GLFWwindow *handle = nullptr;
        ui::fonts::EditorFontLibrary fonts;

        bool Create()
        {
            if (!glfwInit())
                return false;
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            handle = glfwCreateWindow(kWidth, kHeight, "Inspector test", nullptr, nullptr);
            if (!handle)
                return false;
            glfwMakeContextCurrent(handle);
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
                return false;
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            fonts.Load(ImGui::GetIO());
            ui::ApplyActiveEditorTheme();
            ImGui_ImplGlfw_InitForOpenGL(handle, false);
            ImGui_ImplOpenGL3_Init("#version 330");
            return true;
        }

        ~Window()
        {
            if (!handle)
                return;
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            glfwDestroyWindow(handle);
            glfwTerminate();
        }

        void Frame(const std::function<void()> &draw)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::GetIO().DisplaySize = ImVec2(kWidth, kHeight);
            ImGui::NewFrame();
            draw();
            ImGui::Render();
            glViewport(0, 0, kWidth, kHeight);
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        void Capture(const std::string &path) const
        {
            std::vector<unsigned char> pixels(kWidth * kHeight * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, kWidth, kHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << kWidth << " " << kHeight << "\n255\n";
            for (int y = kHeight - 1; y >= 0; --y)
                file.write(reinterpret_cast<const char *>(pixels.data() + y * kWidth * 3), kWidth * 3);
        }
    };
}
