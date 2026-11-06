#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/FoliageDiagnostics.h"
#include "PlutoGE/ui/FoliageTypeSession.h"
#include "PlutoGE/ui/FoliageTypeUsage.h"
#include "PlutoGE/ui/panels/FoliageEditorPanel.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Drives the Foliage Editor: the session (undo, saving, live preview), the asset storage against a real scene and file,
// finding the types that use an asset, and the panel in a hidden OpenGL window. With an output prefix argument, writes
// a .ppm of each view for visual QA.
namespace
{
    using namespace PlutoGE;

    constexpr int kWidth = 1100;
    constexpr int kHeight = 720;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

    class MemoryStorage final : public ui::FoliageTypeStorage
    {
    public:
        explicit MemoryStorage(scene::FoliageTypeAsset &asset) : m_asset(asset) {}
        std::string GetLabel() const override { return "Oak.plutofoliage"; }
        bool Read(scene::FoliageTypeAsset &asset, std::string &error) override
        {
            if (failReads)
            {
                error = "Damaged";
                return false;
            }
            asset = m_asset;
            return true;
        }
        bool Write(const scene::FoliageTypeAsset &asset, std::string &error) override
        {
            if (failWrites)
            {
                error = "Read only";
                return false;
            }
            m_asset = asset;
            ++writes;
            return true;
        }
        void Preview(const scene::FoliageTypeAsset *asset) override
        {
            previewing = asset != nullptr;
            ++previews;
            if (asset)
                previewed = *asset;
        }
        bool failReads = false;
        bool failWrites = false;
        int writes = 0;
        int previews = 0;
        bool previewing = false;
        scene::FoliageTypeAsset previewed;

    private:
        scene::FoliageTypeAsset &m_asset;
    };

    void TestSession()
    {
        scene::FoliageTypeAsset stored;
        stored.cellSize = 40.0f;
        std::string error;

        {
            auto damaged = std::make_unique<MemoryStorage>(stored);
            damaged->failReads = true;
            Require(!ui::FoliageTypeSession::Open(std::move(damaged), error) && error == "Damaged", "A damaged asset does not open, and says why");
            error.clear();
        }

        auto storage = std::make_unique<MemoryStorage>(stored);
        auto *memory = storage.get();
        auto session = ui::FoliageTypeSession::Open(std::move(storage), error);
        Require(session && !session->IsDirty() && !session->IsPreviewing() && !memory->previewing, "An asset opens without unsaved changes");

        // A drag: several edits while the control is held make one undo step.
        session->Modify([](auto &asset) { asset.cellSize = 50.0f; });
        session->EndFrame(true);
        session->Modify([](auto &asset) { asset.cellSize = 60.0f; });
        session->EndFrame(false);
        Require(session->IsDirty() && memory->previewing && Near(memory->previewed.cellSize, 60.0f), "Edits mark the session unsaved and preview live");
        session->Undo();
        Require(!session->IsDirty() && !memory->previewing && Near(session->GetAsset().cellSize, 40.0f), "One undo reverts the drag and ends the preview");
        session->Redo();
        Require(session->IsDirty() && memory->previewing, "Redo restores the drag and the preview");

        // Edits are kept valid, and an edit that changes nothing is not a step.
        const int previewsBefore = memory->previews;
        session->Modify([](auto &) {});
        Require(memory->previews == previewsBefore, "An edit changing nothing does nothing");
        session->Modify([](auto &asset) { asset.collisionRadius = -5.0f; });
        session->EndFrame(false);
        Require(session->GetAsset().collisionRadius > 0.0f, "Edits are sanitized");
        session->Undo();

        session->SetLivePreview(false);
        Require(!memory->previewing && !session->IsPreviewing() && session->IsDirty(), "Turning Live off shows the saved asset");
        session->SetLivePreview(true);
        Require(memory->previewing, "Turning Live on shows the edits again");

        session->SetPreviewSuspended(true);
        Require(!memory->previewing && session->IsLivePreview(), "Suspending ends the preview and keeps the setting");
        session->Modify([](auto &asset) { asset.cellSize = 70.0f; });
        session->EndFrame(false);
        Require(!memory->previewing, "Edits made while suspended do not preview");
        session->SetPreviewSuspended(false);
        Require(memory->previewing && Near(memory->previewed.cellSize, 70.0f), "Resuming shows the current edits");

        const int previewsBeforeReapply = memory->previews;
        session->ReapplyPreview();
        Require(memory->previews == previewsBeforeReapply + 1 && memory->previewing, "Reapplying sends the preview again");

        memory->failWrites = true;
        Require(!session->Save(error) && error == "Read only" && session->IsDirty() && memory->previewing, "A failed save keeps the edits and the preview");
        memory->failWrites = false;
        error.clear();
        Require(session->Save(error) && !session->IsDirty() && memory->writes == 1 && !memory->previewing && Near(stored.cellSize, 70.0f),
                "Save writes the asset and ends the preview");
        const int previewsAfterSave = memory->previews;
        session->ReapplyPreview();
        Require(memory->previews == previewsAfterSave, "Nothing to reapply once saved");

        session->Modify([](auto &asset) { asset.cellSize = 80.0f; });
        session->EndFrame(false);
        session->Revert();
        Require(!session->IsDirty() && !memory->previewing && Near(session->GetAsset().cellSize, 70.0f), "Revert restores the saved asset");
        session->Undo();
        Require(Near(session->GetAsset().cellSize, 80.0f), "Revert can be undone");

        Require(memory->previewing, "Undoing a revert previews again");
        session.reset();
        Require(!memory->previewing, "Closing without saving ends the preview");
    }

