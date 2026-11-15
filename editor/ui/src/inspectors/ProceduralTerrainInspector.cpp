// The Procedural Terrain inspector: generation, the global settings, the masks, and each biome's weight, layer and
// scatter rules. The definition is one scene property, so edits to it are ordinary undoable scene edits; generating
// changes the terrain's weights (files) and the foliage, and records its own undo step holding both.
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/TerrainLayerEditing.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        namespace procedural = scene::procedural;
        constexpr const char *ComponentName = "ProceduralTerrainComponent";

        bool EditText(const char *label, std::string &value)
        {
            std::array<char, 256> buffer{};
            value.copy(buffer.data(), buffer.size() - 1);
            if (!ImGui::InputText(label, buffer.data(), buffer.size()))
                return false;
            value = buffer.data();
            return true;
        }

        // A combo of names (layers, foliage types, masks) that also accepts names not in the list.
        bool EditName(const char *label, std::string &value, const std::vector<std::string> &names)
        {
            bool changed = false;
            const bool known = std::find(names.begin(), names.end(), value) != names.end();
            const std::string preview = value.empty() ? "(none)" : (known ? value : value + " (missing)");
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                if (ImGui::Selectable("(none)", value.empty()))
                    value.clear(), changed = true;
                for (const auto &name : names)
                    if (ImGui::Selectable(name.c_str(), name == value))
                        value = name, changed = true;
                ImGui::EndCombo();
            }
            return changed;
        }

        template <std::size_t Count>
        bool EditEnum(const char *label, std::uint8_t &value, const std::array<const char *, Count> &names)
        {
            int index = std::min<int>(value, static_cast<int>(Count) - 1);
            if (!ImGui::Combo(label, &index, names.data(), static_cast<int>(Count)))
                return false;
            value = static_cast<std::uint8_t>(index);
            return true;
        }

        bool EditRule(procedural::TerrainRule &rule, const std::vector<std::string> &masks)
        {
            static constexpr std::array<const char *, 7> Inputs{"Constant", "Height", "Slope", "Curvature", "Noise", "Mask", "Distance"};
            static constexpr std::array<const char *, 2> Remaps{"Band", "Linear"};
            static constexpr std::array<const char *, 6> Blends{"Multiply", "Add", "Subtract", "Min", "Max", "Replace"};
            static constexpr std::array<const char *, 3> NoiseTypes{"Value", "Fbm", "Ridged"};
            bool changed = ImGui::Checkbox("##Enabled", &rule.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            auto input = static_cast<std::uint8_t>(rule.input);
            if (EditEnum("##Input", input, Inputs))
            {
                // A new input starts from defaults that mean something for it.
                auto fresh = procedural::TerrainRule::Make(static_cast<procedural::RuleInput>(input));
                fresh.blend = rule.blend;
                fresh.opacity = rule.opacity;
                rule = fresh;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            auto blend = static_cast<std::uint8_t>(rule.blend);
            if (EditEnum("##Blend", blend, Blends))
                rule.blend = static_cast<procedural::RuleBlend>(blend), changed = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            changed |= ImGui::SliderFloat("##Opacity", &rule.opacity, 0.0f, 1.0f, "%.2f");

            ImGui::Indent();
            switch (rule.input)
            {
            case procedural::RuleInput::Constant:
                changed |= ImGui::SliderFloat("Value", &rule.constant, 0.0f, 1.0f);
                break;
            case procedural::RuleInput::Noise:
            {
                auto type = static_cast<std::uint8_t>(rule.noise.type);
                if (EditEnum("Noise", type, NoiseTypes))
                    rule.noise.type = static_cast<procedural::NoiseType>(type), changed = true;
                changed |= ImGui::DragFloat("Scale", &rule.noise.scale, 0.5f, 0.1f, 10000.0f, "%.1f m");
                changed |= ImGui::SliderInt("Octaves", &rule.noise.octaves, 1, 8);
                int seed = static_cast<int>(rule.noise.seed);
                if (ImGui::InputInt("Noise Seed", &seed))
                    rule.noise.seed = static_cast<std::uint32_t>(std::max(seed, 0)), changed = true;
                break;
            }
            case procedural::RuleInput::Mask:
                changed |= EditName("Mask", rule.source, masks);
                break;
            case procedural::RuleInput::Distance:
                changed |= EditText("Source Tag", rule.source);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Splines tagged (or named) this: rivers, roads, trails. Distances are from their edge.");
                break;
            default:
                break;
            }
            if (rule.input != procedural::RuleInput::Constant)
            {
                auto remap = static_cast<std::uint8_t>(rule.remap);
                if (EditEnum("Remap", remap, Remaps))
                    rule.remap = static_cast<procedural::RuleRemap>(remap), changed = true;
                const char *unit = rule.input == procedural::RuleInput::Slope ? "%.1f deg"
                                 : (rule.input == procedural::RuleInput::Height || rule.input == procedural::RuleInput::Distance) ? "%.1f m" : "%.3f";
                changed |= ImGui::DragFloatRange2("Range", &rule.min, &rule.max, 0.1f, -10000.0f, 10000.0f, unit, unit);
                if (rule.remap == procedural::RuleRemap::Band)
                    changed |= ImGui::DragFloat("Falloff", &rule.falloff, 0.05f, 0.0f, 10000.0f, unit);
                changed |= ImGui::Checkbox("Invert", &rule.invert);
            }
            ImGui::Unindent();
            return changed;
        }

        bool EditStack(const char *label, procedural::TerrainRuleStack &stack, const std::vector<std::string> &masks)
        {
            bool changed = false;
            ImGui::PushID(label);
            ImGui::TextDisabled("%s", label);
            std::optional<std::size_t> remove;
            std::optional<std::pair<std::size_t, std::size_t>> move;
            for (std::size_t index = 0; index < stack.rules.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                changed |= EditRule(stack.rules[index], masks);
                ImGui::Indent();
                if (index > 0 && ImGui::SmallButton("Up"))
                    move = std::pair{index, index - 1};
                ImGui::SameLine();
                if (index + 1 < stack.rules.size() && ImGui::SmallButton("Down"))
                    move = std::pair{index, index + 1};
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove Rule"))
                    remove = index;
                ImGui::Unindent();
                ImGui::PopID();
            }
            if (remove)
                stack.rules.erase(stack.rules.begin() + static_cast<std::ptrdiff_t>(*remove)), changed = true;
            else if (move)
                std::swap(stack.rules[move->first], stack.rules[move->second]), changed = true;
            if (ImGui::SmallButton("Add Rule"))
                stack.rules.push_back(procedural::TerrainRule::Make(procedural::RuleInput::Slope)), changed = true;
            if (stack.rules.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(everywhere)");
            }
            ImGui::PopID();
            return changed;
        }

        bool EditScatter(procedural::ProceduralScatterRule &rule, const std::vector<std::string> &types, const std::vector<std::string> &masks)
        {
            bool changed = EditName("Foliage Type", rule.foliageType, types);
            changed |= ImGui::DragFloat("Spacing", &rule.spacing, 0.05f, 0.25f, 500.0f, "%.2f m");
            changed |= ImGui::DragFloat("Footprint", &rule.footprint, 0.05f, 0.0f, 100.0f, rule.footprint > 0.0f ? "%.2f m" : "Half spacing");
            changed |= ImGui::SliderFloat("Density", &rule.densityScale, 0.0f, 1.0f);
            changed |= ImGui::DragFloatRange2("Slope", &rule.slopeLimits.x, &rule.slopeLimits.y, 0.2f, 0.0f, 90.0f, "%.0f deg");
            changed |= ImGui::DragFloatRange2("Height", &rule.heightLimits.x, &rule.heightLimits.y, 0.5f, -1.0e6f, 1.0e6f, "%.1f m");
            changed |= ImGui::DragFloatRange2("Scale", &rule.scaleRange.x, &rule.scaleRange.y, 0.01f, 0.01f, 20.0f);
            changed |= ImGui::SliderFloat("Align To Slope", &rule.alignToSlope, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Sink", &rule.sink, 0.01f, 0.0f, 10.0f, "%.2f m");
            changed |= ImGui::Checkbox("Random Yaw", &rule.randomYaw);
            int seed = static_cast<int>(rule.seed);
            if (ImGui::InputInt("Seed", &seed))
                rule.seed = static_cast<std::uint32_t>(std::max(seed, 0)), changed = true;
            changed |= EditStack("Density Rules", rule.density, masks);
            return changed;
        }

        bool EditBiome(procedural::ProceduralBiome &biome, const std::vector<std::string> &layers, const std::vector<std::string> &types,
                       const std::vector<std::string> &masks)
        {
            bool changed = EditText("Name", biome.name);
            changed |= ImGui::Checkbox("Enabled", &biome.enabled);
            changed |= EditName("Mask", biome.mask, masks);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Where the biome is, from a mask; (none) is everywhere the weight rules allow");
            changed |= EditStack("Weight Rules", biome.weight, masks);

            if (ImGui::TreeNode("Layers", "Layers (%zu)", biome.layers.size()))
            {
                ImGui::TextDisabled("Bottom to top: later layers paint over earlier ones; the first takes the rest.");
                std::optional<std::size_t> remove;
                for (std::size_t index = 0; index < biome.layers.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::Separator();
                    changed |= EditName("Terrain Layer", biome.layers[index].layer, layers);
                    if (index > 0)
                        changed |= EditStack("Coverage Rules", biome.layers[index].coverage, masks);
                    else
                        ImGui::TextDisabled("Base layer: covers what the layers above leave");
                    if (ImGui::SmallButton("Remove Layer"))
                        remove = index;
                    ImGui::PopID();
                }
                if (remove)
                    biome.layers.erase(biome.layers.begin() + static_cast<std::ptrdiff_t>(*remove)), changed = true;
                if (ImGui::Button("Add Layer"))
                    biome.layers.push_back({layers.empty() ? std::string() : layers[std::min(biome.layers.size(), layers.size() - 1)], {}}), changed = true;
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Scatter", "Scatter (%zu)", biome.scatter.size()))
            {
                std::optional<std::size_t> remove;
                for (std::size_t index = 0; index < biome.scatter.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::Separator();
                    changed |= EditScatter(biome.scatter[index], types, masks);
                    if (ImGui::SmallButton("Remove Scatter"))
                        remove = index;
                    ImGui::PopID();
                }
                if (remove)
                    biome.scatter.erase(biome.scatter.begin() + static_cast<std::ptrdiff_t>(*remove)), changed = true;
                if (ImGui::Button("Add Scatter"))
                {
                    procedural::ProceduralScatterRule rule;
                    rule.foliageType = types.empty() ? std::string() : types.front();
                    biome.scatter.push_back(rule);
                    changed = true;
                }
                ImGui::TreePop();
            }
            return changed;
        }

        bool EditMask(procedural::ProceduralMaskSource &mask, const assets::Project *project)
        {
            static constexpr std::array<const char *, 4> Channels{"Red", "Green", "Blue", "Alpha"};
            bool changed = EditText("Name", mask.name);
            changed |= RenderProjectAssetPicker("Image", project, assets::ProjectAssetType::Texture, mask.image);
            auto channel = static_cast<std::uint8_t>(mask.channel);
            if (EditEnum("Channel", channel, Channels))
                mask.channel = channel, changed = true;
            changed |= ImGui::DragFloat2("World Min", &mask.worldMin.x, 1.0f);
            changed |= ImGui::DragFloat2("World Size", &mask.worldSize.x, 1.0f, 0.0f, 1.0e6f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The world rectangle (x, z) the image covers. 0 x 0 spans this terrain;\n"
                                  "give the world's rectangle for a mask shared by terrain tiles.");
            return changed;
        }

        // Generates as one undo step: the terrain's weights (saved to their files) and the foliage instances.
        void GenerateWithUndo(EditorShell &shell, scene::Entity &entity, scene::ProceduralTerrainComponent &procedural)
        {
            auto *terrain = procedural.GetTerrain();
            if (!terrain)
                return;
            auto *foliage = procedural.GetFoliage();
            shell.FlushPendingSceneEdits();
            struct State
            {
                scene::TerrainLayerStack::Snapshot layers;
                std::optional<scene::FoliageInstanceSnapshot> foliage;
            };
            const auto capture = [&] {
                return std::make_shared<State>(State{terrain->GetLayers().Capture(),
                                                     foliage ? std::optional(foliage->CaptureInstanceSnapshot()) : std::nullopt});
            };
            auto before = capture();
            const auto result = procedural.Generate();
            auto after = capture();
            SaveTerrainWeightMaps(shell, entity, *terrain);
            const auto id = entity.GetID();
            const auto restore = [id](const State &state) {
                auto *scene = EditorShell::GetInstance().GetScene();
                auto *owner = scene ? scene->FindEntityByID(id) : nullptr;
                auto *target = owner ? owner->GetComponent<scene::TerrainComponent>() : nullptr;
                if (!target)
                    return false;
                target->GetLayers().Restore(state.layers);
                SaveTerrainWeightMaps(EditorShell::GetInstance(), *owner, *target);
                if (auto *trees = owner->GetComponent<scene::FoliageComponent>(); trees && state.foliage)
                    trees->RestoreInstanceSnapshot(*state.foliage);
                return true;
            };
            std::size_t bytes = 0;
            for (const auto *state : {before.get(), after.get()})
            {
                const auto size = static_cast<std::size_t>(state->layers.weights.GetResolution());
                bytes += size * size * scene::TerrainWeightMap::kLayers;
                if (state->foliage)
                    for (const auto &type : *state->foliage)
                        bytes += type.size() * sizeof(scene::FoliageInstance);
            }
            shell.PushSceneEditCommand("Generate Procedural Terrain", [restore, before] { return restore(*before); },
                                       [restore, after] { return restore(*after); }, bytes);
            if (foliage)
                for (std::size_t type = 0; type < foliage->GetTypeCount(); ++type)
                    entity.AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(type) + ".Instances");
            shell.MarkSceneDirty();
            shell.Log(EditorShell::ConsoleSeverity::Info, "Procedural terrain: " + std::to_string(result.InstanceCount()) +
                                                              " foliage instances" + (result.weights ? ", layer weights painted." : "."));
            for (const auto &warning : result.warnings)
                shell.Log(EditorShell::ConsoleSeverity::Warning, "Procedural terrain: " + warning);
        }

        void DrawProceduralTerrainInspector(scene::Component &component, ComponentInspectorContext &context,
                                            std::vector<scene::Property> &properties)
        {
            auto &procedural = static_cast<scene::ProceduralTerrainComponent &>(component);
            auto &shell = context.shell;
            auto &entity = context.entity;
            std::erase_if(properties, [](const scene::Property &property) { return property.name == "Definition"; });

            std::vector<std::string> layers, types, masks;
            if (const auto *terrain = procedural.GetTerrain())
                for (const auto &layer : terrain->GetLayers().GetLayers())
                    layers.push_back(layer.name);
            if (const auto *foliage = procedural.GetFoliage())
                for (std::size_t index = 0; index < foliage->GetTypeCount(); ++index)
                    types.push_back(foliage->GetType(index)->name);
            auto definition = procedural.GetDefinition();
            for (const auto &mask : definition.masks)
                masks.push_back(mask.name);

            if (ImGui::Button("Generate"))
                GenerateWithUndo(shell, entity, procedural);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu biomes", definition.biomes.size());
            if (layers.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Add material layers to the terrain to paint them.");
            if (!procedural.GetFoliage())
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Add a Foliage Component to this entity to scatter plants.");
            for (const auto &warning : procedural.GetLastWarnings())
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", warning.c_str());

            bool changed = false;
            int seed = static_cast<int>(definition.seed);
            if (ImGui::InputInt("World Seed", &seed))
                definition.seed = static_cast<std::uint32_t>(std::max(seed, 0)), changed = true;
            changed |= ImGui::DragFloat("Biome Blend", &definition.biomeBlend, 0.1f, 0.0f, 500.0f, "%.1f m");

            if (ImGui::TreeNode("Masks", "Masks (%zu)", definition.masks.size()))
            {
                std::optional<std::size_t> remove;
                for (std::size_t index = 0; index < definition.masks.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::Separator();
                    changed |= EditMask(definition.masks[index], shell.GetProject());
                    if (ImGui::SmallButton("Remove Mask"))
                        remove = index;
                    ImGui::PopID();
                }
                if (remove)
                    definition.masks.erase(definition.masks.begin() + static_cast<std::ptrdiff_t>(*remove)), changed = true;
                if (ImGui::Button("Add Mask"))
                    definition.masks.push_back({"Mask " + std::to_string(definition.masks.size() + 1)}), changed = true;
                ImGui::TreePop();
            }

            std::optional<std::size_t> removeBiome;
            for (std::size_t index = 0; index < definition.biomes.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                auto &biome = definition.biomes[index];
                const std::string title = (biome.name.empty() ? "Biome" : biome.name) + "###Biome";
                if (ImGui::CollapsingHeader(title.c_str()))
                {
                    ImGui::Indent();
                    changed |= EditBiome(biome, layers, types, masks);
                    if (ImGui::Button("Remove Biome"))
                        removeBiome = index;
                    ImGui::Unindent();
                }
                ImGui::PopID();
            }
            if (removeBiome)
                definition.biomes.erase(definition.biomes.begin() + static_cast<std::ptrdiff_t>(*removeBiome)), changed = true;
            if (ImGui::Button("Add Biome"))
            {
                procedural::ProceduralBiome biome;
                biome.name = "Biome " + std::to_string(definition.biomes.size() + 1);
                if (!layers.empty())
                    biome.layers.push_back({layers.front(), {}});
                definition.biomes.push_back(std::move(biome));
                changed = true;
            }

            if (changed)
            {
                procedural.SetDefinition(std::move(definition));
                entity.AddPrefabOverride(std::string("Component:") + ComponentName + ":Definition");
                shell.MarkSceneDirty();
            }
        }
    }

    void RegisterProceduralTerrainInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::ProceduralTerrainComponent>(), &DrawProceduralTerrainInspector);
    }
}
