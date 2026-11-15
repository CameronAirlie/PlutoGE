#include "PlutoGE/ui/TerrainHeightEditing.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace PlutoGE::ui
{
    namespace
    {
        scene::TerrainComponent *FindTerrain(std::uint64_t entityId, scene::Entity **owner = nullptr)
        {
            auto *scene = EditorShell::GetInstance().GetScene();
            auto *entity = scene ? scene->FindEntityByID(static_cast<scene::EntityID>(entityId)) : nullptr;
            if (owner)
                *owner = entity;
            return entity ? entity->GetComponent<scene::TerrainComponent>() : nullptr;
        }

        // Everything an import replaces: where the heights live, the heights, and the holes (a new grid size
        // clears them).
        struct HeightState
        {
            std::string heightFile;
            scene::TerrainHeightData heights;
            scene::TerrainHoleMask holes;
            std::size_t Bytes() const { return (heights.field.samples.size() + heights.border.size()) * sizeof(float); }
        };

        HeightState Capture(const scene::TerrainComponent &terrain)
        {
            return {terrain.GetHeightDataPath(), terrain.GetHeightData(), terrain.GetHoles()};
        }

        bool Restore(std::uint64_t entityId, const HeightState &state)
        {
            scene::Entity *owner = nullptr;
            auto *terrain = FindTerrain(entityId, &owner);
            if (!terrain)
                return false;
            terrain->SetHeightDataPath(state.heightFile);
            terrain->SetHeightData(state.heights);
            terrain->SetHoles(state.holes);
            if (!state.heightFile.empty())
                SaveTerrainHeights(EditorShell::GetInstance(), *owner, *terrain);
            return true;
        }

        // Per-entity inspector state for the import options.
        struct ImportForm
        {
            std::array<char, 512> path{};
            int format = 0; // TerrainHeightSourceFormat
            int rawWidth = 0, rawDepth = 0;
            bool bigEndian = false;
            bool flipRows = true;
            glm::vec2 range{0.0f, -1.0f}; // y < x: not yet initialised from the terrain
            std::string lastError;
        };

        ImportForm &FormFor(const scene::Entity &entity)
        {
            static std::unordered_map<scene::EntityID, ImportForm> forms;
            return forms[entity.GetID()];
        }

        void BrowseForHeightMap(ImportForm &form)
        {
#ifdef _WIN32
            OPENFILENAMEA dialog = {};
            char fileName[MAX_PATH] = "";
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = "Heightmaps\0*.png;*.tga;*.bmp;*.jpg;*.jpeg;*.r16;*.raw;*.r32;*.f32;*.plutoheight\0All Files\0*.*\0";
            dialog.lpstrFile = fileName;
            dialog.nMaxFile = MAX_PATH;
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&dialog))
            {
                form.path.fill('\0');
                strncpy_s(form.path.data(), form.path.size(), fileName, _TRUNCATE);
            }
#else
            (void)form;
#endif
        }
    }

    std::string DefaultTerrainHeightFile(EditorShell &shell, const scene::Entity &entity)
    {
        auto *project = shell.GetProject();
        if (!project)
            return {};
        const auto *scene = entity.GetScene();
        const std::string stem = scene && !scene->GetFilePath().empty()
            ? std::filesystem::path(scene->GetFilePath()).stem().string() : std::string("Scene");
        const auto file = project->GetAssetDirectoryPath() / "TerrainHeights" /
                          (stem + "_" + std::to_string(entity.GetID()) + std::string(scene::TerrainHeightFile::Extension));
        return project->MakeAssetReference(file);
    }

    bool SaveTerrainHeights(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain)
    {
        auto *project = shell.GetProject();
        if (!project)
            return false;
        if (terrain.GetHeightDataPath().empty())
        {
            terrain.SetHeightDataPath(DefaultTerrainHeightFile(shell, entity));
            entity.AddPrefabOverride("Component:TerrainComponent:HeightData");
        }
        std::string error;
        if (!terrain.SaveHeightData(scene::TerrainHeightEncoding::Float32, &error))
        {
            shell.Log(EditorShell::ConsoleSeverity::Error, "Could not write the terrain's heights: " + error);
            return false;
        }
        project->RefreshAssetEntry(project->ResolveAssetReference(terrain.GetHeightDataPath()));
        return true;
    }

    bool StoreTerrainHeightsInFile(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain)
    {
        if (!terrain.GetHeightDataPath().empty() || !shell.GetProject())
            return false;
        // An ordinary scene edit: before, the scene holds the samples; after, it names the file (which stays on disk,
        // so redo reads it back).
        shell.BeginSceneEdit("Store Terrain Heights In File");
        const bool saved = SaveTerrainHeights(shell, entity, terrain);
        if (!saved)
        {
            terrain.SetHeightDataPath({});
            shell.CancelSceneEdit();
            return false;
        }
        shell.MarkSceneDirty();
        shell.EndSceneEdit();
        return true;
    }

    bool ImportTerrainHeightMap(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain,
                                const std::string &path, const scene::TerrainHeightImportOptions &options,
                                glm::vec2 heightRange, std::string *error)
    {
        shell.FlushPendingSceneEdits();
        auto before = std::make_shared<HeightState>(Capture(terrain));
        if (!terrain.ImportHeightMap(path, options, heightRange, error))
            return false;
        if (shell.GetProject())
            SaveTerrainHeights(shell, entity, terrain);
        entity.AddPrefabOverride("Component:TerrainComponent:HeightSamples");
        entity.AddPrefabOverride("Component:TerrainComponent:HoleMask");
        auto after = std::make_shared<HeightState>(Capture(terrain));
        const std::uint64_t id = entity.GetID();
        const auto bytes = before->Bytes() + after->Bytes();
        shell.PushSceneEditCommand("Import Heightmap", [id, before] { return Restore(id, *before); },
                                   [id, after] { return Restore(id, *after); }, bytes);
        shell.MarkSceneDirty();
        return true;
    }

    void RenderTerrainHeightInspector(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain)
    {
        ImGui::SeparatorText("Heights");
        ImGui::Text("Resolution: %d x %d samples (%.0f x %.0f m)", terrain.GetWidth(), terrain.GetDepth(),
                    (terrain.GetWidth() - 1) * terrain.GetCellSize(), (terrain.GetDepth() - 1) * terrain.GetCellSize());

        if (!terrain.GetHeightDataPath().empty())
        {
            ImGui::TextDisabled("Height file: %s", terrain.GetHeightDataPath().c_str());
            if (!terrain.GetHeightBorder().empty())
                ImGui::TextDisabled("Tile border: shading continues into the neighbouring tiles");
            if (ImGui::Button("Reload Height File"))
            {
                shell.FlushPendingSceneEdits();
                auto before = std::make_shared<HeightState>(Capture(terrain));
                std::string error;
                if (terrain.LoadHeightData(&error))
                {
                    auto after = std::make_shared<HeightState>(Capture(terrain));
                    const std::uint64_t id = entity.GetID();
                    shell.PushSceneEditCommand("Reload Terrain Heights", [id, before] { return Restore(id, *before); },
                                               [id, after] { return Restore(id, *after); }, before->Bytes() + after->Bytes());
                }
                else
                    shell.Log(EditorShell::ConsoleSeverity::Error, "Could not reload the terrain's heights: " + error);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Read the heights again from the file (after another tool changed it)");
        }
        else
        {
            ImGui::BeginDisabled(!shell.GetProject());
            if (ImGui::Button("Store Heights In File"))
                StoreTerrainHeightsInFile(shell, entity, terrain);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Keep the heights in a binary .plutoheight file instead of the scene: much smaller,\n"
                                  "faster scenes for large terrains. Sculpting does this by itself.");
        }

        float lodScale = terrain.GetLodDistanceScale();
        if (ImGui::SliderFloat("LOD Distance Scale", &lodScale, 0.25f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
        {
            terrain.SetLodDistanceScale(lodScale);
            entity.AddPrefabOverride("Component:TerrainComponent:LodDistanceScale");
            shell.MarkSceneDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far chunks keep their detail. Raise it for large terrains seen from far away.");

        if (!ImGui::TreeNode("Import Heightmap"))
            return;
        auto &form = FormFor(entity);
        if (form.range.y < form.range.x)
            form.range = {0.0f, terrain.GetHeightScale()};
        ImGui::InputText("File", form.path.data(), form.path.size());
        ImGui::SameLine();
        if (ImGui::Button("...##TerrainHeightImport"))
            BrowseForHeightMap(form);
        constexpr const char *formats[] = {"Auto", "Image (8/16-bit)", "Raw 16-bit", "Raw 32-bit float", ".plutoheight"};
        ImGui::Combo("Format", &form.format, formats, IM_ARRAYSIZE(formats));
        const auto format = static_cast<scene::TerrainHeightSourceFormat>(form.format);
        const auto effective = format == scene::TerrainHeightSourceFormat::Auto ? scene::TerrainHeightFormatFromPath(form.path.data()) : format;
        const bool raw = effective == scene::TerrainHeightSourceFormat::Raw16 || effective == scene::TerrainHeightSourceFormat::Raw32Float;
        if (raw)
        {
            ImGui::InputInt("Raw Width", &form.rawWidth);
            ImGui::InputInt("Raw Depth", &form.rawDepth);
            ImGui::TextDisabled("0 x 0 reads a square grid from the file size");
            ImGui::Checkbox("Big Endian", &form.bigEndian);
        }
        if (effective != scene::TerrainHeightSourceFormat::PlutoHeight)
        {
            ImGui::Checkbox("Top Row Is Far Edge", &form.flipRows);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Images and most terrain tools write the far (+Z) edge first");
        }
        if (effective != scene::TerrainHeightSourceFormat::PlutoHeight && effective != scene::TerrainHeightSourceFormat::Raw32Float)
        {
            ImGui::DragFloat2("Height Range", &form.range.x, 0.1f, 0.0f, terrain.GetHeightScale(), "%.2f m");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Heights for black and white (the lowest and highest sample), within Height Scale");
        }
        ImGui::BeginDisabled(std::strlen(form.path.data()) == 0);
        if (ImGui::Button("Import"))
        {
            scene::TerrainHeightImportOptions options;
            options.format = format;
            options.rawWidth = std::max(0, form.rawWidth);
            options.rawDepth = std::max(0, form.rawDepth);
            options.rawBigEndian = form.bigEndian;
            options.flipRows = form.flipRows;
            form.lastError.clear();
            if (!ImportTerrainHeightMap(shell, entity, terrain, form.path.data(), options, form.range, &form.lastError))
                shell.Log(EditorShell::ConsoleSeverity::Error, "Heightmap import failed: " + form.lastError);
        }
        ImGui::EndDisabled();
        if (!form.lastError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", form.lastError.c_str());
        ImGui::TreePop();
    }

}