    struct SceneFixture
    {
        scene::Scene scene;
        scene::Entity *forest = nullptr;
        scene::FoliageComponent *foliage = nullptr;
        std::filesystem::path directory = std::filesystem::temp_directory_path() / "PlutoGEFoliageEditorTests";
        std::filesystem::path file = directory / "Oak.plutofoliage";
        std::string reference = file.string();

        SceneFixture()
        {
            std::filesystem::remove_all(directory);
            std::filesystem::create_directories(directory);
            forest = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Forest"}));
            foliage = forest->CreateComponent<scene::FoliageComponent>();
        }
        ~SceneFixture() { std::filesystem::remove_all(directory); }

        std::unique_ptr<ui::FoliageTypeStorage> Storage()
        {
            return ui::MakeFoliageTypeAssetStorage(reference, [this] { return &scene; });
        }
    };

    // The asset, its file and the types linked to it together: what the panel does through the session.
    void TestEditorDrivesLinkedTypes()
    {
        SceneFixture fixture;
        auto &foliage = *fixture.foliage;
        scene::FoliageTypeAsset initial;
        initial.cellSize = 48.0f;
        std::string error;
        Require(scene::SaveFoliageTypeAsset(fixture.reference, initial, &error), error);

        foliage.AddType("Oak");
        const std::size_t linked = foliage.GetTypeCount() - 1;
        foliage.AddType("Grass");
        const std::size_t other = foliage.GetTypeCount() - 1;
        Require(foliage.SetTypeAssetReference(linked, fixture.reference, &error), "Linking: " + error);

        const auto users = ui::FindFoliageTypeUsers(fixture.scene, fixture.reference);
        Require(users.size() == 1 && users[0].entity == fixture.forest->GetID() && users[0].entityName == "Forest" &&
                    users[0].typeIndex == linked && users[0].typeName == "Oak" && users[0].instanceCount == 0,
                "Only the linked type uses the asset");
        Require(ui::FindFoliageTypeUsers(fixture.scene, "").empty(), "An empty reference matches the unlinked types of nothing");
        Require(ui::FindFoliageTypeUsers(fixture.scene, "project://Other.plutofoliage").empty(), "Another asset has no users");
        Require(ui::FindFoliageTypeLinkedTo(fixture.scene, fixture.reference) == foliage.GetType(linked), "The linked type can be found");

        auto session = ui::FoliageTypeSession::Open(fixture.Storage(), error);
        Require(session && Near(session->GetAsset().cellSize, 48.0f), "The session reads the file: " + error);
        Require(session->GetAsset().assetReference.empty(), "The draft holds no link, which is not stored in the file");

        session->Modify([](auto &asset) { asset.cellSize = 64.0f; });
        session->EndFrame(false);
        Require(Near(foliage.GetType(linked)->asset.cellSize, 64.0f), "The linked type previews the edit");
        Require(foliage.GetType(linked)->asset.assetReference == fixture.reference, "A preview keeps the link");
        Require(Near(foliage.GetType(other)->asset.cellSize, 32.0f), "An unlinked type does not preview");
        scene::FoliageTypeAsset onDisk;
        Require(scene::LoadFoliageTypeAsset(fixture.reference, onDisk) && Near(onDisk.cellSize, 48.0f), "A preview leaves the file alone");

        // A scene that reloads the type, as undo or reopening it does, loses the preview until it is sent again.
        Require(fixture.scene.RefreshFoliageTypeAsset(fixture.reference) == 1 && Near(foliage.GetType(linked)->asset.cellSize, 48.0f), "Reloading goes to the file");
        session->ReapplyPreview();
        Require(Near(foliage.GetType(linked)->asset.cellSize, 64.0f), "Reapplying restores the preview");

        session->SetLivePreview(false);
        Require(Near(foliage.GetType(linked)->asset.cellSize, 48.0f), "Turning Live off goes back to the file");
        session->SetLivePreview(true);
        Require(Near(foliage.GetType(linked)->asset.cellSize, 64.0f), "Turning Live on previews again");

        Require(session->Save(error), "Saving: " + error);
        Require(scene::LoadFoliageTypeAsset(fixture.reference, onDisk) && Near(onDisk.cellSize, 64.0f), "Save writes the file");
        Require(Near(foliage.GetType(linked)->asset.cellSize, 64.0f) && !session->IsPreviewing(), "The linked type keeps the saved value");

        // Saving with Live off still reaches linked types.
        session->SetLivePreview(false);
        session->Modify([](auto &asset) { asset.cellSize = 72.0f; });
        session->EndFrame(false);
        Require(Near(foliage.GetType(linked)->asset.cellSize, 64.0f), "With Live off an edit stays in the editor");
        Require(session->Save(error) && Near(foliage.GetType(linked)->asset.cellSize, 72.0f), "Save updates linked types with Live off");

        session->SetLivePreview(true);
        session->Modify([](auto &asset) { asset.cellSize = 90.0f; });
        session->EndFrame(false);
        session.reset();
        Require(Near(foliage.GetType(linked)->asset.cellSize, 72.0f), "Closing without saving puts the type back");

        // No scene: the storage still reads and writes the file.
        std::string noSceneError;
        auto detached = ui::FoliageTypeSession::Open(ui::MakeFoliageTypeAssetStorage(fixture.reference, [] { return static_cast<scene::Scene *>(nullptr); }), noSceneError);
        Require(detached != nullptr, "Opening without a scene: " + noSceneError);
        detached->Modify([](auto &asset) { asset.cellSize = 24.0f; });
        detached->EndFrame(false);
        Require(detached->Save(noSceneError) && scene::LoadFoliageTypeAsset(fixture.reference, onDisk) && Near(onDisk.cellSize, 24.0f), "Saving without a scene");

        // A damaged file never opens, so it cannot be overwritten with defaults.
        std::ofstream(fixture.file, std::ios::trunc) << "not a foliage type";
        std::string damagedError;
        Require(!ui::FoliageTypeSession::Open(fixture.Storage(), damagedError) && !damagedError.empty(), "A damaged file reports why");
        std::ifstream check(fixture.file);
        std::string content;
        std::getline(check, content);
        Require(content == "not a foliage type", "A damaged file is left as it was");
    }

