#include "PlutoGE/ui/DetailGrassInspector.h"

#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/render/DetailGrass.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/SceneWind.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/TerrainEditTool.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// The detail grass inspector (docs/DETAIL_GRASS.md), top to bottom: a summary, the layer list (add from a preset,
// duplicate, reorder, enable, remove), and the selected layer's settings in sections ordered by how often they are
// changed: where it grows, size, colour, shading, wind, the clump, and distance and cost. Each section is a function
// of the layer it edits; edits go through one place that records undo and prefab overrides.
namespace PlutoGE::ui
{
    namespace
    {
        using Layer = scene::DetailGrassComponent::Layer;
        constexpr const char *ComponentName = "DetailGrassComponent";

        // What every part of the inspector edits through.
        struct Inspected
        {
            scene::Entity &entity;
            DetailGrassInspectorHost &host;
        };

        // ---- edits ---------------------------------------------------------------------------------------------

        // Prefab overrides for every serialized property an edit changed, added or removed (layers are stored as
        // "Layer.N.*", so reordering touches many).
        void RecordOverrides(Inspected &context, const std::vector<scene::Property> &before,
                             const scene::DetailGrassComponent &grass)
        {
            const auto after = grass.Serialize();
            std::unordered_map<std::string, const std::string *> old;
            for (const auto &property : before) old.emplace(property.name, &property.value);
            for (const auto &property : after)
            {
                const auto found = old.find(property.name);
                if (found == old.end() || *found->second != property.value)
                    context.entity.AddPrefabOverride(std::string("Component:") + ComponentName + ":" + property.name);
                if (found != old.end()) old.erase(found);
            }
            for (const auto &[name, value] : old) context.entity.AddPrefabOverride(std::string("Component:") + ComponentName + ":" + name);
        }

        // A change of the layer list: one labelled undo step on this entity.
        void EditLayers(Inspected &context, scene::DetailGrassComponent &grass, const char *label,
                        const std::function<void()> &edit)
        {
            context.host.EditStep(label, [&]
            {
                const auto before = grass.Serialize();
                edit();
                RecordOverrides(context, before, grass);
            });
        }

        // A control's change of one layer, already made on a copy: reported after the fact so drags merge.
        void CommitLayer(Inspected &context, scene::DetailGrassComponent &grass, std::size_t index, const Layer &layer)
        {
            const auto before = grass.Serialize();
            grass.SetLayer(index, layer);
            RecordOverrides(context, before, grass);
            context.host.Changed();
        }

        // Which layer each component's inspector shows (editor state, not saved).
        std::size_t &SelectedLayer(const scene::Entity &entity)
        {
            static std::unordered_map<scene::EntityID, std::size_t> selected;
            return selected[entity.GetID()];
        }

        std::uint32_t NewSeed()
        {
            static std::mt19937 random{std::random_device{}()};
            return std::uniform_int_distribution<std::uint32_t>(1u, 99999u)(random);
        }

        // ---- colours ---------------------------------------------------------------------------------------------

