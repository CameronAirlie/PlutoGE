// Terrain world sections (the world's settings and streaming, building its files from a heightmap or flat ground) and
// the compact Terrain World inspector that points at the Terrain Editor.
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/TerrainHeightData.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/TerrainEditorSections.h"

#include <imgui.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *ComponentName = "TerrainWorldComponent";

        struct BuildForm
        {
            std::array<char, 512> source{};
            int format = 0;
            int rawWidth = 0, rawDepth = 0;
            bool bigEndian = false;
            bool flipRows = true;
            glm::vec2 range{0.0f, -1.0f};
            float flatHeight = 20.0f;
            int overviewSamples = 16;
            bool compact = false;
            std::string message;
        };

        BuildForm &FormFor(const scene::Entity &entity)
        {
            static std::unordered_map<scene::EntityID, BuildForm> forms;
            return forms[entity.GetID()];
        }

        void Browse(BuildForm &form)
        {
#ifdef _WIN32
            OPENFILENAMEA dialog = {};
            char fileName[MAX_PATH] = "";
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = "Heightmaps\0*.png;*.tga;*.r16;*.raw;*.r32;*.f32;*.plutoheight\0All Files\0*.*\0";
            dialog.lpstrFile = fileName;
            dialog.nMaxFile = MAX_PATH;
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&dialog))
            {
                form.source.fill('\0');
                strncpy_s(form.source.data(), form.source.size(), fileName, _TRUNCATE);
            }
#else
            (void)form;
