#include "PlutoGE/ui/ModelHierarchyInspector.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/AssetCatalog.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;
namespace
{
    void Require(bool value, const std::string &message)
    { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-hierarchy-inspector-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch()
        {
            if (root.parent_path() == std::filesystem::temp_directory_path() && root.filename().string().starts_with("PlutoGE-hierarchy-inspector-"))
            { std::error_code error; std::filesystem::remove_all(root, error); }
        }
    };
    void Write(const std::filesystem::path &path, const std::string &bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << bytes;
        Require(static_cast<bool>(output), "Cannot write inspector fixture");
    }
    std::string Frame(ui::ModelHierarchyInspector &inspector, const assets::Project &project,
        const std::string &reference, const assets::ModelAsset &package,
        std::shared_ptr<const assets::AssetCatalog> catalog, bool importing = false,
        const scene::Scene *scene = nullptr,
        const std::function<bool(const assetimport::ModelNodeRepairProposal &, std::string *)> &applyRepair = {})
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({460, 850});
        ImGui::Begin("Hierarchy inspector test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::LogToBuffer();
                if (scene)
        {
            const auto sourceId = ImHashStr("SourceHierarchy", 0, ImGui::GetCurrentWindow()->ID);
            ImGui::GetStateStorage()->SetInt(ImHashStr("Repair Correspondence", 0, sourceId), 1);
        }
        inspector.Render(project, reference, package, std::move(catalog), importing, scene, applyRepair);
        const std::string log = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::End();
        ImGui::Render();
        return log;
    }
    void Contains(const std::string &log, const char *text)
    { Require(log.find(text) != std::string::npos, std::string("Missing inspector text: ") + text + "\n" + log); }
}