    // A version 1 file has no mesh. The scene's type has one, and saving must not take it away.
    void TestOldFileKeepsMesh()
    {
        SceneFixture fixture;
        auto &foliage = *fixture.foliage;
        foliage.AddType("Oak");
        const std::size_t index = foliage.GetTypeCount() - 1;
        auto scenes = foliage.GetType(index)->asset;
        scenes.materialAssetReference = "project://Materials/Bark.plutomaterial";
        scenes.minScale = 2.0f;
        scenes.maxScale = 3.0f;
        foliage.ApplyTypeAsset(index, scenes);

        std::ofstream(fixture.file) << "PLUTOFOLIAGE\t1\nCELL_SIZE\t48\n";
        std::string error;
        Require(foliage.SetTypeAssetReference(index, fixture.reference, &error), "Linking an old file: " + error);
        Require(Near(foliage.GetType(index)->asset.minScale, 2.0f), "Linking an old file keeps the scene's placement");

        auto session = ui::FoliageTypeSession::Open(fixture.Storage(), error);
        Require(session != nullptr, "Opening an old file: " + error);
        Require(session->GetAsset().materialAssetReference == scenes.materialAssetReference && Near(session->GetAsset().minScale, 2.0f),
                "The draft takes what an old file lacks from the type using it");
        Require(Near(session->GetAsset().cellSize, 48.0f), "The draft takes what an old file has from the file");
        session->Modify([](auto &asset) { asset.cellSize = 56.0f; });
        session->EndFrame(false);
        Require(session->Save(error), "Upgrading an old file: " + error);

        scene::FoliageTypeAsset upgraded;
        Require(scene::LoadFoliageTypeAsset(fixture.reference, upgraded), "Reading the upgraded file");
        Require(upgraded.materialAssetReference == scenes.materialAssetReference && Near(upgraded.minScale, 2.0f) && Near(upgraded.cellSize, 56.0f),
                "Saving an old file keeps the prototype and placement");
    }

