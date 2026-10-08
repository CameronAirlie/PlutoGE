#include "PlutoGE/ui/AssetReferencePicker.h"

#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <unordered_map>

namespace PlutoGE::ui
{
    namespace
    {
        bool StartsWith(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        struct AssetReferenceOptionsCacheEntry
        {
            const assets::Project *project = nullptr;
            const assets::ProjectAssetEntry *assetEntriesData = nullptr;
            std::size_t assetEntryCount = 0;
            std::string firstReference;
            std::string middleReference;
            std::string lastReference;
            std::vector<AssetReferenceOption> options;
        };

        std::vector<AssetReferenceOption> CollectAssetReferenceOptions(const assets::Project *project, assets::ProjectAssetType type)
        {
            std::vector<AssetReferenceOption> options;
            if (!project)
            {
                for (const auto &reference : assets::Project::GetBuiltinAssetReferences())
                {
                    if (assets::Project::GetAssetTypeForReference(reference) != type)
                    {
                        continue;
                    }

                    options.push_back(AssetReferenceOption{.reference = reference, .displayName = reference});
                }
                return options;
            }

            for (const auto &assetEntry : project->GetManifest().assetEntries)
            {
                if (assetEntry.type != type)
                {
                    continue;
                }

                std::string displayName = assetEntry.reference;
                if (StartsWith(displayName, assets::Project::kProjectAssetScheme))
                {
                    displayName.erase(0, assets::Project::kProjectAssetScheme.size());
                }
                else if (StartsWith(displayName, assets::Project::kEngineAssetScheme))
                {
                    displayName.erase(0, assets::Project::kEngineAssetScheme.size());
                }

                options.push_back(AssetReferenceOption{.reference = assetEntry.reference, .displayName = std::move(displayName)});
            }

            std::sort(options.begin(), options.end(),
                      [](const AssetReferenceOption &left, const AssetReferenceOption &right)
                      {
                          return left.displayName < right.displayName;
                      });
            return options;
        }

        bool ContainsInsensitive(std::string_view text, std::string_view query)
        {
            return std::search(text.begin(), text.end(), query.begin(), query.end(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                   }) != text.end();
        }
    }

    const std::vector<AssetReferenceOption> &GetCachedAssetReferenceOptions(const assets::Project *project,
                                                                            assets::ProjectAssetType type)
    {
        static std::array<AssetReferenceOptionsCacheEntry,
                          static_cast<std::size_t>(assets::ProjectAssetType::Count)>
            cacheEntries;

        const auto typeIndex = static_cast<std::size_t>(type);
        if (typeIndex >= cacheEntries.size())
        {
            static const std::vector<AssetReferenceOption> emptyOptions;
            return emptyOptions;
        }
        auto &cacheEntry = cacheEntries[typeIndex];
        if (!project)
        {
            if (cacheEntry.project == nullptr && !cacheEntry.options.empty())
            {
                return cacheEntry.options;
            }

            cacheEntry.options = CollectAssetReferenceOptions(nullptr, type);
            cacheEntry.project = nullptr;
            cacheEntry.assetEntriesData = nullptr;
            cacheEntry.assetEntryCount = 0;
            cacheEntry.firstReference.clear();
            cacheEntry.middleReference.clear();
            cacheEntry.lastReference.clear();
            return cacheEntry.options;
        }

        const auto &assetEntries = project->GetManifest().assetEntries;
        const auto *assetEntriesData = assetEntries.data();
        const std::size_t assetEntryCount = assetEntries.size();
        const std::string_view firstReference = assetEntryCount > 0 ? assetEntries.front().reference : std::string_view{};
        const std::string_view middleReference = assetEntryCount > 0 ? assetEntries[assetEntryCount / 2].reference : std::string_view{};
        const std::string_view lastReference = assetEntryCount > 0 ? assetEntries.back().reference : std::string_view{};

        if (cacheEntry.project == project &&
            cacheEntry.assetEntriesData == assetEntriesData &&
            cacheEntry.assetEntryCount == assetEntryCount &&
            cacheEntry.firstReference == firstReference &&
            cacheEntry.middleReference == middleReference &&
            cacheEntry.lastReference == lastReference)
        {
            return cacheEntry.options;
        }

        cacheEntry.options = CollectAssetReferenceOptions(project, type);
        cacheEntry.project = project;
        cacheEntry.assetEntriesData = assetEntriesData;
        cacheEntry.assetEntryCount = assetEntryCount;
        cacheEntry.firstReference = firstReference;
        cacheEntry.middleReference = middleReference;
        cacheEntry.lastReference = lastReference;
        return cacheEntry.options;
    }

