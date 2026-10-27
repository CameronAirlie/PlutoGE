#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/input/InputBindingCapture.h"
#include "PlutoGE/ui/input/InputMappingSession.h"
#include "PlutoGE/ui/input/InputMappingViews.h"
#include "PlutoGE/ui/panels/InputMappingEditorPanel.h"

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

// Drives the input mapping editor: session behaviour (undo grouping, saving,
// unreadable files), press-to-bind capture, the analysis behind the reports,
// and every view in a hidden OpenGL window. With an output prefix argument,
// writes a .ppm of each view for visual QA.
namespace
{
    using namespace PlutoGE;

    constexpr int kWidth = 1100;
    constexpr int kHeight = 720;

    const char *kMapping = R"({
  "Actions": [
    { "Name": "Jump", "Bindings": [
        { "Kind": "Key", "Key": "Space", "Scale": 1, "DeadZone": 0 },
        { "Kind": "GamepadButton", "Button": "A", "Scale": 1, "DeadZone": 0 } ] },
    { "Name": "MoveX", "Bindings": [
        { "Kind": "Key", "Key": "D", "Scale": 1, "DeadZone": 0 },
        { "Kind": "Key", "Key": "A", "Scale": -1, "DeadZone": 0 },
        { "Kind": "GamepadAxis", "Axis": "LeftX", "Scale": 1, "DeadZone": 0.15 } ] },
    { "Name": "Roll", "Bindings": [
        { "Kind": "GamepadButton", "Button": "B", "Scale": 1, "DeadZone": 0, "Trigger": { "Kind": "MultiTap", "Count": 2, "Window": 0.25 } } ] },
    { "Name": "Crouch", "Bindings": [
        { "Kind": "GamepadButton", "Button": "B", "Scale": 1, "DeadZone": 0, "Trigger": { "Kind": "Hold", "Duration": 0.3 } },
        { "Kind": "Key", "Key": "LeftControl", "Scale": 1, "DeadZone": 0 } ] },
    { "Name": "MenuAccept", "Bindings": [ { "Kind": "GamepadButton", "Button": "A", "Scale": 1, "DeadZone": 0 } ] },
    { "Name": "Fire", "Bindings": [ { "Kind": "MouseButton", "MouseButton": "Left", "Scale": 1, "DeadZone": 0 } ] }
  ]
})";

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    // Keeps the mapping in memory, standing in for an asset file.
    class MemoryStorage final : public ui::InputMappingStorage
    {
    public:
        explicit MemoryStorage(std::string &text) : m_text(text) {}
        std::string GetLabel() const override { return "Player.plutoinput"; }
        bool Read(std::string &text, std::string &) override
        {
            text = m_text;
            return true;
        }
        bool Write(const std::string &text, std::string &) override
        {
            m_text = text;
            ++writes;
            return true;
        }
        int writes = 0;

    private:
        std::string &m_text;
    };

    void TestSession()
    {
        std::string stored = kMapping;
        std::string error;
        auto storage = std::make_unique<MemoryStorage>(stored);
        auto *memory = storage.get();
        auto session = ui::InputMappingSession::Open(std::move(storage), error);
        Require(session && !session->IsDirty(), "A mapping opens without unsaved changes");

        // Changes while a control is held are one undo step.
        session->Modify([](ui::InputMappingDocument &document) { document.Actions()[0].name = "Ju"; });
        session->Modify([](ui::InputMappingDocument &document) { document.Actions()[0].name = "Jumping"; });
        session->EndFrame(false);
        Require(session->IsDirty() && session->CanUndo(), "Edits mark the session unsaved");
        session->Undo();
        Require(session->GetActions()[0].name == "Jump" && !session->IsDirty(), "One undo reverts the group and the unsaved state");
        session->Redo();
        Require(session->GetActions()[0].name == "Jumping", "Redo restores the group");

        // An edit that changes nothing is not an undo step.
        session->EndFrame(false);
        session->Undo();
        session->Modify([](ui::InputMappingDocument &) {});
        Require(!session->CanUndo() && session->CanRedo(), "A no-op edit keeps the redo history");

        session->Redo();
        Require(session->Save(error) && !session->IsDirty() && memory->writes == 1, "Save writes and clears the unsaved state");
        Require(stored.find("\"Jumping\"") != std::string::npos, "Save writes the mapping text");

        session->Modify([](ui::InputMappingDocument &document) { document.Actions().clear(); });
        session->EndFrame(false);
        session->Revert();
        Require(session->GetActions().size() == 6 && !session->IsDirty(), "Revert restores the saved mapping");
        session->Undo();
        Require(session->GetActions().empty(), "Revert can be undone");

        std::string broken = "{ \"Actions\": 3 }";
        Require(!ui::InputMappingSession::Open(std::make_unique<MemoryStorage>(broken), error) && !error.empty(),
                "An unreadable mapping is refused with a reason");
        Require(broken == "{ \"Actions\": 3 }", "A refused mapping is left unchanged");
    }

    void TestCapture()
    {
        platform::InputState input;
        ui::InputBindingCapture capture;
        Require(!capture.Poll(input), "An idle capture hears nothing");

        // Keys: a fresh press is heard, a held key is not.
        capture.Begin(0, 1, input);
        input.keys[71] = input.previousKeys[71] = true; // G held from before
        Require(!capture.Poll(input) && capture.IsListeningTo(0, 1), "A key held before listening is ignored");
        input.keys[75] = true; // K pressed
        auto heard = capture.Poll(input);
        Require(heard && heard->kind == ui::InputBindingKind::Key && heard->value == "K" && !capture.IsActive(), "A key press is heard");
        input = {};

        // Escape cancels; the left mouse button (clicking the editor) is ignored.
        capture.Begin(0, 0, input);
        input.mouseState.buttons[0] = true;
        Require(!capture.Poll(input) && capture.IsActive(), "The left mouse button does not bind");
        input.mouseState.buttons[1] = true;
        heard = capture.Poll(input);
        Require(heard && heard->kind == ui::InputBindingKind::MouseButton && heard->value == "Right", "Other mouse buttons bind");
        input = {};
        capture.Begin(0, 0, input);
        input.keys[256] = true;
        Require(!capture.Poll(input) && !capture.IsActive(), "Escape cancels");
        input = {};

        // Axes count movement from where they rested: a trigger resting at -1 is not a press.
        input.gamepads[1].connected = true;
        input.gamepads[1].axes[4] = -1.0f;
        capture.Begin(2, 0, input);
        Require(!capture.Poll(input), "A resting trigger is ignored");
        input.gamepads[1].axes[0] = -0.8f;
        heard = capture.Poll(input);
        Require(heard && heard->kind == ui::InputBindingKind::GamepadAxis && heard->value == "LeftX" && heard->gamepad == 1 &&
                    heard->direction < 0.0f, "Stick movement binds the axis, its pad and direction");

        ui::InputMappingBinding binding;
        binding.scale = 2.0f;
        binding.trigger.kind = ui::InputTriggerKind::Hold;
        heard->ApplyTo(binding);
        Require(binding.kind == ui::InputBindingKind::GamepadAxis && binding.scale == -2.0f && binding.trigger.kind == ui::InputTriggerKind::Hold,
                "Applying a capture keeps the trigger and the scale's size");
    }

    void TestAnalysis()
    {
        std::string stored = kMapping;
        std::string error;
        auto session = ui::InputMappingSession::Open(std::make_unique<MemoryStorage>(stored), error);
        ui::input::InputMappingAnalysis analysis;
        analysis.Update(*session);
        // Pad A: Jump and MenuAccept both press (a conflict). Pad B: double tap vs hold (told apart).
        Require(analysis.sharedInputs.size() == 2 && analysis.conflictCount == 1, "Shared inputs and conflicts are found");
        Require(analysis.actionProblems[0] == 1 && analysis.actionProblems[2] == 0, "Conflicts are counted on their actions");
        const auto *shared = analysis.FindShared(session->GetActions()[4].bindings[0]);
        Require(shared && !shared->disambiguatedByTriggers && shared->label.find("A") != std::string::npos, "Bindings find their shared input");

        session->Modify([](ui::InputMappingDocument &document) { document.Actions()[5].name = "Jump"; });
        analysis.Update(*session);
        Require(analysis.IsNameInvalid("Jump") && analysis.ProblemCount() == 2, "Duplicate names are problems, refreshed on change");
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
            handle = glfwCreateWindow(kWidth, kHeight, "Input mapping editor test", nullptr, nullptr);
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
            std::cout << "InputMappingEditorTests: no OpenGL context, skipping views\n";
            return;
        }
        std::string stored = kMapping;
        std::string error;
        auto session = ui::InputMappingSession::Open(std::make_unique<MemoryStorage>(stored), error);
        auto *raw = session.get();
        const auto capture = [&](const char *name)
        {
            if (!capturePrefix.empty())
                window.Capture(capturePrefix + name + ".ppm");
        };
        const auto frames = [&](const std::function<void()> &draw)
        {
            // A few frames settle layout (tables, child sizes) before capturing.
            for (int frame = 0; frame < 4; ++frame)
                window.Frame(draw);
        };

        ui::InputMappingEditorPanel panel({"Input Mapping Editor", true});
        panel.Open(std::move(session));
        raw->Modify([](ui::InputMappingDocument &document) { document.Actions()[0].bindings[0].value = "W"; });
        raw->EndFrame(false);
        frames([&] { panel.Update(); });
        capture("panel");
        Require(panel.HasUnsavedChanges(), "The panel reports unsaved changes");

        platform::InputState input;
        input.gamepads[0].connected = true;
        input.gamepads[0].axes = {0.42f, -0.1f, 0.0f, 0.0f, -1.0f, 0.6f};
        input.gamepads[0].buttons[0] = input.gamepads[0].previousButtons[0] = true; // A held from before
        ui::input::InputMappingAnalysis analysis;
        ui::input::ActionsViewState actions;
        const auto drawActions = [&]
        {
            analysis.Update(*raw);
            ImGui::Begin("Actions");
            ui::input::DrawActionsView(*raw, actions, analysis, &input);
            ImGui::End();
        };
        actions.selected = 1;
        actions.expandedBinding = 2;
        frames(drawActions);
        capture("actions-axis");

        actions.selected = 2;
        actions.expandedBinding = 0;
        frames(drawActions);
        capture("actions-trigger");

        // Listening shows its prompt, then a gamepad press binds.
        actions.selected = 0;
        actions.expandedBinding = -1;
        actions.capture.Begin(0, 1, input);
        frames(drawActions);
        capture("actions-listening");
        input.gamepads[0].buttons[3] = true;
        frames(drawActions);
        Require(raw->GetActions()[0].bindings[1].value == "Y" && !actions.capture.IsActive(), "Listening binds the pressed button");
        input.gamepads[0].previousButtons[3] = true;

        frames([&]
               {
                   analysis.Update(*raw);
                   ImGui::Begin("Overview");
                   (void)ui::input::DrawOverviewView(*raw, analysis);
                   ImGui::End();
               });
        capture("overview");

        frames([&]
               {
                   ImGui::Begin("Devices");
                   ui::input::DrawDevicesView(&input);
                   ImGui::End();
               });
        capture("devices");
    }
}

int main(int argc, char **argv)
{
    try
    {
        TestSession();
        TestCapture();
        TestAnalysis();
        TestViews(argc > 1 ? argv[1] : "");
    }
    catch (const std::exception &error)
    {
        std::cerr << "InputMappingEditorTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "InputMappingEditorTests passed\n";
    return 0;
}
