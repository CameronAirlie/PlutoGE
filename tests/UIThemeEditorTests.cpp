#include "PlutoGE/scene/ui/UIThemeFormat.h"
#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/panels/UIThemeEditorPanel.h"
#include "PlutoGE/ui/theme/UIThemeDocument.h"
#include "PlutoGE/ui/theme/UIThemeViews.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Drives the theme editor in a hidden OpenGL window: document behaviour
// (undo grouping, saving, edits routed to open documents) and every view.
// With an output prefix argument, writes a .ppm of each view for visual QA.
namespace
{
    using namespace PlutoGE;

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    // Keeps the theme in memory, standing in for an asset file.
    class MemoryStorage final : public ui::UIThemeStorage
    {
    public:
        explicit MemoryStorage(std::string &text) : m_text(text) {}
        std::string GetLabel() const override { return "Memory.plutotheme"; }
        bool SavesSeparately() const override { return true; }
        bool Read(scene::UITheme &theme, std::vector<std::string> &problems, std::string &) override
        {
            theme = scene::ParseUITheme(m_text, &problems);
            return true;
        }
        void Preview(const scene::UITheme &, bool unsaved) override { previewing = unsaved; }
        bool Save(const scene::UITheme &theme, std::string &) override
        {
            m_text = scene::WriteUITheme(theme);
            return true;
        }
        bool PollExternalChange(const scene::UITheme &, scene::UITheme &, std::vector<std::string> &) override { return false; }
        bool previewing = false;

    private:
        std::string &m_text;
    };

    void TestDocument()
    {
        std::string stored = scene::WriteUITheme(scene::MakeStarterUITheme());
        const auto target = ui::UIThemeTarget::ForAsset("project://Memory.plutotheme");
        std::string error;
        auto document = ui::UIThemeDocument::Open(target, std::make_unique<MemoryStorage>(stored), error);
        Require(document && document->GetLoadErrors().empty(), "The starter theme must open cleanly");
        Require(ui::FindOpenUIThemeDocument(target) == document.get(), "Open documents are found by target");

        // Two edits while a control is held are one undo step.
        document->Modify([](scene::UITheme &theme) { theme.FindVariable("accent")->value = "1,0,0,1"; });
        document->Modify([](scene::UITheme &theme) { theme.FindVariable("accent")->value = "0,1,0,1"; });
        Require(document->IsDirty() && document->CanUndo(), "Edits mark the document unsaved");
        document->Undo();
        Require(document->GetTheme().FindVariable("accent")->value == "0.27,0.52,0.96,1", "One undo reverts the whole group");
        document->Redo();
        Require(document->GetTheme().FindVariable("accent")->value == "0,1,0,1", "Redo restores the group");

        // Edits from elsewhere (the inspector) go through the open document.
        Require(ui::EditUITheme(target, [](scene::UITheme &theme) { theme.AddStyle("Card", "Panel"); }, error), "EditUITheme must succeed");
        Require(document->GetTheme().FindStyle("Card"), "EditUITheme edits the open document");
        Require(document->GetResolved().FindStyle("Card")->chain.size() == 2, "Resolution follows edits");

        Require(document->Save(error) && !document->IsDirty(), "Save clears the unsaved state");
        Require(scene::ParseUITheme(stored) == document->GetTheme(), "Save writes the theme to its storage");
        document.reset();
        Require(!ui::FindOpenUIThemeDocument(target), "Closed documents are forgotten");
    }

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
            handle = glfwCreateWindow(kWidth, kHeight, "Theme editor test", nullptr, nullptr);
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
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
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

    void TestViews(const std::string &capturePrefix)
    {
        Window window;
        if (!window.Create())
        {
            std::cout << "UIThemeEditorTests: no OpenGL context, skipping views\n";
            return;
        }
        std::string stored = scene::WriteUITheme(scene::MakeStarterUITheme());
        std::string error;
        auto document = ui::UIThemeDocument::Open(ui::UIThemeTarget::ForAsset("project://Memory.plutotheme"),
                                                   std::make_unique<MemoryStorage>(stored), error);
        auto *raw = document.get();
        ui::UIThemeEditorPanel panel({"UI Theme Editor", true});
        panel.Open(std::move(document));
        raw->Modify([](scene::UITheme &theme) { theme.FindVariable("accent")->value = "0.9,0.4,0.2,1"; });

        const auto capture = [&](const char *name)
        {
            if (!capturePrefix.empty())
                window.Capture(capturePrefix + name + ".ppm");
        };
        // A few frames settle layout (tables, child sizes) before capturing.
        for (int frame = 0; frame < 4; ++frame)
            window.Frame([&] { panel.Update(); });
        capture("panel");

        ui::theme::StylesViewState styles;
        styles.selected = "Primary-Button";
        for (const auto state : {scene::UIStyleState::Normal, scene::UIStyleState::Hovered})
        {
            styles.state = state;
            for (int frame = 0; frame < 3; ++frame)
                window.Frame([&]
                             {
                                 ImGui::Begin("Styles");
                                 ui::theme::DrawStylesView(*raw, styles);
                                 ImGui::End();
                             });
            capture(state == scene::UIStyleState::Normal ? "styles" : "styles-hover");
        }

        ui::theme::VariablesViewState variables;
        for (int frame = 0; frame < 3; ++frame)
            window.Frame([&]
                         {
                             ImGui::Begin("Variables");
                             ui::theme::DrawVariablesView(*raw, variables);
                             ImGui::End();
                         });
        capture("variables");

        ui::theme::SourceViewState source;
        for (int frame = 0; frame < 3; ++frame)
            window.Frame([&]
                         {
                             ImGui::Begin("Source");
                             ui::theme::DrawSourceView(*raw, source);
                             ImGui::End();
                         });
        capture("source");
        Require(source.text.find(".Primary-Button extends .Button") != std::string::npos, "The source view shows the theme text");
        Require(raw->IsDirty(), "Views keep the document's unsaved state");
    }
}

int main(int argc, char **argv)
{
    try
    {
        TestDocument();
        TestViews(argc > 1 ? argv[1] : "");
    }
    catch (const std::exception &error)
    {
        std::cerr << "UIThemeEditorTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "UIThemeEditorTests passed\n";
    return 0;
}