        // Layer colours are linear albedo; pickers and previews show them as they appear on screen.
        float ToSrgb(float linear)
        {
            linear = std::clamp(linear, 0.0f, 1.0f);
            return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        }
        float ToLinear(float srgb)
        {
            srgb = std::clamp(srgb, 0.0f, 1.0f);
            return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        }
        ImU32 ScreenColor(glm::vec3 linear, float alpha = 1.0f)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(ToSrgb(linear.r), ToSrgb(linear.g), ToSrgb(linear.b), alpha));
        }

        float Hash(std::uint32_t value)
        {
            value ^= value >> 16u;
            value *= 0x7feb352du;
            value ^= value >> 15u;
            value *= 0x846ca68bu;
            value ^= value >> 16u;
            return float(value >> 8u) * (1.0f / 16777216.0f);
        }

        // Linear colour of a blade at height share `v` (0 root, 1 tip) over `ground`, as the shader tints it.
        glm::vec3 BladeColor(const render::DetailGrassLayer &layer, glm::vec3 albedo, glm::vec3 ground, float v)
        {
            const float tip = std::sqrt(std::clamp(v, 0.0f, 1.0f));
            const glm::vec3 tinted = glm::mix(glm::mix(albedo, ground, layer.groundTint), albedo, tip);
            return tinted * glm::mix(layer.rootOcclusion, 1.0f, tip);
        }

        // A stand-in for the terrain's colour under previews.
        constexpr glm::vec3 PreviewGround{0.11f, 0.08f, 0.05f};

        // ---- small widgets ---------------------------------------------------------------------------------------

        // A button whose action waits for a second click in a small popup.
        bool ConfirmedButton(const char *label, const std::string &question, const char *tooltip = nullptr)
        {
            ImGui::PushID(label);
            if (ImGui::Button(label)) ImGui::OpenPopup("Confirm");
            if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
            bool confirmed = false;
            if (ImGui::BeginPopup("Confirm"))
            {
                ImGui::TextUnformatted(question.c_str());
                if (ImGui::Button("Yes"))
                {
                    confirmed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::PopID();
            return confirmed;
        }

        std::string Abbreviated(float value)
        {
            std::array<char, 32> text{};
            if (value >= 1.0e6f) std::snprintf(text.data(), text.size(), "%.1fM", value / 1.0e6f);
            else if (value >= 1.0e3f) std::snprintf(text.data(), text.size(), "%.0fk", value / 1.0e3f);
            else std::snprintf(text.data(), text.size(), "%.0f", value);
            return text.data();
        }

        // Controls that edit a layer copy and remember whether anything changed.
        struct Controls
        {
            bool changed = false;

            static void Tip(const char *tip)
            {
                if (tip) ImGui::SetItemTooltip("%s", tip);
            }
            void Slider(const char *label, float &value, float low, float high, const char *format, const char *tip,
                        ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp)
            {
                changed |= ImGui::SliderFloat(label, &value, low, high, format, flags);
                Tip(tip);
            }
            void Drag(const char *label, float &value, float speed, float low, float high, const char *format, const char *tip)
            {
                changed |= ImGui::DragFloat(label, &value, speed, low, high, format, ImGuiSliderFlags_AlwaysClamp);
                Tip(tip);
            }
            void Range(const char *label, float &low, float &high, float speed, float min, float max, const char *format, const char *tip)
            {
                changed |= ImGui::DragFloatRange2(label, &low, &high, speed, min, max, format, nullptr, ImGuiSliderFlags_AlwaysClamp);
                Tip(tip);
            }
            void Count(const char *label, std::uint32_t &value, int low, int high, const char *tip)
            {
                int count = static_cast<int>(value);
                if (ImGui::SliderInt(label, &count, low, high, "%d", ImGuiSliderFlags_AlwaysClamp))
                {
                    value = static_cast<std::uint32_t>(count);
                    changed = true;
                }
                Tip(tip);
            }
            void Color(const char *label, glm::vec3 &linear, const char *tip)
            {
                std::array<float, 3> screen{ToSrgb(linear.r), ToSrgb(linear.g), ToSrgb(linear.b)};
                if (ImGui::ColorEdit3(label, screen.data()))
                {
                    linear = {ToLinear(screen[0]), ToLinear(screen[1]), ToLinear(screen[2])};
                    changed = true;
                }
                Tip(tip);
            }
            void Seed(const char *label, std::uint32_t &seed, const char *tip)
            {
                ImGui::PushID(label);
                int value = static_cast<int>(seed);
                const auto &style = ImGui::GetStyle();
                const float button = ImGui::CalcTextSize("New").x + style.FramePadding.x * 2.0f;
                ImGui::SetNextItemWidth(std::max(40.0f, ImGui::CalcItemWidth() - button - style.ItemInnerSpacing.x));
                if (ImGui::InputInt("##Value", &value, 0, 0))
                {
                    seed = static_cast<std::uint32_t>(std::max(0, value));
                    changed = true;
                }
                Tip(tip);
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (ImGui::Button("New##Shuffle"))
                {
                    seed = NewSeed();
                    changed = true;
                }
                ImGui::SetItemTooltip("Pick a new random seed.");
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextUnformatted(label);
                ImGui::PopID();
            }
        };

        // ---- estimates -------------------------------------------------------------------------------------------

        // Estimates and clump meshes keyed by the settings they depend on, so the inspector does not rebuild them
        // every frame. Small: one entry per layer on screen.
        const render::DetailGrassEstimate &Estimate(const render::DetailGrassLayer &layer)
        {
            static std::vector<std::pair<render::DetailGrassLayer, render::DetailGrassEstimate>> cache;
            for (const auto &[key, estimate] : cache)
                if (key == layer) return estimate;
            if (cache.size() >= 16) cache.erase(cache.begin());
            cache.emplace_back(layer, render::EstimateDetailGrass(layer));
            return cache.back().second;
        }

        const render::DetailGrassClumpMesh &ClumpMesh(const render::DetailGrassClumpShape &shape)
        {
            static std::optional<std::pair<render::DetailGrassClumpShape, render::DetailGrassClumpMesh>> cached;
            if (!cached || cached->first != shape) cached.emplace(shape, render::BuildDetailGrassClump(shape));
            return cached->second;
        }

        // A 90-degree view sees about a quarter of the field around the camera.
        constexpr float ViewShare = 0.25f;

        // ---- the terrain's layers --------------------------------------------------------------------------------

        struct TerrainLayers
        {
            const scene::TerrainComponent *terrain = nullptr;
            std::vector<std::string> names;

            [[nodiscard]] bool Has(const std::string &name) const
            {
                return std::find(names.begin(), names.end(), name) != names.end();
            }
            // Whether a layer growing on `densityLayer` grows at all: empty grows everywhere.
            [[nodiscard]] bool Resolves(const std::string &densityLayer) const { return densityLayer.empty() || Has(densityLayer); }
        };

        TerrainLayers ReadTerrainLayers(const scene::DetailGrassComponent &grass)
        {
            TerrainLayers result;
            result.terrain = grass.GetTerrain();
            if (result.terrain)
                for (const auto &layer : result.terrain->GetLayers().GetLayers()) result.names.push_back(layer.name);
            return result;
        }

        // ---- summary ---------------------------------------------------------------------------------------------

        void DrawSummary(scene::DetailGrassComponent &grass, const TerrainLayers &terrain)
        {
            if (!terrain.terrain)
            {
                ImGui::TextColored(ImVec4(0.98f, 0.7f, 0.25f, 1.0f), "%s Needs a Terrain component on this entity.", icons::Warning);
                return;
            }
            float clumps = 0.0f, triangles = 0.0f;
            int drawn = 0;
            for (const auto &layer : grass.GetLayers())
            {
                if (!layer.settings.enabled || !terrain.Resolves(layer.densityLayer)) continue;
                const auto &estimate = Estimate(layer.settings);
                clumps += estimate.Clumps() * ViewShare;
                triangles += estimate.Triangles() * ViewShare;
                ++drawn;
            }
            if (drawn == 0)
            {
                ImGui::TextDisabled("No layer is drawing.");
                return;
            }
            ImGui::TextDisabled("Up to ~%s clumps, ~%s triangles in view", Abbreviated(clumps).c_str(), Abbreviated(triangles).c_str());
            ImGui::SetItemTooltip("Estimated for a camera on fully painted, flat ground; real counts are lower where layers\n"
                                  "are not painted or the view is blocked.");
        }

        // ---- layer list ------------------------------------------------------------------------------------------

        // The list operations the buttons and each row's menu share. Each is one undo step and moves the selection
        // with the layer.
        void DuplicateLayer(Inspected &context, scene::DetailGrassComponent &grass, std::size_t index, std::size_t &selected)
        {
            if (grass.GetLayers().size() >= scene::DetailGrassComponent::MaxLayers) return;
            EditLayers(context, grass, "Duplicate Grass Layer", [&]
            {
                auto layers = grass.GetLayers();
                auto copy = layers[index];
                copy.settings.name += " copy";
                copy.settings.seed = NewSeed(); // a copy at the same seed would grow exactly on top of the original
                layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
                grass.SetLayers(std::move(layers));
                selected = index + 1;
            });
        }

        void MoveLayer(Inspected &context, scene::DetailGrassComponent &grass, std::size_t index, std::size_t to, std::size_t &selected)
        {
            if (to >= grass.GetLayers().size() || to == index) return;
            EditLayers(context, grass, "Move Grass Layer", [&]
            {
                auto layers = grass.GetLayers();
                std::swap(layers[index], layers[to]);
                grass.SetLayers(std::move(layers));
                selected = to;
            });
        }

        void RemoveLayer(Inspected &context, scene::DetailGrassComponent &grass, std::size_t index, std::size_t &selected)
        {
            EditLayers(context, grass, "Remove Grass Layer", [&] { grass.RemoveLayer(index); });
            if (selected > index || selected >= grass.GetLayers().size()) selected = selected > 0 ? selected - 1 : 0;
        }

        void DrawLayerRow(Inspected &context, scene::DetailGrassComponent &grass, const TerrainLayers &terrain,
                          std::size_t index, std::size_t &selected)
        {
            const auto &layer = grass.GetLayers()[index];
            ImGui::PushID(static_cast<int>(index));

            bool enabled = layer.settings.enabled;
            if (ImGui::Checkbox("##Enabled", &enabled))
            {
                auto edited = layer;
                edited.settings.enabled = enabled;
                EditLayers(context, grass, enabled ? "Enable Grass Layer" : "Disable Grass Layer", [&] { grass.SetLayer(index, edited); });
            }
            ImGui::SetItemTooltip(enabled ? "Drawing. Untick to hide this layer." : "Hidden. Tick to draw this layer.");
            ImGui::SameLine();

            const float rowHeight = ImGui::GetFrameHeight();
            if (ImGui::Selectable("##Row", selected == index, ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight)))
                selected = index;
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            const bool hovered = ImGui::IsItemHovered();

            if (ImGui::BeginPopupContextItem("##RowMenu"))
            {
                selected = index;
                if (ImGui::MenuItem("Duplicate", nullptr, false, grass.GetLayers().size() < scene::DetailGrassComponent::MaxLayers))
                    DuplicateLayer(context, grass, index, selected);
                if (ImGui::MenuItem("Move Up", nullptr, false, index > 0)) MoveLayer(context, grass, index, index - 1, selected);
                if (ImGui::MenuItem("Move Down", nullptr, false, index + 1 < grass.GetLayers().size()))
                    MoveLayer(context, grass, index, index + 1, selected);
                ImGui::Separator();
                if (ImGui::MenuItem("Remove")) RemoveLayer(context, grass, index, selected);
                ImGui::EndPopup();
            }
            ImGui::PopID();
            if (index >= grass.GetLayers().size()) return; // removed from the menu
            const auto &shown = grass.GetLayers()[index];

            auto *drawList = ImGui::GetWindowDrawList();
            const float textY = min.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
            // Swatch: healthy and dry colour side by side.
            const float swatch = rowHeight - 8.0f;
            const ImVec2 swatchMin(min.x + 4.0f, min.y + 4.0f);
            drawList->AddRectFilled(swatchMin, ImVec2(swatchMin.x + swatch * 0.6f, swatchMin.y + swatch), ScreenColor(shown.settings.color), 2.0f,
                                    ImDrawFlags_RoundCornersLeft);
            drawList->AddRectFilled(ImVec2(swatchMin.x + swatch * 0.6f, swatchMin.y), ImVec2(swatchMin.x + swatch, swatchMin.y + swatch),
                                    ScreenColor(shown.settings.dryColor), 2.0f, ImDrawFlags_RoundCornersRight);

            const auto dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const bool resolves = terrain.Resolves(shown.densityLayer);
            const std::string where = shown.densityLayer.empty() ? "Everywhere"
                                    : resolves ? shown.densityLayer
                                               : std::string(icons::Warning) + " " + shown.densityLayer;
            const float whereWidth = ImGui::CalcTextSize(where.c_str()).x;
            const float whereX = max.x - whereWidth - 6.0f;
            drawList->AddText(ImVec2(whereX, textY), resolves ? dim : ImGui::GetColorU32(ImVec4(0.98f, 0.7f, 0.25f, 1.0f)), where.c_str());

            const float nameX = swatchMin.x + swatch + 8.0f;
            drawList->PushClipRect(ImVec2(nameX, min.y), ImVec2(whereX - 6.0f, max.y), true);
            drawList->AddText(ImVec2(nameX, textY), shown.settings.enabled ? ImGui::GetColorU32(ImGuiCol_Text) : dim, shown.settings.name.c_str());
            drawList->PopClipRect();

            if (hovered)
            {
                if (!resolves)
                    ImGui::SetTooltip("The terrain has no layer named \"%s\", so this grows nothing.", shown.densityLayer.c_str());
                else
                    ImGui::SetTooltip("Right-click to duplicate, move or remove.");
            }
        }

        void DrawAddLayerMenu(Inspected &context, scene::DetailGrassComponent &grass, std::size_t &selected)
        {
            const bool full = grass.GetLayers().size() >= scene::DetailGrassComponent::MaxLayers;
            ImGui::BeginDisabled(full);
            if (ImGui::Button((std::string(icons::Plus) + " Add Layer").c_str())) ImGui::OpenPopup("##AddGrassLayer");
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(full ? "A component holds at most 8 layers." : "Add a layer from a preset.");
            if (!ImGui::BeginPopup("##AddGrassLayer")) return;
            for (std::size_t preset = 0; preset < std::size_t(render::DetailGrassPreset::Count); ++preset)
            {
                const auto kind = static_cast<render::DetailGrassPreset>(preset);
                if (ImGui::MenuItem(render::DetailGrassPresetName(kind)))
                    EditLayers(context, grass, "Add Grass Layer", [&]
                    {
                        Layer layer{render::DetailGrassPresetLayer(kind)};
                        layer.settings.seed = NewSeed();
                        if (grass.AddLayer(std::move(layer))) selected = grass.GetLayers().size() - 1;
                    });
                ImGui::SetItemTooltip("%s", render::DetailGrassPresetDescription(kind));
            }
            ImGui::EndPopup();
        }

        void DrawLayerList(Inspected &context, scene::DetailGrassComponent &grass, const TerrainLayers &terrain,
                           std::size_t &selected)
        {
            const auto count = grass.GetLayers().size();
            const float rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
            const float listHeight = rowHeight * float(std::clamp<std::size_t>(count, 1, scene::DetailGrassComponent::MaxLayers)) +
                                     ImGui::GetStyle().WindowPadding.y * 2.0f;
            if (ImGui::BeginChild("##GrassLayers", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders))
            {
                if (count == 0) ImGui::TextDisabled("No layers: add one below.");
                for (std::size_t index = 0; index < grass.GetLayers().size(); ++index)
                    DrawLayerRow(context, grass, terrain, index, selected);
            }
            ImGui::EndChild();

            DrawAddLayerMenu(context, grass, selected);
            if (grass.GetLayers().empty()) return;
            selected = std::min(selected, grass.GetLayers().size() - 1);

            const auto current = selected;
            ImGui::SameLine();
            ImGui::BeginDisabled(grass.GetLayers().size() >= scene::DetailGrassComponent::MaxLayers);
            if (widgets::IconButton("##DuplicateGrassLayer", icons::Copy, "Duplicate the selected layer"))
                DuplicateLayer(context, grass, current, selected);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(current == 0);
            if (widgets::IconButton("##RaiseGrassLayer", icons::ArrowUp, "Move the selected layer up"))
                MoveLayer(context, grass, current, current - 1, selected);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(current + 1 >= grass.GetLayers().size());
            if (widgets::IconButton("##LowerGrassLayer", icons::ArrowDown, "Move the selected layer down"))
                MoveLayer(context, grass, current, current + 1, selected);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ConfirmedButton(icons::Trash, "Remove the layer \"" + grass.GetLayers()[current].settings.name + "\"?", "Remove the selected layer"))
                RemoveLayer(context, grass, current, selected);
        }

        // ---- previews --------------------------------------------------------------------------------------------

        // A strip of blades showing the colour mix: dry patches, per-clump jitter and the root tint.
        void DrawColorStrip(const render::DetailGrassLayer &layer)
        {
            const float width = ImGui::GetContentRegionAvail().x, height = 34.0f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(width, height));
            auto *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(ImVec2(origin.x, origin.y + height - 4.0f), ImVec2(origin.x + width, origin.y + height),
                                    ScreenColor(PreviewGround));
            const int blades = std::max(8, int(width / 5.0f));
            const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
            for (int blade = 0; blade < blades; ++blade)
            {
                // Low-frequency patch noise along the strip, as the shader's world-space patches.
                const float along = float(blade) / float(blades) * 40.0f / layer.patchSize;
                const auto cell = static_cast<std::uint32_t>(std::floor(along));
                const float t = along - std::floor(along), smooth = t * t * (3.0f - 2.0f * t);
                const float patch = glm::mix(Hash(cell * 7919u + layer.seed), Hash((cell + 1u) * 7919u + layer.seed), smooth);
                const float dry = std::clamp((layer.dryAmount - patch) * 4.0f + 0.5f, 0.0f, 1.0f);
                const float jitter = 1.0f + (Hash(std::uint32_t(blade) * 31u + 17u) * 2.0f - 1.0f) * layer.colorJitter;
                const glm::vec3 albedo = glm::mix(layer.color, layer.dryColor, dry) * jitter;
                const float x = origin.x + (float(blade) + 0.5f) / float(blades) * width + (Hash(std::uint32_t(blade) * 13u) - 0.5f) * 4.0f;
                const float tall = (0.55f + 0.45f * Hash(std::uint32_t(blade) * 101u + 3u)) * (height - 4.0f);
                const float lean = (Hash(std::uint32_t(blade) * 57u + 9u) - 0.5f) * 8.0f;
                const float base = origin.y + height - 3.0f;
                drawList->PrimReserve(3, 3);
                drawList->PrimVtx(ImVec2(x - 2.0f, base), white, ScreenColor(BladeColor(layer, albedo, PreviewGround, 0.0f)));
                drawList->PrimVtx(ImVec2(x + 2.0f, base), white, ScreenColor(BladeColor(layer, albedo, PreviewGround, 0.0f)));
                drawList->PrimVtx(ImVec2(x + lean, base - tall), white, ScreenColor(BladeColor(layer, albedo, PreviewGround, 1.0f)));
            }
        }

        // The clump mesh from the side at the layer's mid size, lit from above; drag to turn it.
        void DrawClumpPreview(const render::DetailGrassLayer &layer, int lod, float &yaw)
        {
            const auto &mesh = ClumpMesh(layer.shape);
            const float width = ImGui::GetContentRegionAvail().x, height = 120.0f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##ClumpPreview", ImVec2(width, height));
            if (ImGui::IsItemActive()) yaw += ImGui::GetIO().MouseDelta.x * 0.01f;
            ImGui::SetItemTooltip("Drag to turn. Shown at the layer's average height and width.");

            auto *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
            const auto range = mesh.lods[std::clamp(lod, 0, int(render::DetailGrassLodCount) - 1)];
            if (range.indexCount == 0) return;

            const float clumpHeight = 0.5f * (layer.minHeight + layer.maxHeight);
            const float clumpWidth = 0.5f * (layer.minWidth + layer.maxWidth);
            const float c = std::cos(yaw), s = std::sin(yaw);
            struct Point
            {
                glm::vec2 screen;
                float depth;
                float v;
                float light;
            };
            const glm::vec3 sun = glm::normalize(glm::vec3(0.4f, 0.8f, -0.45f));
            std::vector<Point> points(mesh.vertices.size());
            glm::vec2 low(FLT_MAX), high(-FLT_MAX);
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
            {
                const auto &vertex = mesh.vertices[i];
                const glm::vec3 p(vertex.position.x * clumpWidth, vertex.position.y * clumpHeight, vertex.position.z * clumpWidth);
                const glm::vec3 n(vertex.normal.x * c + vertex.normal.z * s, vertex.normal.y, -vertex.normal.x * s + vertex.normal.z * c);
                points[i] = {{p.x * c + p.z * s, p.y}, -p.x * s + p.z * c, vertex.position.y, 0.55f + 0.45f * std::abs(glm::dot(n, sun))};
                low = glm::min(low, points[i].screen);
                high = glm::max(high, points[i].screen);
            }
            // Fit with a margin; metres to pixels, y up.
            const float margin = 10.0f;
            const glm::vec2 extent = glm::max(high - low, glm::vec2(1.0e-3f));
            const float scale = std::min((width - 2.0f * margin) / extent.x, (height - 2.0f * margin) / std::max(extent.y, clumpHeight));
            const float centerX = origin.x + width * 0.5f - (low.x + high.x) * 0.5f * scale;
            const float groundY = origin.y + height - margin;
            drawList->AddLine(ImVec2(origin.x + margin, groundY), ImVec2(origin.x + width - margin, groundY), ScreenColor(PreviewGround * 2.0f), 2.0f);

            std::vector<std::uint32_t> triangles;
            for (std::uint32_t i = 0; i < range.indexCount; i += 3) triangles.push_back(range.firstIndex + i);
            const auto depth = [&](std::uint32_t first)
            {
                return points[mesh.indices[first]].depth + points[mesh.indices[first + 1]].depth + points[mesh.indices[first + 2]].depth;
            };
            std::sort(triangles.begin(), triangles.end(), [&](std::uint32_t a, std::uint32_t b) { return depth(a) > depth(b); });

            const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
            drawList->PushClipRect(origin, ImVec2(origin.x + width, origin.y + height), true);
            for (const auto first : triangles)
            {
                drawList->PrimReserve(3, 3);
                for (std::uint32_t corner = 0; corner < 3; ++corner)
                {
                    const auto &point = points[mesh.indices[first + corner]];
                    const auto color = BladeColor(layer, layer.color, PreviewGround, point.v) * point.light;
                    drawList->PrimVtx(ImVec2(centerX + point.screen.x * scale, groundY - point.screen.y * scale), white, ScreenColor(color));
                }
            }
            drawList->PopClipRect();
        }

        // Kept share against distance, with the LOD bands and the fade-out, plus a hover readout.
        void DrawDistanceGraph(const render::DetailGrassLayer &layer, const render::DetailGrassEstimate &estimate)
        {
            const auto lattice = render::MakeDetailGrassLattice(layer);
            const float width = ImGui::GetContentRegionAvail().x, height = 72.0f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##DistanceGraph", ImVec2(width, height));
            const bool hovered = ImGui::IsItemHovered();
            auto *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);

            const float top = origin.y + 14.0f, bottom = origin.y + height - 4.0f;
            const auto xOf = [&](float distance) { return origin.x + distance / layer.drawDistance * width; };
            const std::array<float, 4> bands{0.0f, layer.lod1Distance, layer.lod2Distance, layer.drawDistance};
            const std::array<ImU32, 3> tints{IM_COL32(110, 170, 90, 40), IM_COL32(110, 150, 190, 40), IM_COL32(170, 130, 190, 40)};
            for (std::size_t lod = 0; lod < 3; ++lod)
            {
                if (bands[lod + 1] <= bands[lod]) continue;
                drawList->AddRectFilled(ImVec2(xOf(bands[lod]), origin.y), ImVec2(xOf(bands[lod + 1]), origin.y + height), tints[lod]);
                const std::string label = "LOD " + std::to_string(lod);
                if (xOf(bands[lod + 1]) - xOf(bands[lod]) > ImGui::CalcTextSize(label.c_str()).x + 6.0f)
                    drawList->AddText(ImVec2(xOf(bands[lod]) + 3.0f, origin.y + 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), label.c_str());
            }
            // The last fifth, where the field shrinks into the ground.
            const float fadeStart = layer.drawDistance * (1.0f - PLUTO_DETAIL_GRASS_DISTANCE_FADE);
            for (float x = xOf(fadeStart); x < origin.x + width; x += 4.0f)
                drawList->AddLine(ImVec2(x, top), ImVec2(std::min(x + 4.0f, origin.x + width), bottom), IM_COL32(0, 0, 0, 40));

            std::vector<ImVec2> curve;
            const int samples = std::max(16, int(width / 2.0f));
            for (int i = 0; i <= samples; ++i)
            {
                const float distance = layer.drawDistance * float(i) / float(samples);
                curve.emplace_back(xOf(distance), bottom - lattice.Keep(std::max(distance, 0.01f)) * (bottom - top));
            }
            curve.emplace_back(origin.x + width, bottom);
            drawList->AddPolyline(curve.data(), int(curve.size()), ScreenColor(layer.color * 2.0f), ImDrawFlags_None, 2.0f);

            if (hovered)
            {
                const float distance = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / width, 0.0f, 1.0f) * layer.drawDistance;
                drawList->AddLine(ImVec2(xOf(distance), origin.y), ImVec2(xOf(distance), origin.y + height), ImGui::GetColorU32(ImGuiCol_Text, 0.5f));
                const float keep = lattice.Keep(std::max(distance, 0.01f));
                ImGui::SetTooltip("%.0f m: %.0f%% of clumps grow (%.1f per m2), LOD %u%s", distance, keep * 100.0f,
                                  keep * estimate.clumpsPerSquareMetre, render::DetailGrassLod(layer, distance),
                                  distance > fadeStart ? ", fading out" : "");
            }
        }

        // ---- sections --------------------------------------------------------------------------------------------

        bool Section(const char *title, bool openByDefault)
        {
            ImGui::Spacing();
            return ImGui::CollapsingHeader(title, openByDefault ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
        }

        void DrawWhere(Controls &controls, Layer &layer, const TerrainLayers &terrain, DetailGrassInspectorHost &host)
        {
            if (!Section("Where It Grows", true)) return;
            const bool resolves = terrain.Resolves(layer.densityLayer);
            const std::string preview = layer.densityLayer.empty() ? "Everywhere"
                                      : resolves ? layer.densityLayer
                                                 : layer.densityLayer + " (missing)";
            if (ImGui::BeginCombo("Grows On", preview.c_str()))
            {
                if (ImGui::Selectable("Everywhere", layer.densityLayer.empty()))
                {
                    layer.densityLayer.clear();
                    controls.changed = true;
                }
                if (!terrain.names.empty()) ImGui::Separator();
                for (const auto &name : terrain.names)
                    if (ImGui::Selectable(name.c_str(), name == layer.densityLayer))
                    {
                        layer.densityLayer = name;
                        controls.changed = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("The terrain layer whose paint is this grass's density: paint that layer to grow it,\n"
                                  "erase it to clear it. Everywhere ignores the paint.");
            if (!resolves)
                ImGui::TextColored(ImVec4(0.98f, 0.7f, 0.25f, 1.0f), "%s The terrain has no layer \"%s\": nothing grows.", icons::Warning,
                                   layer.densityLayer.c_str());
            else if (terrain.names.empty())
                ImGui::TextDisabled("Add layers to the terrain to choose where this grows.");

            if (!layer.densityLayer.empty() && resolves && host.CanPaintTerrain())
            {
                if (ImGui::Button((std::string(icons::Paint) + " Paint Where It Grows").c_str()))
                    host.PaintTerrainLayer(layer.densityLayer);
                ImGui::SetItemTooltip("Paint the \"%s\" layer in the viewport: grass grows where it is painted.", layer.densityLayer.c_str());
            }

            ImGui::BeginDisabled(layer.densityLayer.empty());
            controls.Slider("Paint Threshold", layer.settings.weightThreshold, 0.0f, 0.95f, "%.2f",
                            "Paint weaker than this grows nothing; above it the grass thickens to full.\n"
                            "Raise it to keep grass off the soft edges of painted strokes.");
            ImGui::EndDisabled();
            controls.Slider("Max Slope", layer.settings.maxSlopeDegrees, 0.0f, 90.0f, "%.0f deg",
                            "Steeper ground thins out to nothing over the last 10 degrees.");

            auto &grass = layer.settings;
            bool limited = grass.minAltitude > -9999.0f || grass.maxAltitude < 9999.0f;
            if (ImGui::Checkbox("Limit Height", &limited))
            {
                grass.minAltitude = limited ? 0.0f : -10000.0f;
                grass.maxAltitude = limited ? 100.0f : 10000.0f;
                controls.changed = true;
            }
            ImGui::SetItemTooltip("Grow only between two world heights: above a beach, below a snow line.");
            if (limited)
            {
                controls.Range("Heights", grass.minAltitude, grass.maxAltitude, 0.5f, -10000.0f, 10000.0f, "%.1f m",
                               "The world heights the grass grows between.");
                controls.Drag("Height Fade", grass.altitudeFade, 0.1f, 0.01f, 500.0f, "%.1f m",
                              "Over how many metres beyond the heights the grass thins out to nothing.");
            }
            if (ImGui::Checkbox("Grow Under Water", &grass.growUnderWater))
                controls.changed = true;
            ImGui::SetItemTooltip("Off: no grass under lakes, rivers or the sea (their still level). On: grass grows under\n"
                                  "water as on land, for submerged meadows.");
            if (!grass.growUnderWater)
                controls.Drag("Shore Margin", grass.shoreMargin, 0.01f, -2.0f, 10.0f, "%.2f m",
                              "How far above the waterline the grass stops: higher leaves a bare, wet shore; negative lets\n"
                              "it reach into the shallows. It fades in over 15 cm above that.");
            controls.Slider("Bare Patches", grass.clustering, 0.0f, 0.95f, "%.2f",
                            "Leave patches of bare ground: 0 covers everything the rest allows, higher values leave more\n"
                            "and larger gaps. The patches are as big as the colour patches (Colour > Patch Size).");
        }

        void DrawSize(Controls &controls, Layer &layer, const render::DetailGrassEstimate &estimate)
        {
            if (!Section("Size And Density", true)) return;
            auto &grass = layer.settings;
            controls.Slider("Density", grass.density, 0.1f, 64.0f, "%.1f clumps/m2",
                            "Clumps per square metre near the camera, where the paint is full. Cost grows with it.",
                            ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            if (estimate.clumpsPerSquareMetre < grass.density * 0.98f)
                ImGui::TextColored(ImVec4(0.98f, 0.7f, 0.25f, 1.0f), "%s Limited to %.1f/m2 at this draw distance.", icons::Warning,
                                   estimate.clumpsPerSquareMetre);
            controls.Range("Height", grass.minHeight, grass.maxHeight, 0.01f, 0.01f, 5.0f, "%.2f m",
                           "Each clump's height is picked from this range.");
            controls.Range("Width", grass.minWidth, grass.maxWidth, 0.01f, 0.1f, 5.0f, "x%.2f",
                           "Each clump's spread is the clump shape scaled by a value from this range.");
            controls.Slider("Follow Slope", grass.alignToGround, 0.0f, 1.0f, "%.2f",
                            "0 grows straight up; 1 grows out of the ground along its slope.");
            controls.Seed("Seed", grass.seed, "Where clumps stand and their random sizes. Change it to vary two layers alike.");
        }

        void DrawColor(Controls &controls, Layer &layer)
        {
            if (!Section("Colour", true)) return;
            auto &grass = layer.settings;
            DrawColorStrip(grass);
            controls.Color("Healthy", grass.color, "The colour of healthy grass.");
            controls.Color("Dry", grass.dryColor, "The colour of dry patches.");
            controls.Slider("Dry Amount", grass.dryAmount, 0.0f, 1.0f, "%.2f", "How much of the ground the dry patches cover.");
            controls.Drag("Patch Size", grass.patchSize, 0.1f, 0.5f, 200.0f, "%.1f m", "How far across a typical dry or healthy patch is.");
            controls.Slider("Variation", grass.colorJitter, 0.0f, 1.0f, "%.2f", "How much each clump's brightness differs from its neighbours.");
        }

        void DrawShading(Controls &controls, Layer &layer, const assets::Project *project)
        {
            if (!Section("Shading", false)) return;
            auto &grass = layer.settings;
            controls.Slider("Ground Tint", grass.groundTint, 0.0f, 1.0f, "%.2f",
                            "How much blade roots take the terrain's colour beneath them, blending grass into the ground.");
            controls.Slider("Root Shade", grass.rootOcclusion, 0.0f, 1.0f, "%.2f",
                            "Light reaching the roots: lower is darker at the base, like a thick sward's own shade.");
            controls.Slider("Soft Shading", grass.normalBend, 0.0f, 1.0f, "%.2f",
                            "Bends blade normals toward the ground's so the field shades as one soft surface instead of\n"
                            "thousands of noisy cards. 0 shades every blade on its own.");

            if (RenderProjectAssetPicker("Material", project, assets::ProjectAssetType::Material, layer.materialAsset,
                                         {.noneLabel = "Built-in grass", .emptyPreview = "Built-in grass"}))
                controls.changed = true;
            ImGui::SetItemTooltip("Optional. The built-in material is white, two-sided and translucent, and takes its colour\n"
                                  "from this layer. With your own, set the layer colours to white to keep the material's.");
            ImGui::BeginDisabled(!layer.materialAsset.empty());
            controls.Slider("Roughness", grass.roughness, 0.04f, 1.0f, "%.2f", "Of the built-in material.");
            controls.Slider("Translucency", grass.translucency, 0.0f, 1.0f, "%.2f",
                            "Of the built-in material: how much light glows through backlit blades.");
            ImGui::EndDisabled();
        }

        void DrawWind(Controls &controls, Layer &layer, bool sceneHasWind)
        {
            if (!Section("Wind", false)) return;
            auto &wind = layer.settings.wind;
            controls.changed |= ImGui::Checkbox("Sway In Wind", &wind.enabled);
            Controls::Tip("Bend with the scene's Wind component.");
            if (wind.enabled && !sceneHasWind) ImGui::TextDisabled("The scene is calm: add Environment > Wind to an entity.");
            ImGui::BeginDisabled(!wind.enabled);
            controls.Slider("Stiffness", wind.stiffness, 0.0f, 1.0f, "%.2f", "0 bends fully with the wind; 1 barely moves. Stiffer grass sways faster.");
            controls.Slider("Flutter", wind.flutter, 0.0f, 1.0f, "%.2f", "Fast, small motion of the blade tips.");
            ImGui::EndDisabled();
        }

        void DrawClump(Controls &controls, Layer &layer)
        {
            if (!Section("Clump Shape", false)) return;
            static int lod = 0;
            static float yaw = 0.6f;
            DrawClumpPreview(layer.settings, lod, yaw);
            static constexpr std::array<const char *, 3> lods{"Near", "Middle", "Far"};
            widgets::SegmentedControl("##ClumpLod", &lod, lods);
            ImGui::SetItemTooltip("Preview the clump as drawn near the camera, in the middle distance (LOD 1) or far (LOD 2).");

            auto &shape = layer.settings.shape;
            controls.Count("Blades", shape.blades, 1, 64, "Blades in a near clump. Farther clumps draw fewer, wider blades.");
            controls.Drag("Clump Radius", shape.radius, 0.005f, 0.0f, 1.0f, "%.2f m", "How far apart the blade roots spread.");
            controls.Drag("Blade Width", shape.bladeWidth, 0.0005f, 0.001f, 0.2f, "%.3f m", "Width at the root; blades taper to a point.");
            controls.Slider("Shortest Blade", shape.minBladeHeight, 0.05f, 1.0f, "%.2f", "The shortest blade, as a share of the clump's height.");
            controls.Slider("Lean", shape.lean, 0.0f, 1.0f, "%.2f",
                            "How far the tips lean out. Wide, leaning clumps overlap into a sward; upright ones read as tufts.");
            controls.Slider("Curvature", shape.curvature, 0.0f, 1.0f, "%.2f", "0 keeps blades straight; 1 bends them over toward the tip.");
            controls.Seed("Shape Seed", shape.seed, "Which random clump is built.");
        }

        void DrawDistance(Controls &controls, Layer &layer, const render::DetailGrassEstimate &estimate)
        {
            if (!Section("Distance And Cost", false)) return;
            auto &grass = layer.settings;
            DrawDistanceGraph(grass, estimate);
            controls.Drag("Full Density Distance", grass.fullDensityDistance, 0.1f, 1.0f, 60.0f, "%.1f m",
                          "Every clump grows within this distance of the camera.");
            controls.Slider("Thinning", grass.densityFalloff, 1.5f, 3.0f, "%.2f",
                            "How fast clumps thin out beyond the full density distance. 2 keeps the clumps on screen even;\n"
                            "higher is cheaper but thinner in the distance.");
            controls.Drag("Draw Distance", grass.drawDistance, 0.5f, grass.fullDensityDistance, 300.0f, "%.0f m",
                          "The grass shrinks away into the ground over the last fifth of this distance.");
            controls.Slider("Distant Widening", grass.maxWidthCompensation, 1.0f, 4.0f, "x%.2f",
                            "Thinned clumps widen up to this much, so distant grass keeps its coverage.");
            controls.Range("LOD Distances", grass.lod1Distance, grass.lod2Distance, 0.5f, 0.0f, grass.drawDistance, "%.0f m",
                           "Where clumps switch to fewer blades (LOD 1) and to single-triangle blades (LOD 2).");

            ImGui::Spacing();
            if (ImGui::BeginTable("##GrassCost", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("");
                ImGui::TableSetupColumn("Clumps");
                ImGui::TableSetupColumn("Tris each");
                ImGui::TableSetupColumn("Triangles");
                ImGui::TableHeadersRow();
                for (std::uint32_t lod = 0; lod < render::DetailGrassLodCount; ++lod)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("LOD %u", lod);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(Abbreviated(estimate.clumps[lod] * ViewShare).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", estimate.trianglesPerClump[lod]);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(Abbreviated(estimate.clumps[lod] * ViewShare * float(estimate.trianglesPerClump[lod])).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("In view on painted flat ground; %s candidates tested per frame.", Abbreviated(float(estimate.candidates)).c_str());
        }

        void DrawSelectedLayer(Inspected &context, scene::DetailGrassComponent &grass, const TerrainLayers &terrain,
                               std::size_t index)
        {
            Layer layer = grass.GetLayers()[index];
            Controls controls;

            ImGui::Spacing();
            ImGui::SeparatorText("Layer");
            std::array<char, 128> name{};
            std::snprintf(name.data(), name.size(), "%s", layer.settings.name.c_str());
            if (ImGui::InputText("Name", name.data(), name.size()))
            {
                layer.settings.name = name.data();
                controls.changed = true;
            }

            ImGui::TextUnformatted("Preset");
            for (std::size_t preset = 0; preset < std::size_t(render::DetailGrassPreset::Count); ++preset)
            {
                const auto kind = static_cast<render::DetailGrassPreset>(preset);
                if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize(render::DetailGrassPresetName(kind)).x + 24.0f) ImGui::SameLine();
                if (ImGui::SmallButton(render::DetailGrassPresetName(kind)))
                {
                    const auto applied = render::DetailGrassApplyPreset(layer.settings, kind);
                    EditLayers(context, grass, "Apply Grass Preset", [&]
                    {
                        auto edited = grass.GetLayers()[index];
                        edited.settings = applied;
                        grass.SetLayer(index, edited);
                    });
                    return; // the copy is stale; the next frame draws the result
                }
                ImGui::SetItemTooltip("%s\nReplaces size, colour, shading, wind, clump and distances; keeps where it grows.",
                                      render::DetailGrassPresetDescription(kind));
            }

            const auto &estimate = Estimate(layer.settings);
            DrawWhere(controls, layer, terrain, context.host);
            DrawSize(controls, layer, estimate);
            DrawColor(controls, layer);
            DrawShading(controls, layer, context.host.Project());
            DrawWind(controls, layer, !render::BuildSceneWind(context.entity.GetScene()).Calm());
            DrawClump(controls, layer);
            DrawDistance(controls, layer, estimate);

            if (controls.changed) CommitLayer(context, grass, index, layer);
        }

        // The shell as the inspector's host: list changes are scoped to the entity, control edits report after the fact.
        class ShellHost final : public DetailGrassInspectorHost
        {
        public:
            ShellHost(EditorShell &shell, scene::EntityID entity) : m_shell(shell), m_entity(entity) {}
            void EditStep(const char *label, const std::function<void()> &edit) override
            {
                m_shell.ExecuteSceneEdit(label, edit, SceneEditScope::Entities({m_entity}));
            }
            void Changed() override { m_shell.MarkSceneDirty(); }
            const assets::Project *Project() const override { return m_shell.GetProject(); }
            bool CanPaintTerrain() const override { return true; }
            void PaintTerrainLayer(const std::string &layer) override
            {
                auto *scene = m_shell.GetScene();
                auto *entity = scene ? scene->FindEntityByID(m_entity) : nullptr;
                const auto target = TerrainEditTarget::Resolve(entity);
                const auto *terrain = target.Terrain();
                if (!terrain)
                    return;
                const auto &layers = terrain->GetLayers().GetLayers();
                const auto found = std::find_if(layers.begin(), layers.end(), [&](const auto &candidate) { return candidate.name == layer; });
                if (found == layers.end())
                    return;
                // On a world's template this paints the world's tiles.
                auto &tool = m_shell.GetTerrainTool();
                tool.paint.layer = static_cast<std::size_t>(found - layers.begin());
                tool.SetMode(TerrainToolMode::Paint, target.RootId());
            }

        private:
            EditorShell &m_shell;
            scene::EntityID m_entity;
        };

        void InspectDetailGrass(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            properties.clear(); // every setting has a place in the inspector
            ShellHost host(context.shell, context.entity.GetID());
            DrawDetailGrassInspector(static_cast<scene::DetailGrassComponent &>(component), context.entity, host);
        }
    }

    void DrawDetailGrassInspector(scene::DetailGrassComponent &grass, scene::Entity &entity, DetailGrassInspectorHost &host)
    {
        Inspected context{entity, host};
        const auto terrain = ReadTerrainLayers(grass);
        auto &selected = SelectedLayer(entity);

        DrawSummary(grass, terrain);
        DrawLayerList(context, grass, terrain, selected);
        if (grass.GetLayers().empty()) return;
        selected = std::min(selected, grass.GetLayers().size() - 1);
        ImGui::PushID("SelectedGrassLayer");
        DrawSelectedLayer(context, grass, terrain, selected);
        ImGui::PopID();
    }

    void DrawDetailGrassEditor(EditorShell &shell, scene::Entity &entity, scene::DetailGrassComponent &grass)
    {
        ShellHost host(shell, entity.GetID());
        DrawDetailGrassInspector(grass, entity, host);
    }

    void RegisterDetailGrassInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::DetailGrassComponent>(), &InspectDetailGrass);
    }
}