    std::string ToProjectAssetReference(const assets::Project &project, const std::string &pathOrReference)
    {
        if (pathOrReference.empty() || pathOrReference.starts_with("asset://") || assets::Project::IsProjectAssetReference(pathOrReference) ||
            assets::Project::IsEngineAssetReference(pathOrReference))
            return pathOrReference;
        std::filesystem::path path(pathOrReference);
        // Materials persist texture paths relative to the asset directory.
        if (path.is_relative())
            path = project.GetAssetDirectoryPath() / path;
        const auto reference = project.MakeAssetReference(path);
        return assets::Project::IsProjectAssetReference(reference) ? reference : pathOrReference;
    }

    std::string ResolveAssetPickerReference(const assets::AssetCatalog *catalog, const std::string &reference)
    {
        assets::AssetReference identity;
        if (catalog && assets::ParseAssetReference(reference, identity))
            if (const auto *object = catalog->Find(identity)) return object->location;
        return reference;
    }

    std::vector<AssetReferenceOption> CollectProjectAssetChoices(const assets::Project *project,
                                                                 std::span<const assets::ProjectAssetType> types,
                                                                 const ProjectAssetPickerOptions &options)
    {
        std::vector<AssetReferenceOption> assetsOffered;
        const auto offer = [&](const AssetReferenceOption &option) {
            const bool allowed = assets::Project::IsProjectAssetReference(option.reference)
                                     ? project != nullptr
                                     : options.includeEngineAssets && assets::Project::IsEngineAssetReference(option.reference);
            const bool duplicate = std::any_of(assetsOffered.begin(), assetsOffered.end(),
                                               [&](const auto &offered) { return offered.reference == option.reference; });
            if (allowed && !duplicate && (!options.filter || options.filter(option)))
                assetsOffered.push_back(option);
        };
        for (const auto type : types)
        {
            if (project && options.catalog)
                for (const auto &object : options.catalog->GetObjects())
                {
                    if (object.type != type) continue;
                    auto display = object.location;
                    if (display.starts_with(assets::Project::kProjectAssetScheme))
                        display.erase(0, assets::Project::kProjectAssetScheme.size());
                    offer({object.location, std::move(display)});
                }
            for (const auto &option : GetCachedAssetReferenceOptions(project, type))
            {
                if (options.catalog && assets::Project::IsProjectAssetReference(option.reference)) continue;
                offer(option);
            }
            // Built-ins stay available even when the manifest does not list them.
            if (options.includeEngineAssets && project)
                for (const auto &option : GetCachedAssetReferenceOptions(nullptr, type))
                    offer(option);
        }
        std::stable_sort(assetsOffered.begin(), assetsOffered.end(),
                         [](const auto &left, const auto &right) { return left.displayName < right.displayName; });

        std::vector<AssetReferenceOption> choices(options.builtinOptions.begin(), options.builtinOptions.end());
        choices.insert(choices.end(), std::make_move_iterator(assetsOffered.begin()),
                       std::make_move_iterator(assetsOffered.end()));
        return choices;
    }

    std::vector<AssetReferenceOption> CollectProjectAssetChoices(const assets::Project *project, assets::ProjectAssetType type,
                                                                 const ProjectAssetPickerOptions &options)
    {
        return CollectProjectAssetChoices(project, std::span(&type, 1), options);
    }

