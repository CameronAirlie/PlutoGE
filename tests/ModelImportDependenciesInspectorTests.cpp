#include "PlutoGE/ui/ModelImportDependenciesInspector.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace PlutoGE;
namespace
{
    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-input-inspector-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch()
        {
            if (root.parent_path() == std::filesystem::temp_directory_path() && root.filename().string().starts_with("PlutoGE-input-inspector-"))
            { std::error_code error; std::filesystem::remove_all(root, error); }
        }
    };
    void Write(const std::filesystem::path &path, const std::string &text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary); out << text; Require(bool(out), "Fixture write failed");
    }
    void Complete(ui::ModelImportDependenciesInspector &view)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!view.Poll())
        { Require(std::chrono::steady_clock::now() < end, "Verification timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
}
int main()
try
{
    Scratch scratch;
    assets::ProjectManifest manifest; manifest.assetPipelineVersion = 3;
    assets::Project project(scratch.root / "Test.plutoproject", manifest);
    assetimport::ImportState state; state.ownerId = "dependency-source"; state.sourceReference = "project://Model.gltf";
    Write(scratch.root / "Assets/Model.gltf", "source");
    Write(scratch.root / "Assets/Model.gltf.plutometa", "metadata");
    Write(scratch.root / "External/buffer.bin", "buffer");
    for (auto input : {assetimport::ArtifactInput{"source", scratch.root / "Assets/Model.gltf"},
        assetimport::ArtifactInput{"source-metadata", scratch.root / "Assets/Model.gltf.plutometa"},
        assetimport::ArtifactInput{"external-buffer", scratch.root / "External/buffer.bin"}})
    { Require(content::HashFileContent(input.path, input.digest), "Hash fixture"); state.inputs.push_back(std::move(input)); }
    std::string error;
    Require(assetimport::ImportStateStore(scratch.root).Store(state, &error), error);
    ui::ModelImportDependenciesInspector view;
    Require(view.Refresh(project, state.sourceReference, state.ownerId), view.Error());
    Require(view.Inputs().size() == 3 && view.Inputs()[0].status == ui::ImportInputStatus::Unchecked, "Load must not imply freshness");
    Require(view.Verify(), "Start verification"); Complete(view);
    for (const auto &row : view.Inputs()) Require(row.status == ui::ImportInputStatus::Current, "Unchanged input");
    Write(scratch.root / "Assets/Model.gltf", "edited"); // Same length: content, not size/time, is authoritative.
    std::filesystem::remove(scratch.root / "External/buffer.bin");
    Require(view.Verify(), "Recheck verification"); Complete(view);
    Require(view.Inputs()[0].status == ui::ImportInputStatus::Changed, "Detect same-size edit");
    Require(view.Inputs()[2].status == ui::ImportInputStatus::Unavailable, "Detect missing external input");
    Require(view.Verify(), "Start old-source check");
    Require(!view.Refresh(project, "project://Other.gltf", state.ownerId), "Reject source mismatch");
    Complete(view); Require(view.Inputs().empty(), "Discard stale background result");
    Require(!view.Refresh(project, state.sourceReference, "missing-owner"), "Missing state is diagnostic");
    Require(view.Inputs().empty() && !view.Error().empty(), "Missing state clears stale rows");
    Require(view.Refresh(project, state.sourceReference, state.ownerId), view.Error());
    Require(view.Verify(), "Final check"); Complete(view);
    ImGui::CreateContext(); auto &io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = {800, 800}; io.DeltaTime = 1.0f / 60.0f;
    unsigned char *pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame(); ImGui::Begin("Inputs");
    ImGui::GetStateStorage()->SetInt(ImGui::GetID("Import Dependencies"), 1);
    ImGui::LogToBuffer(); view.Render(project, state.sourceReference, state.ownerId, false);
    const std::string log = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish(); ImGui::End(); ImGui::Render(); ImGui::DestroyContext();
    Require(log.find("Changed - reimport needed") != std::string::npos && log.find("Missing or unreadable") != std::string::npos, "UI exposes actionable content statuses");
    manifest.assetPipelineVersion = 2; assets::Project legacy(scratch.root / "Legacy.plutoproject", manifest);
    Require(!view.Refresh(legacy, state.sourceReference, state.ownerId) && view.Inputs().empty(), "Legacy gate");
    Require(!std::filesystem::exists(scratch.root / "Assets/Model.gltf.plutometa.plutometa"), "Inspection must not create metadata");
    std::cout << "Import dependency inspection passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
