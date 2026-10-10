#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/RmlLoadingDocument.h"
#include <RmlUi/Core.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <GLFW/glfw3.h>

namespace { void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); } }
int main() try
{
    using namespace PlutoGE;
    auto &editor = ui::EditorShell::GetInstance();
    Check(editor.Initialize({}, false), "Hidden editor initialization failed");
    struct Scope { ui::EditorShell &editor; ~Scope() { editor.Shutdown(); } } scope{editor};
    Check(editor.GetProject() == nullptr && editor.GetScene() == nullptr, "Startup created a projectless editing scene");
    Check(!editor.LoadProjectFromPath("out/nonexistent-startup-project.plutoproject"), "Invalid startup project was accepted");
    Check(editor.GetProject() == nullptr && editor.GetScene() == nullptr, "Failed project load entered workspace");
    auto &startupWindow = core::Engine::GetInstance().GetWindow();
    startupWindow.RequestClose();
    editor.Render(); // Closing the launcher must return without initializing workspace panels.
    Check(editor.GetProject() == nullptr && editor.GetScene() == nullptr, "Closing launcher created an editing scene");
    glfwSetWindowShouldClose(static_cast<GLFWwindow *>(startupWindow.GetWindow()), GLFW_FALSE);
    const auto root = std::filesystem::current_path() / "out" / "authoring-integration" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto &engine = core::Engine::GetInstance();
    for (const auto &entry : editor.GetAuthoringRegistry().List(ui::AuthoringRegistry::Kind::ProjectTemplate))
    {
        const auto projectPath = root / entry.id / "AuthoringTest.plutoproject";
        Check(editor.CreateProjectAtPath(projectPath, entry.id), "Template creation or script build failed");
        Check(std::filesystem::exists(projectPath), "Template project was not saved");
        if (entry.id == "pluto.empty") continue;
        auto *camera = editor.GetScene()->FindEntityByName("Camera");
        Check(camera && camera->GetComponent<scene::CameraComponent>()->IsMainCamera(), "Template has no main camera");
        if (entry.id == "pluto.third-person" || entry.id == "pluto.fps")
        {
            auto *player = editor.GetScene()->FindEntityByName("Player");
            Check(player && camera->GetParent() == player, "Player camera is not parented to controller");
            Check(player->HasTag("Player") && player->HasTag("Persistent"), "Template player tags missing");
        }
        engine.StartRuntime();
        editor.GetScene()->Update(1.0f / 60);
        engine.StopRuntime();
        for (const auto &message : editor.GetConsoleMessages())
            if (message.severity == ui::EditorShell::ConsoleSeverity::Error) throw std::runtime_error(message.text);
        if (auto *menu = editor.GetScene()->FindEntityByName("Project menu"))
        {
            auto *canvas = menu->GetComponent<scene::CanvasComponent>();
            Check(canvas && canvas->GetScaleMode() == scene::CanvasScaleMode::ConstantPixels, "Template UI uses reference-resolution scaling");
            auto document = render::RmlUiRuntime::Get().CreateLoadingDocument(
                (editor.GetProject()->GetAssetDirectoryPath() / "UI/starter.rml").generic_string(), engine.GetWindow(), *engine.GetRenderDevice());
            Check(document && document->GetDocument()->GetElementById("interface-scale"), "Packaged template UI cannot load");
            if (entry.id == "pluto.application")
            {
                auto &runtime = render::RmlUiRuntime::Get();
                Check(runtime.Initialize(engine.GetWindow(), engine.GetRenderDevice()), "Cannot initialize template screen context");
                auto *context = runtime.GetContext();
                auto *screen = context->LoadDocument((editor.GetProject()->GetAssetDirectoryPath() / "UI/starter.rml").generic_string());
                Check(screen != nullptr, "Cannot load template into screen context");
                screen->Show();
                for (const auto size : {Rml::Vector2i{390, 844}, Rml::Vector2i{800, 400}, Rml::Vector2i{1920, 1080}})
                    for (float scale : {0.75f, 1.0f, 1.25f, 3.0f})
                    {
                        context->SetDimensions(size); runtime.SetInterfaceScale(scale); context->Update();
                        const auto coverage = screen->GetElementById("screen")->GetBox().GetSize(Rml::BoxArea::Border);
                        Check(std::abs(coverage.x - size.x) < 1 && std::abs(coverage.y - size.y) < 1, "Template root lost viewport coverage");
                        auto *panel = screen->GetElementById("menu");
                        const auto panelSize = panel->GetBox().GetSize(Rml::BoxArea::Border);
                        Check(panelSize.x <= size.x && panelSize.y <= size.y, "Template menu exceeds viewport");
                        Check(screen->GetElementById("interface-scale")->GetBox().GetSize().x > 40, "Template slider collapsed");
                        panel->SetScrollTop(panel->GetScrollHeight()); context->Update();
                        auto *quit = screen->GetElementById("quit");
                        Check(quit->GetAbsoluteOffset(Rml::BoxArea::Border).y + quit->GetBox().GetSize(Rml::BoxArea::Border).y <= size.y + 1,
                            "Template footer is unreachable at large interface scale");
                        panel->SetScrollTop(0);
                    }
                screen->Close(); runtime.SetInterfaceScale(1);
            }
        }
        editor.ClearConsoleMessages();
        std::cout << entry.id << " passed\n";
    }
    // Extension failures must restore the scene instead of leaving partial edits.
    auto *camera = editor.GetScene()->FindEntityByName("Camera");
    const auto id = camera->GetID();
    const auto before = camera->GetPosition();
    try { editor.ExecuteSceneEdit("Failing extension", [&] { camera->SetPosition({99, 99, 99}); throw std::runtime_error("expected"); }); }
    catch (const std::runtime_error &) {}
    Check(glm::length(editor.GetScene()->FindEntityByID(id)->GetPosition() - before) < 0.0001f, "Failed extension left partial scene edits");
    const auto tools = editor.GetProject()->GetAssetDirectoryPath() / "Scripts/EditorTools.cs";
    std::ofstream(tools) << R"(using System.Numerics;
using PlutoGE.ScriptCore.Authoring;
public sealed class FailingCommand : EditorCommand {
    protected override void Execute() { GameObject.Position = new Vector3(99); throw new System.InvalidOperationException("expected editor failure"); }
})";
    Check(editor.BuildProjectScripts(), "Cannot build project editor command");
    auto &scripts = engine.GetScriptEngine();
    const auto *commandType = scripts.FindClass("FailingCommand");
    Check(commandType && std::find(commandType->assignableTypeNames.begin(), commandType->assignableTypeNames.end(),
        "PlutoGE.ScriptCore.Authoring.EditorCommand") != commandType->assignableTypeNames.end(), "Managed command was not discovered");
    const auto behaviours = scripts.GetClassNames();
    Check(std::find(behaviours.begin(), behaviours.end(), "FailingCommand") == behaviours.end(), "Editor command exposed as gameplay behaviour");
    bool threw = false;
    try
    {
        editor.ExecuteSceneEdit("Managed failing extension", [&]
        {
            auto command = scripts.CreateInstance("FailingCommand");
            Check(command != nullptr, "Cannot create command");
            command->SetOwner(editor.GetScene()->FindEntityByID(id));
            command->OnCreate();
        });
    }
    catch (const std::runtime_error &) { threw = true; }
    Check(threw && glm::length(editor.GetScene()->FindEntityByID(id)->GetPosition() - before) < 0.0001f,
        "Managed command errors did not propagate/roll back");
    editor.ClearConsoleMessages();
    const auto pump = [&](std::chrono::milliseconds duration)
    {
        const auto end = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < end) { editor.UpdateAuthoring(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    };
    pump(std::chrono::milliseconds(1600)); // Establish the initial read-only source snapshot.
    const auto autoSource = editor.GetProject()->GetAssetDirectoryPath() / "Scripts/AutoProbe.cs";
    std::ofstream(autoSource) << "public sealed class AutoProbe : PlutoGE.ScriptCore.ScriptBehaviour {}";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!scripts.HasClass("AutoProbe") && std::chrono::steady_clock::now() < deadline) pump(std::chrono::milliseconds(50));
    if (!scripts.HasClass("AutoProbe"))
    {
        for (const auto &message : editor.GetConsoleMessages()) std::cerr << message.text << '\n';
        throw std::runtime_error("Automatic source build did not reload new class");
    }
    std::ofstream(autoSource) << "this is invalid C#";
    const auto failures = [&]
    {
        int count = 0;
        for (const auto &message : editor.GetConsoleMessages()) if (message.text.find("Build FAILED.") != std::string::npos) ++count;
        return count;
    };
    const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while ((failures() == 0 || editor.IsScriptBuildRunning()) && std::chrono::steady_clock::now() < failureDeadline) pump(std::chrono::milliseconds(50));
    Check(failures() == 1 && scripts.HasClass("AutoProbe"), "Failed automatic build did not retain loaded assembly");
    pump(std::chrono::milliseconds(2200));
    Check(failures() == 1 && !editor.IsScriptBuildRunning(), "Compiler failure triggered a rebuild loop");
    std::cout << "Authoring integration tests passed.\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