#endif
        }

        // The folder a world's files go in: where its tile pattern points, or TerrainWorld/<name>.
        std::filesystem::path WorldFolder(const assets::Project &project, const scene::Entity &entity, const scene::TerrainWorldComponent &world)
        {
            const auto &pattern = world.GetTileFiles();
            if (const auto slash = pattern.find_last_of('/'); pattern.starts_with("project://") && slash != std::string::npos)
                return project.ResolveAssetReference(pattern.substr(0, slash));
            return project.GetAssetDirectoryPath() / "TerrainWorld" / (entity.GetName().empty() ? std::string("World") : entity.GetName());
        }

        // Writes the world's files from a world-sized field and points the world at them, as one scene edit.
        bool WriteWorld(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world, const scene::TerrainHeightField &heights,
                        int overviewSamples, bool compact, std::string &message)
        {
            auto *project = shell.GetProject();
            if (!project)
            {
                message = "Open a project first: the world's files go into its Assets.";
                return false;
            }
            const auto folder = WorldFolder(*project, entity, world);
            const auto overview = folder / "overview.plutoheight";
            const auto result = scene::TerrainWorldComponent::WriteWorldFiles(
                heights, world.GetGrid(), (folder / "tile_{x}_{z}.plutoheight").string(), overview.string(), std::max(overviewSamples, 1),
                compact ? scene::TerrainHeightEncoding::Unorm16 : scene::TerrainHeightEncoding::Float32);
            if (!result.Ok())
            {
                message = result.error;
                return false;
            }
            std::error_code ignored;
            for (const auto &file : std::filesystem::directory_iterator(folder, ignored))
                project->RefreshAssetEntry(file.path());
            // The pattern keeps its {x}/{z} tokens, so it is made relative to the project by hand.
            const std::string tileFiles = project->MakeAssetReference(folder) + "/tile_{x}_{z}.plutoheight";
            const std::string overviewFile = project->MakeAssetReference(overview);
            // A scene edit only when the world must point somewhere new (a new world already points here).
            if (tileFiles != world.GetTileFiles() || overviewFile != world.GetOverviewFile())
                shell.ExecuteSceneEdit("Build Terrain World", [&] {
                    world.SetTileFiles(tileFiles);
                    world.SetOverviewFile(overviewFile);
                    for (const char *name : {"TileFiles", "OverviewFile"})
                        entity.AddPrefabOverride(std::string("Component:") + ComponentName + ":" + name);
                });
            world.Reload();
            message = "Wrote " + std::to_string(result.tilesWritten) + " tiles.";
            return true;
        }

        void BuildFromHeightmap(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world, BuildForm &form)
        {
            std::ifstream input(form.source.data(), std::ios::binary);
            if (!input)
            {
                form.message = std::string("Could not read ") + form.source.data() + ".";
                return;
            }
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            scene::TerrainHeightImportOptions options;
            options.format = static_cast<scene::TerrainHeightSourceFormat>(form.format);
            options.rawWidth = std::max(form.rawWidth, 0);
            options.rawDepth = std::max(form.rawDepth, 0);
            options.rawBigEndian = form.bigEndian;
            options.flipRows = form.flipRows;
            std::string error;
            auto imported = scene::DecodeTerrainHeightSource(bytes, form.source.data(), options, &error);
            if (!imported)
            {
                form.message = error;
                return;
            }
            const auto &grid = world.GetGrid();
            for (auto &sample : imported->field.samples)
                sample = std::clamp(imported->normalized ? form.range.x + sample * (form.range.y - form.range.x) : sample, 0.0f, grid.heightScale);
            if (WriteWorld(shell, entity, world, imported->field, form.overviewSamples, form.compact, form.message) &&
                (imported->field.width != grid.WorldSamplesX() || imported->field.depth != grid.WorldSamplesZ()))
                form.message += " The " + std::to_string(imported->field.width) + " x " + std::to_string(imported->field.depth) +
                                " heightmap was resampled to " + std::to_string(grid.WorldSamplesX()) + " x " + std::to_string(grid.WorldSamplesZ()) + ".";
        }

        void DrawTerrainWorldInspector(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            auto &world = static_cast<scene::TerrainWorldComponent &>(component);
            const auto &grid = world.GetGrid();
            const glm::vec2 extent = grid.Extent();
            ImGui::Text("%d x %d tiles, %.0f x %.0f m; %zu loaded", grid.tilesX, grid.tilesZ, extent.x, extent.y, world.GetActiveTileCount());
            ImGui::TextDisabled(world.GetTileTemplate() ? "Tiles copy the Tile Template child." : "No Tile Template: plain tiles.");
            for (const auto &error : world.GetStreamingErrors())
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error.c_str());
            if (ImGui::Button("Open Terrain Editor"))
                context.shell.OpenTerrainEditor();
            // The grid and streaming settings live in the Terrain Editor's World tab; the rest stay here.
            std::erase_if(properties, [](const scene::Property &property) {
                return property.name == "TilesX" || property.name == "TilesZ" || property.name == "TileSamples" || property.name == "CellSize" ||
                       property.name == "HeightScale" || property.name == "LoadRadius" || property.name == "UnloadRadius" ||
                       property.name == "MaxLoadsInFlight" || property.name == "ActivationsPerFrame" || property.name == "StreamInEditor";
            });
        }
    }

    void DrawTerrainWorldSettings(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world)
    {
        // The grid is edited as a draft and applied at once: a new grid drops every loaded tile.
        static std::unordered_map<scene::EntityID, scene::TerrainWorldGrid> drafts;
        auto &grid = drafts.try_emplace(entity.GetID(), world.GetGrid()).first->second;
        const glm::vec2 extent = grid.Extent();
        ImGui::Text("World: %.0f x %.0f m, %d x %d samples", extent.x, extent.y, grid.WorldSamplesX(), grid.WorldSamplesZ());
        ImGui::DragInt("Tiles X", &grid.tilesX, 0.1f, 1, 256);
        ImGui::DragInt("Tiles Z", &grid.tilesZ, 0.1f, 1, 256);
        constexpr std::array sampleOptions{65, 129, 257, 513, 1025};
        if (ImGui::BeginCombo("Tile Samples", std::to_string(grid.tileSamples).c_str()))
        {
            for (const int option : sampleOptions)
                if (ImGui::Selectable(std::to_string(option).c_str(), option == grid.tileSamples))
                    grid.tileSamples = option;
            ImGui::EndCombo();
        }
        ImGui::DragFloat("Cell Size", &grid.cellSize, 0.05f, 0.1f, 64.0f, "%.2f m");
        ImGui::DragFloat("Height Scale", &grid.heightScale, 1.0f, 1.0f, 10000.0f, "%.0f m");
        if (grid != world.GetGrid())
        {
            if (ImGui::Button("Apply Grid"))
                shell.ExecuteSceneEdit("Terrain World Grid", [&] {
                    world.SetGrid(grid);
                    for (const char *name : {"TilesX", "TilesZ", "TileSamples", "CellSize", "HeightScale"})
                        entity.AddPrefabOverride(std::string("Component:") + ComponentName + ":" + name);
                });
            ImGui::SameLine();
            if (ImGui::Button("Revert"))
                grid = world.GetGrid();
            ImGui::TextDisabled("A new grid needs new tile files: build them below.");
        }
        else
            grid = world.GetGrid(); // follow undo and other edits

        ImGui::SeparatorText("Streaming");
        auto streaming = world.GetStreaming();
        bool streamingChanged = ImGui::DragFloat("Load Radius", &streaming.loadRadius, 5.0f, 0.0f, 20000.0f, "%.0f m");
        streamingChanged |= ImGui::DragFloat("Unload Radius", &streaming.unloadRadius, 5.0f, 0.0f, 20000.0f, "%.0f m");
        streamingChanged |= ImGui::SliderInt("Loads In Flight", &streaming.maxLoadsInFlight, 1, 16);
        streamingChanged |= ImGui::SliderInt("Tiles Per Frame", &streaming.activationsPerFrame, 1, 16);
        if (streamingChanged)
        {
            world.SetStreaming(streaming);
            for (const char *name : {"LoadRadius", "UnloadRadius", "MaxLoadsInFlight", "ActivationsPerFrame"})
                entity.AddPrefabOverride(std::string("Component:") + ComponentName + ":" + name);
            shell.MarkSceneDirty();
        }
        bool streamInEditor = world.GetStreamInEditor();
        if (ImGui::Checkbox("Stream Around Editor Camera", &streamInEditor))
        {
            world.SetStreamInEditor(streamInEditor);
            entity.AddPrefabOverride(std::string("Component:") + ComponentName + ":StreamInEditor");
            shell.MarkSceneDirty();
        }
        ImGui::Text("Loaded tiles: %zu", world.GetActiveTileCount());
        ImGui::SameLine();
        if (ImGui::SmallButton("Load Around Camera"))
            world.LoadAround(shell.GetEditorCamera().position, std::chrono::seconds(30));
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload"))
            world.Reload();
        for (const auto &error : world.GetStreamingErrors())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error.c_str());
    }

    bool WriteFlatTerrainWorld(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world, float height)
    {
        const auto &grid = world.GetGrid();
        scene::TerrainHeightField flat{grid.WorldSamplesX(), grid.WorldSamplesZ(),
                                       std::vector<float>(static_cast<std::size_t>(grid.WorldSamplesX()) * grid.WorldSamplesZ(),
                                                          std::clamp(height, 0.0f, grid.heightScale))};
        std::string message;
        const bool written = WriteWorld(shell, entity, world, flat, 8, false, message);
        if (!written)
            shell.Log(EditorShell::ConsoleSeverity::Error, "Terrain world: " + message);
        return written;
    }

    void DrawTerrainWorldBuilder(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world)
    {
        auto &form = FormFor(entity);
        const auto &grid = world.GetGrid();
        if (form.range.y < form.range.x)
            form.range = {0.0f, grid.heightScale};
        ImGui::TextWrapped("Builds the world's tile files and its distant-terrain overview: from one heightmap for the whole world "
                           "(Gaea, World Machine...; other sizes are resampled to %d x %d), or flat.",
                           grid.WorldSamplesX(), grid.WorldSamplesZ());
        ImGui::InputText("Heightmap", form.source.data(), form.source.size());
        ImGui::SameLine();
        if (ImGui::Button("...##TerrainWorldSource"))
            Browse(form);
        constexpr const char *formats[] = {"Auto", "Image (8/16-bit)", "Raw 16-bit", "Raw 32-bit float", ".plutoheight"};
        ImGui::Combo("Format", &form.format, formats, IM_ARRAYSIZE(formats));
        if (form.format == 2 || form.format == 3 || scene::TerrainHeightFormatFromPath(form.source.data()) == scene::TerrainHeightSourceFormat::Raw16)
        {
            ImGui::InputInt("Raw Width", &form.rawWidth);
            ImGui::InputInt("Raw Depth", &form.rawDepth);
            ImGui::Checkbox("Big Endian", &form.bigEndian);
        }
        ImGui::Checkbox("Top Row Is Far Edge", &form.flipRows);
        ImGui::DragFloat2("Height Range", &form.range.x, 0.5f, 0.0f, grid.heightScale, "%.1f m");
        ImGui::SliderInt("Overview Detail", &form.overviewSamples, 2, 64, "%d cells per tile");
        ImGui::Checkbox("16-bit Files", &form.compact);
        ImGui::SetItemTooltip("Half the size on disk; heights quantized to 1/65535 of each tile's range");
        ImGui::BeginDisabled(std::strlen(form.source.data()) == 0);
        if (ImGui::Button("Build From Heightmap"))
            BuildFromHeightmap(shell, entity, world, form);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("##FlatHeight", &form.flatHeight, 0.5f, 0.0f, grid.heightScale, "%.0f m");
        ImGui::SameLine();
        if (ImGui::Button("Build Flat"))
            form.message = WriteFlatTerrainWorld(shell, entity, world, form.flatHeight) ? "Wrote flat tiles." : "Could not write the tiles.";
        ImGui::SetItemTooltip("Replaces every tile with flat ground at this height.");
        if (!form.message.empty())
            ImGui::TextWrapped("%s", form.message.c_str());
    }

    void RegisterTerrainWorldInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::TerrainWorldComponent>(), &DrawTerrainWorldInspector);
    }
}
