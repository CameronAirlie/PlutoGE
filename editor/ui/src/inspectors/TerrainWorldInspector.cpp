// The Terrain World inspector: the world's size and streaming state, and building a world's tile and overview files
// from one large heightmap. The grid and streaming settings stay in the generic property list below.
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/TerrainHeightData.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/EditorShell.h"

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
        struct BuildForm
        {
            std::array<char, 512> source{};
            std::array<char, 256> folder{};
            int format = 0;
            int rawWidth = 0, rawDepth = 0;
            bool bigEndian = false;
            bool flipRows = true;
            glm::vec2 range{0.0f, -1.0f};
            int overviewSamples = 16;
            bool compact = false;
            std::string message;
        };

        BuildForm &FormFor(const scene::Entity &entity)
        {
            static std::unordered_map<scene::EntityID, BuildForm> forms;
            auto &form = forms[entity.GetID()];
            if (form.folder[0] == '\0')
            {
                const std::string folder = "TerrainWorld/" + (entity.GetName().empty() ? std::string("World") : entity.GetName());
                folder.copy(form.folder.data(), form.folder.size() - 1);
            }
            return form;
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

        // Decodes the source, writes the tiles and overview under Assets/<folder>, and points the world at them.
        void BuildWorld(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world, BuildForm &form)
        {
            auto *project = shell.GetProject();
            if (!project)
            {
                form.message = "Open a project first: the world's files go into its Assets.";
                return;
            }
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

            const auto folder = project->GetAssetDirectoryPath() / std::filesystem::path(form.folder.data());
            const auto tilePattern = (folder / "tile_{x}_{z}.plutoheight").string();
            const auto overview = (folder / "overview.plutoheight").string();
            const auto result = scene::TerrainWorldComponent::WriteWorldFiles(
                imported->field, grid, tilePattern, overview, std::max(form.overviewSamples, 1),
                form.compact ? scene::TerrainHeightEncoding::Unorm16 : scene::TerrainHeightEncoding::Float32);
            if (!result.Ok())
            {
                form.message = result.error;
                return;
            }
            std::error_code ignored;
            for (const auto &file : std::filesystem::directory_iterator(folder, ignored))
                project->RefreshAssetEntry(file.path());
            // The pattern keeps its {x}/{z} tokens, so it is made relative to the project by hand.
            const std::string folderReference = project->MakeAssetReference(folder);
            shell.ExecuteSceneEdit("Build Terrain World", [&] {
                world.SetTileFiles(folderReference + "/tile_{x}_{z}.plutoheight");
                world.SetOverviewFile(project->MakeAssetReference(overview));
                for (const char *name : {"TileFiles", "OverviewFile"})
                    entity.AddPrefabOverride(std::string("Component:TerrainWorldComponent:") + name);
            });
            form.message = "Wrote " + std::to_string(result.tilesWritten) + " tiles from a " + std::to_string(imported->field.width) + " x " +
                           std::to_string(imported->field.depth) + " heightmap" +
                           (imported->field.width != grid.WorldSamplesX() || imported->field.depth != grid.WorldSamplesZ()
                                ? " (resampled to " + std::to_string(grid.WorldSamplesX()) + " x " + std::to_string(grid.WorldSamplesZ()) + ")."
                                : ".");
        }

        void DrawTerrainWorldInspector(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &)
        {
            auto &world = static_cast<scene::TerrainWorldComponent &>(component);
            auto &shell = context.shell;
            const auto &grid = world.GetGrid();
            const glm::vec2 extent = grid.Extent();
            ImGui::Text("World: %d x %d tiles, %.0f x %.0f m (%d x %d samples)", grid.tilesX, grid.tilesZ, extent.x, extent.y,
                        grid.WorldSamplesX(), grid.WorldSamplesZ());
            ImGui::Text("Tiles: %.0f m, %d samples a side; loaded: %zu", grid.TileSize(), grid.tileSamples, world.GetActiveTileCount());
            if (((grid.tileSamples - 1) & (grid.tileSamples - 2)) != 0)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Tile Samples works best as a power of two plus one (129, 257, 513).");
            for (const auto &error : world.GetStreamingErrors())
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error.c_str());
            if (ImGui::Button("Load Around Camera"))
                world.LoadAround(shell.GetEditorCamera().position, std::chrono::seconds(30));
            ImGui::SameLine();
            if (ImGui::Button("Unload Tiles"))
                world.UnloadAll();

            if (!ImGui::TreeNode("Build World From Heightmap"))
                return;
            auto &form = FormFor(context.entity);
            if (form.range.y < form.range.x)
                form.range = {0.0f, grid.heightScale};
            ImGui::TextWrapped("Splits one heightmap for the whole world (from Gaea, World Machine...) into this grid's tile files "
                               "and an overview for distant terrain. Other sizes are resampled to the grid.");
            ImGui::InputText("Heightmap", form.source.data(), form.source.size());
            ImGui::SameLine();
            if (ImGui::Button("...##TerrainWorldSource"))
                Browse(form);
            constexpr const char *formats[] = {"Auto", "Image (8/16-bit)", "Raw 16-bit", "Raw 32-bit float", ".plutoheight"};
            ImGui::Combo("Format", &form.format, formats, IM_ARRAYSIZE(formats));
            ImGui::InputInt("Raw Width", &form.rawWidth);
            ImGui::InputInt("Raw Depth", &form.rawDepth);
            ImGui::Checkbox("Big Endian", &form.bigEndian);
            ImGui::SameLine();
            ImGui::Checkbox("Top Row Is Far Edge", &form.flipRows);
            ImGui::DragFloat2("Height Range", &form.range.x, 0.5f, 0.0f, grid.heightScale, "%.1f m");
            ImGui::InputText("Assets Folder", form.folder.data(), form.folder.size());
            ImGui::SliderInt("Overview Detail", &form.overviewSamples, 2, 64, "%d cells per tile");
            ImGui::Checkbox("16-bit Files", &form.compact);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Half the size on disk; heights quantized to 1/65535 of each tile's range");
            ImGui::BeginDisabled(std::strlen(form.source.data()) == 0);
            if (ImGui::Button("Build World Files"))
                BuildWorld(shell, context.entity, world, form);
            ImGui::EndDisabled();
            if (!form.message.empty())
                ImGui::TextWrapped("%s", form.message.c_str());
            ImGui::TreePop();
        }
    }

    void RegisterTerrainWorldInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::TerrainWorldComponent>(), &DrawTerrainWorldInspector);
    }
}