    bool RenderProjectAssetPicker(const char *label, const assets::Project *project,
                                  std::span<const assets::ProjectAssetType> types, std::string &reference,
                                  const ProjectAssetPickerOptions &options)
    {
        auto effectiveOptions = options;
        // Keep the snapshot alive throughout rendering, including drag/drop.
        const auto snapshot = project && project == EditorShell::GetInstance().GetProject()
            ? core::Engine::GetInstance().GetAssetManager().GetAssetCatalog() : nullptr;
        if (!effectiveOptions.catalog) effectiveOptions.catalog = snapshot.get();
        const auto choices = CollectProjectAssetChoices(project, types, effectiveOptions);
        const auto resolvedReference = ResolveAssetPickerReference(effectiveOptions.catalog, reference);
        const auto current = std::find_if(choices.begin(), choices.end(),
                                          [&](const auto &choice) { return choice.reference == resolvedReference; });
        const bool invalid = !reference.empty() && current == choices.end();
        // Distinguish an existing asset the site's filter rejects (for example
        // the wrong scriptable object class) from one deleted or renamed.
        const bool incompatible = invalid && options.filter &&
            std::ranges::any_of(CollectProjectAssetChoices(project, types, {.includeEngineAssets = options.includeEngineAssets,
                                                                          .catalog = effectiveOptions.catalog}),
                                [&](const auto &choice) { return choice.reference == resolvedReference; });
        const bool missing = invalid && !incompatible &&
            (assets::Project::IsProjectAssetReference(reference) || reference.starts_with("asset://"));
        const char *emptyPreview = options.emptyPreview ? options.emptyPreview
                                   : options.noneLabel  ? options.noneLabel
                                                        : "None";
        const std::string preview = reference.empty()          ? emptyPreview
                                    : current != choices.end() ? current->displayName
                                    : incompatible             ? "Incompatible: " + reference
                                    : missing                  ? "Missing: " + reference
                                                               : "Not a project asset: " + reference;

        bool changed = false;
        const auto choose = [&](const std::string &value) {
            // Preserve an existing stable identity when its location was chosen.
            // Consumers still accept compatibility project paths; persistence
            // converts them through AssetManager at the owning boundary.
            if (value == resolvedReference)
                return;
            reference = value;
            changed = true;
        };
        if (invalid)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.3f, 1.0f));
        const bool open = ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge);
        if (invalid)
        {
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(incompatible ? "This asset is not valid here. Choose a replacement."
                                  : missing    ? "This asset is no longer in the project. Choose a replacement."
                                               : "Only assets inside the project can be used. Choose a replacement.");
        }
        if (open)
        {
            // Keyed by widget ID so each picker keeps its own search text.
            static std::unordered_map<ImGuiID, std::array<char, 96>> searches;
            auto &search = searches[ImGui::GetID(label)];
            if (ImGui::IsWindowAppearing())
            {
                search.fill('\0');
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##Search", "Search", search.data(), search.size());
            if (options.noneLabel && ImGui::Selectable(options.noneLabel, reference.empty()))
                choose({});
            for (const auto &choice : choices)
            {
                if (search[0] != '\0' && !ContainsInsensitive(choice.displayName, search.data()))
                    continue;
                const bool selected = choice.reference == resolvedReference;
                if (ImGui::Selectable(choice.displayName.c_str(), selected))
                    choose(choice.reference);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            if (choices.empty())
                ImGui::TextDisabled(project ? "No matching project assets" : "Open a project to choose assets");
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload);
                payload && payload->Data && payload->DataSize > 0)
            {
                const auto *data = static_cast<const char *>(payload->Data);
                const std::string dropped(data, data + payload->DataSize - 1);
                // Drops follow the same rule as the list.
                const auto resolvedDrop = ResolveAssetPickerReference(effectiveOptions.catalog, dropped);
                if (std::any_of(choices.begin(), choices.end(), [&](const auto &choice) { return choice.reference == resolvedDrop; }))
                    choose(resolvedDrop);
            }
            ImGui::EndDragDropTarget();
        }
        return changed;
    }

    bool RenderProjectAssetPicker(const char *label, const assets::Project *project, assets::ProjectAssetType type,
                                  std::string &reference, const ProjectAssetPickerOptions &options)
    {
        return RenderProjectAssetPicker(label, project, std::span(&type, 1), reference, options);
    }
}