int main()
try
{
    Scratch scratch;
    assets::ProjectManifest manifest;
    manifest.assetPipelineVersion = 3;
    assets::Project project(scratch.root / "Test.plutoproject", manifest);
    assets::ModelHierarchyAsset snapshot;
    snapshot.sourceAssetId = "inspector-source";
    Require(assets::SerializeAssetReference({snapshot.sourceAssetId, 42}, snapshot.meshReference), "Cannot encode mesh");
    snapshot.hierarchy.nodes = {{"Root", -1}, {"Mesh", 0}, {"", 0}};
    snapshot.hierarchy.sceneRoots = {0};
    snapshot.hierarchy.bindings = {{1, 0, glm::mat4(1)}};
    assets::ModelImportSettings settings;
    std::string error;
    Require(assets::ReconcileModelNodeIdentities(snapshot.hierarchy, settings, snapshot.identities, &error), error);
    settings.objects.push_back({"mesh/Mesh", 42, false});
    assets::ModelAsset package;
    package.sourceReference = "project://Model.fbx";
    package.sourceAssetId = snapshot.sourceAssetId;
    package.objects.push_back({42, assets::ProjectAssetType::Mesh, "Mesh", "project://Model/Mesh.plutomesh"});
    std::string bytes;
    Require(assets::SerializeModelHierarchyAsset(snapshot, bytes, &error), error);
    const auto digest = content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size())));
    const std::string hierarchyReference = "project://Model/Model.plutomodelhierarchy";
    Require(assets::WriteModelHierarchyArtifact(package, {hierarchyReference, digest}, &error), error);
    assets::AssetMetadata metadata;
    metadata.id = snapshot.sourceAssetId;
    Require(assets::WriteModelImportSettings(metadata, settings, &error) &&
        assets::WriteModelSourcePackage(metadata, package, &error) && assets::WriteModelArtifactGeneration(metadata, digest, &error), error);
    std::string sidecar;
    Require(assets::SerializeAssetMetadata(metadata, sidecar, &error), error);
    Write(scratch.root / "Assets/Model.fbx", "Read-only inspector fixture");
    Write(assets::GetAssetMetadataPath(scratch.root / "Assets/Model.fbx"), sidecar);
    const auto path = scratch.root / "Library/Artifacts" / content::DigestToHex(digest) / "Files/Model/Model.plutomodelhierarchy";
    Write(path, bytes);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = {460, 850};
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    {
        ui::ModelHierarchyInspector inspector;
        auto catalog = std::make_shared<const assets::AssetCatalog>();
        auto log = Frame(inspector, project, package.sourceReference, package, catalog);
        Contains(log, "Selected scene: 3 nodes, 1 bindings");
        Contains(log, "1 selected node needs identity repair");
        Contains(log, "(unnamed)  [unresolved]");
        Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Inspector produced no draw data");
        // Exercise an actual node-row click, then preserve selection by source
        // identity when the accepted catalog snapshot changes.
        ImGuiWindow *tree = nullptr;
        for (auto *window : ImGui::GetCurrentContext()->Windows)
            if (std::string_view(window->Name).find("/NodeTree_") != std::string_view::npos) tree = window;
        Require(tree != nullptr, "Hierarchy tree child window is missing");
        const auto point = ImVec2(tree->DC.CursorStartPos.x + 100,
            tree->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetTextLineHeight() * .5f);
        ImGui::GetIO().AddMousePosEvent(point.x, point.y);
        Frame(inspector, project, package.sourceReference, package, catalog);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        Frame(inspector, project, package.sourceReference, package, catalog);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        log = Frame(inspector, project, package.sourceReference, package, catalog);
        Contains(log, "Name: Mesh");
        Contains(log, "Source node ID:");
        Contains(log, "Exact Local Matrix");
        Contains(log, "Submesh 0");
        catalog = std::make_shared<const assets::AssetCatalog>();
        Contains(Frame(inspector, project, package.sourceReference, package, catalog), "Name: Mesh");
        // A steady frame reuses the accepted snapshot; refresh/catalog publication
        // revalidates bytes and clears the old tree if verification fails.
        Write(path, "corrupt hierarchy");
        Contains(Frame(inspector, project, package.sourceReference, package, catalog), "Selected scene: 3 nodes");
        catalog = std::make_shared<const assets::AssetCatalog>();
        log = Frame(inspector, project, package.sourceReference, package, catalog);
        Contains(log, "Hierarchy unavailable:");
        Require(log.find("Selected scene: 3 nodes") == std::string::npos, "Failed refresh retained stale hierarchy");
        Write(path, bytes);
        log = Frame(inspector, project, package.sourceReference, package, catalog, true);
        Contains(log, "Import in progress.");
        Contains(Frame(inspector, project, package.sourceReference, package, catalog), "Selected scene: 3 nodes");
        log = Frame(inspector, project, "project://Missing.fbx", package, catalog);
        Contains(log, "Hierarchy unavailable:");
        Require(log.find("Selected scene: 3 nodes") == std::string::npos, "Changing source retained previous source tree");
        Contains(Frame(inspector, project, package.sourceReference, package, catalog), "Selected scene: 3 nodes");
        project.GetManifest().assetPipelineVersion = 5;
        scene::Scene noRetainedInstances;
        bool repairApplied = false;
        auto applyRepair = [&](const assetimport::ModelNodeRepairProposal &, std::string *) {
            repairApplied = true;
            return true;
        };
        // A capability-5 source exposes repair review for a selected node, but
        // must not invent retired identities when no accepted instance exists.
        ImGui::GetIO().AddMousePosEvent(point.x, point.y);
        Frame(inspector, project, package.sourceReference, package, catalog);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        Frame(inspector, project, package.sourceReference, package, catalog);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        log = Frame(inspector, project, package.sourceReference, package, catalog, false, &noRetainedInstances, applyRepair);
        Contains(log, "Repair Correspondence");
        Contains(log, "No retired nodes are available");
        Require(!repairApplied, "Inspector applied an unreviewed repair");
        project.GetManifest().assetPipelineVersion = 2;
        Contains(Frame(inspector, project, package.sourceReference, package, catalog), "version 3 or later");
    }
    ImGui::DestroyContext();
    std::cout << "Model hierarchy inspector smoke checks passed.\n";
    return 0;
}
catch (const std::exception &error)
{
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    std::cerr << error.what() << '\n';
    return 1;
}