    void TestDiagnostics()
    {
        scene::FoliageTypeAsset asset;
        auto issues = ui::DiagnoseFoliageAsset(asset, nullptr, false, true);
        Require(issues.size() == 1 && issues[0].severity == ui::FoliageIssueSeverity::Warning, "An asset without a mesh is a warning");
        asset.sourceMeshPath = "project://Missing.plutomesh";
        issues = ui::DiagnoseFoliageAsset(asset, nullptr, false, true);
        Require(issues.size() == 1 && issues[0].severity == ui::FoliageIssueSeverity::Error, "A mesh that did not load is an error");
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
            handle = glfwCreateWindow(kWidth, kHeight, "Foliage editor test", nullptr, nullptr);
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

        void Frame(const std::function<void()> &draw, int width = kWidth, int height = kHeight)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::GetIO().DisplaySize = ImVec2(kWidth, kHeight);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
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
            std::cout << "FoliageEditorTests: no OpenGL context, skipping views\n";
            return;
        }
        const auto capture = [&](const char *name)
        {
            if (!capturePrefix.empty())
                window.Capture(capturePrefix + name + ".ppm");
        };
        // A few frames settle layout (child sizes) before capturing.
        const auto frames = [&](const std::function<void()> &draw, int width = kWidth)
        {
            for (int frame = 0; frame < 4; ++frame)
                window.Frame(draw, width);
        };

        scene::FoliageTypeAsset stored;
        std::string error;
        auto session = ui::FoliageTypeSession::Open(std::make_unique<MemoryStorage>(stored), error);
        auto *raw = session.get();

        ui::FoliageEditorPanel panel({"Foliage Editor", true});
        frames([&] { panel.Update(); });
        capture("empty");

        panel.Open(std::move(session));
        frames([&] { panel.Update(); });
        capture("wide");
        Require(!panel.HasUnsavedChanges(), "A freshly opened asset has no unsaved changes");

        raw->Modify([](auto &asset) { asset.cellSize = 48.0f; });
        raw->EndFrame(false);
        frames([&] { panel.Update(); });
        capture("unsaved");
        Require(panel.HasUnsavedChanges(), "The panel reports unsaved changes");
        Require(raw->IsPreviewing(), "The panel leaves the preview on while the game is not running");

        // Too narrow for two columns: everything shares one scrolling column.
        frames([&] { panel.Update(); }, 420);
        capture("narrow");

        frames([&]
               {
                   ImGui::Begin("Used");
                   ui::FoliageUsageViewOptions options;
                   options.addTargetName = "Meadow";
                   static_cast<void>(ui::DrawFoliageUsageView({{1, "Forest", 0, "Oak", 1204}, {2, "Ridge Line", 3, "Old Oak", 56}}, options));
                   ImGui::Separator();
                   options.addTargetName.clear();
                   options.editable = false;
                   static_cast<void>(ui::DrawFoliageUsageView({}, options));
                   ImGui::End();
               }, 300);
        capture("usage");

        // Real clicks on the toolbar. The positions are checked against the captures.
        frames([&] { panel.Update(); });
        const auto draw = [&] { panel.Update(); };
        const auto click = [&](ImVec2 at)
        {
            auto &io = ImGui::GetIO();
            io.AddMousePosEvent(at.x, at.y);
            window.Frame(draw);
            io.AddMouseButtonEvent(0, true);
            window.Frame(draw);
            io.AddMouseButtonEvent(0, false);
            window.Frame(draw);
            window.Frame(draw);
        };
        const ImVec2 liveSwitch(826.0f, 45.0f), undo(912.0f, 45.0f), redo(945.0f, 45.0f), save(978.0f, 45.0f), revert(1011.0f, 45.0f), close(1077.0f, 45.0f);

        click(liveSwitch);
        Require(!raw->IsLivePreview() && !raw->IsPreviewing(), "The Live switch turns the preview off");
        click(liveSwitch);
        Require(raw->IsLivePreview() && raw->IsPreviewing(), "The Live switch turns it back on");
        click(undo);
        Require(!raw->IsDirty() && !panel.HasUnsavedChanges(), "The undo button undoes the edit");
        click(redo);
        Require(raw->IsDirty() && Near(raw->GetAsset().cellSize, 48.0f), "The redo button redoes it");
        click(revert);
        Require(!raw->IsDirty(), "The revert button discards the edit");
        click(undo);
        Require(raw->IsDirty(), "Undo brings back what revert discarded");
        click(save);
        Require(!raw->IsDirty() && Near(stored.cellSize, 48.0f), "The save button writes the asset");

        // Closing with unsaved changes asks first, and Cancel keeps everything.
        raw->Modify([](auto &asset) { asset.cellSize = 56.0f; });
        raw->EndFrame(false);
        click(close);
        for (int frame = 0; frame < 4; ++frame)
            window.Frame(draw);
        capture("close-prompt");
        Require(panel.HasUnsavedChanges(), "Closing with unsaved changes waits for an answer");
        window.Frame([&]
                     {
                         ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
                         draw();
                     });
        window.Frame([&]
                     {
                         ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
                         draw();
                     });
        for (int frame = 0; frame < 4; ++frame)
            window.Frame(draw);
        Require(panel.HasUnsavedChanges() && Near(raw->GetAsset().cellSize, 56.0f), "Cancelling the prompt keeps the session and its edits");

        // Closing with nothing unsaved happens on the next frame, with the rest of this frame still drawing the session.
        raw->Revert();
        raw->EndFrame(false);
        click(close);
        for (int frame = 0; frame < 2; ++frame)
            window.Frame(draw);
        capture("closed");
        Require(!panel.HasUnsavedChanges(), "The close button closes the asset");

        frames(draw, 300);
        panel.Open(ui::FoliageTypeSession::Open(std::make_unique<MemoryStorage>(stored), error));
        frames(draw, 300);
        capture("very-narrow");

        panel.Open(nullptr);
        frames([&] { panel.Update(); });
        Require(!panel.HasUnsavedChanges(), "Replacing the session drops its changes");
    }
}

int main(int argc, char **argv)
{
    try
    {
        TestSession();
        TestEditorDrivesLinkedTypes();
        TestOldFileKeepsMesh();
        TestDiagnostics();
        TestViews(argc > 1 ? argv[1] : "");
    }
    catch (const std::exception &error)
    {
        std::cerr << "FoliageEditorTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "FoliageEditorTests passed\n";
    return 0;
}
