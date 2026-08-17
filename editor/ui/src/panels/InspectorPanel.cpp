
#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/ActiveRagdollComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/DecalComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <imgui.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::size_t kInspectorPathBufferSize = 512;
        constexpr std::size_t kNewScriptNameBufferSize = 128;
        constexpr int kScriptCoreSearchAncestorLimit = 8;
        constexpr std::string_view kBuiltinExampleNamespace = "PlutoGE.ScriptCore.Examples.";

        bool IsBuiltinExampleScript(std::string_view className)
        {
            return className.rfind(kBuiltinExampleNamespace, 0) == 0;
        }

        bool ContainsInsensitive(std::string_view text, std::string_view query)
        {
            if (query.empty())
            {
                return true;
            }
            return std::search(text.begin(), text.end(), query.begin(), query.end(),
                               [](char left, char right)
                               {
                                   return std::tolower(static_cast<unsigned char>(left)) ==
                                          std::tolower(static_cast<unsigned char>(right));
                               }) != text.end();
        }
        constexpr const char *kPostProcessEffectDragDropPayload = "PGE_PP_FX";
        constexpr const char *kEditorPostProcessEffectDragDropPayload = "PGE_ED_PP_FX";
        constexpr const char *kHierarchyEntityDragDropPayload = "PLUTOGE_SCENE_ENTITY";
        enum class AddableComponentType
        {
            Mesh = 0,
            Terrain = 1,
            Foliage = 2,
            Animation = 3,
            Cloth = 4,
            Camera = 5,
            Light = 6,
            Rigidbody = 7,
            Collider = 8,
            IblCapture = 9,
            PhysicalSky = 10,
            VolumetricCloud = 11,
            ParticleSystem = 12,
            Spline = 13,
            Ocean = 14,
            Script = 15,
            SoundEmitter = 16,
            SoundListener = 17,
            Canvas = 18,
            RectTransform = 19,
            UIImage = 20,
            UIText = 21,
            UIButton = 22,
            NavAgent = 23,
            NavigationMesh = 24,
            Decal = 25,
            RmlWidget = 26,
            LegacyCanvas = 27,
            ActiveRagdoll = 28,
        };

        struct ScriptAssetOption
        {
            std::string reference;
            std::string displayName;
            std::string className;
            std::filesystem::path sourcePath;
            bool classLoaded = false;
            bool engineBuiltin = false;
        };

        struct AssetReferenceOption
        {
            std::string reference;
            std::string displayName;
        };

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

        struct ScriptAssetOptionsCacheEntry
        {
            const assets::Project *project = nullptr;
            const assets::ProjectAssetEntry *assetEntriesData = nullptr;
            std::size_t assetEntryCount = 0;
            std::string firstReference;
            std::string middleReference;
            std::string lastReference;
            std::vector<std::string> loadedClassNames;
            std::vector<ScriptAssetOption> options;
        };

        ScriptAssetOptionsCacheEntry &GetScriptAssetOptionsCache()
        {
            static ScriptAssetOptionsCacheEntry cache;
            return cache;
        }

        void InvalidateScriptAssetOptionsCache()
        {
            GetScriptAssetOptionsCache().project = nullptr;
        }

        struct FoliageSubmeshChoice
        {
            std::string label;
            std::vector<int> indices;
            bool isGroup = false;
        };

        std::string GetFoliageSubmeshGroupName(std::string name)
        {
            const auto trimToken = [](std::string token)
            {
                while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())) != 0)
                {
                    token.erase(token.begin());
                }
                while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())) != 0)
                {
                    token.pop_back();
                }
                return token;
            };

            name = trimToken(std::move(name));
            const auto delimiter = name.find_first_of("_-. ");
            if (delimiter != std::string::npos && delimiter > 0)
            {
                return trimToken(name.substr(0, delimiter));
            }

            while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())) != 0)
            {
                name.pop_back();
            }
            return trimToken(name);
        }

        bool FoliageSubmeshSelectionMatches(const scene::FoliageType &type, const std::vector<int> &indices)
        {
            if (indices.empty())
            {
                return type.submeshIndex < 0 && type.submeshIndices.empty();
            }

            std::vector<int> current = type.submeshIndices;
            if (current.empty() && type.submeshIndex >= 0)
            {
                current.push_back(type.submeshIndex);
            }

            auto sortedCurrent = current;
            auto sortedIndices = indices;
            std::sort(sortedCurrent.begin(), sortedCurrent.end());
            std::sort(sortedIndices.begin(), sortedIndices.end());
            return sortedCurrent == sortedIndices;
        }

        std::vector<FoliageSubmeshChoice> BuildFoliageSubmeshChoices(const render::Mesh &mesh)
        {
            std::vector<FoliageSubmeshChoice> choices;
            std::map<std::string, std::vector<int>> groupedIndices;
            for (std::size_t submeshIndex = 0; submeshIndex < mesh.GetSubmeshCount(); ++submeshIndex)
            {
                const auto &submesh = mesh.GetSubmesh(submeshIndex);
                const std::string groupName = GetFoliageSubmeshGroupName(submesh.name);
                if (!groupName.empty())
                {
                    groupedIndices[groupName].push_back(static_cast<int>(submeshIndex));
                }
            }

            for (const auto &[groupName, indices] : groupedIndices)
            {
                if (indices.size() <= 1)
                {
                    continue;
                }
                choices.push_back(FoliageSubmeshChoice{
                    .label = groupName + " (" + std::to_string(indices.size()) + " submeshes)",
                    .indices = indices,
                    .isGroup = true,
                });
            }

            for (std::size_t submeshIndex = 0; submeshIndex < mesh.GetSubmeshCount(); ++submeshIndex)
            {
                const auto &submesh = mesh.GetSubmesh(submeshIndex);
                choices.push_back(FoliageSubmeshChoice{
                    .label = submesh.name.empty()
                                 ? "Submesh " + std::to_string(submeshIndex)
                                 : submesh.name + " (" + std::to_string(submeshIndex) + ")",
                    .indices = {static_cast<int>(submeshIndex)},
                    .isGroup = false,
                });
            }

            return choices;
        }

        std::string_view TrimWhitespace(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
            {
                text.remove_prefix(1);
            }

            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
            {
                text.remove_suffix(1);
            }

            return text;
        }

        std::string JoinTags(const std::vector<std::string> &tags)
        {
            std::string joined;
            for (const auto &tag : tags)
            {
                if (tag.empty())
                {
                    continue;
                }

                if (!joined.empty())
                {
                    joined += ", ";
                }
                joined += tag;
            }
            return joined;
        }

        std::vector<std::string> ParseTags(std::string_view text)
        {
            std::vector<std::string> tags;
            while (!text.empty())
            {
                const auto comma = text.find(',');
                auto token = TrimWhitespace(text.substr(0, comma));
                if (!token.empty())
                {
                    const std::string tag(token);
                    if (std::find(tags.begin(), tags.end(), tag) == tags.end())
                    {
                        tags.push_back(tag);
                    }
                }

                if (comma == std::string_view::npos)
                {
                    break;
                }
                text.remove_prefix(comma + 1);
            }
            return tags;
        }

        bool StartsWith(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        bool EndsWithInsensitive(std::string_view text, std::string_view suffix)
        {
            if (text.size() < suffix.size())
            {
                return false;
            }

            const auto offset = text.size() - suffix.size();
            for (std::size_t index = 0; index < suffix.size(); ++index)
            {
                const auto left = static_cast<unsigned char>(text[offset + index]);
                const auto right = static_cast<unsigned char>(suffix[index]);
                if (std::tolower(left) != std::tolower(right))
                {
                    return false;
                }
            }

            return true;
        }

        std::string ExtractClassName(std::string_view line)
        {
            const std::size_t classPos = line.find("class ");
            if (classPos == std::string_view::npos)
            {
                return {};
            }

            std::string_view remainder = line.substr(classPos + 6);
            remainder = TrimWhitespace(remainder);

            std::size_t length = 0;
            while (length < remainder.size())
            {
                const unsigned char character = static_cast<unsigned char>(remainder[length]);
                if (!std::isalnum(character) && character != '_')
                {
                    break;
                }

                ++length;
            }

            return std::string(remainder.substr(0, length));
        }

        std::optional<std::string> ParseScriptClassNameFromFile(const std::filesystem::path &filePath)
        {
            std::ifstream input(filePath);
            if (!input.is_open())
            {
                return std::nullopt;
            }

            std::string namespaceName;
            std::string firstClassName;
            std::string scriptClassName;
            std::string line;
            while (std::getline(input, line))
            {
                std::string_view trimmed = TrimWhitespace(line);
                if (trimmed.empty() || StartsWith(trimmed, "//"))
                {
                    continue;
                }

                if (namespaceName.empty() && StartsWith(trimmed, "namespace "))
                {
                    trimmed.remove_prefix(std::string_view("namespace ").size());
                    trimmed = TrimWhitespace(trimmed);
                    const std::size_t delimiterPos = trimmed.find_first_of("{;");
                    namespaceName = std::string(TrimWhitespace(trimmed.substr(0, delimiterPos)));
                }

                if (trimmed.find("class ") == std::string_view::npos)
                {
                    continue;
                }

                const std::string className = ExtractClassName(trimmed);
                if (className.empty())
                {
                    continue;
                }

                if (firstClassName.empty())
                {
                    firstClassName = className;
                }

                if (trimmed.find("ScriptBehaviour") != std::string_view::npos)
                {
                    scriptClassName = className;
                    break;
                }
            }

            const std::string &resolvedClassName = scriptClassName.empty() ? firstClassName : scriptClassName;
            if (resolvedClassName.empty())
            {
                return std::nullopt;
            }

            if (namespaceName.empty())
            {
                return resolvedClassName;
            }

            return namespaceName + "." + resolvedClassName;
        }

        std::filesystem::path FindScriptCoreProjectPath(const std::filesystem::path &searchRoot)
        {
            auto candidateRoot = searchRoot.lexically_normal();
            for (int depth = 0; depth < kScriptCoreSearchAncestorLimit && !candidateRoot.empty(); ++depth)
            {
                const std::array<std::filesystem::path, 2> candidates{
                    candidateRoot / "SDK" / "PlutoGE.ScriptCore" / "PlutoGE.ScriptCore.csproj",
                    candidateRoot / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "PlutoGE.ScriptCore.csproj",
                };
                for (const auto &candidate : candidates)
                {
                    if (std::filesystem::exists(candidate))
                    {
                        return candidate.lexically_normal();
                    }
                }

                const auto parentRoot = candidateRoot.parent_path();
                if (parentRoot.empty() || parentRoot == candidateRoot)
                {
                    break;
                }

                candidateRoot = parentRoot;
            }

            return {};
        }

        std::filesystem::path FindScriptCoreExamplesDirectory()
        {
            const auto scriptCoreProjectPath = FindScriptCoreProjectPath(std::filesystem::current_path());
            if (scriptCoreProjectPath.empty())
            {
                return {};
            }

            const auto examplesDirectory = scriptCoreProjectPath.parent_path() / "Examples";
            return std::filesystem::exists(examplesDirectory) ? examplesDirectory.lexically_normal() : std::filesystem::path{};
        }

        std::string FindLoadedClassForShortName(const std::vector<std::string> &loadedClassNames, std::string_view shortName)
        {
            if (shortName.empty())
            {
                return {};
            }

            std::vector<std::string> suffixMatches;
            suffixMatches.reserve(loadedClassNames.size());
            for (const auto &className : loadedClassNames)
            {
                if (className == shortName)
                {
                    return className;
                }

                if (className.size() > shortName.size() &&
                    className.compare(className.size() - shortName.size(), shortName.size(), shortName) == 0 &&
                    className[className.size() - shortName.size() - 1] == '.')
                {
                    suffixMatches.push_back(className);
                }
            }

            return suffixMatches.size() == 1 ? suffixMatches.front() : std::string{};
        }

        std::string ResolveScriptClassName(const std::filesystem::path &filePath, const std::vector<std::string> &loadedClassNames)
        {
            const auto parsedClassName = ParseScriptClassNameFromFile(filePath);
            if (parsedClassName.has_value())
            {
                if (std::find(loadedClassNames.begin(), loadedClassNames.end(), *parsedClassName) != loadedClassNames.end())
                {
                    return *parsedClassName;
                }

                const auto shortName = filePath.stem().string();
                const auto loadedClassName = FindLoadedClassForShortName(loadedClassNames, shortName);
                if (!loadedClassName.empty())
                {
                    return loadedClassName;
                }

                return *parsedClassName;
            }

            const auto shortName = filePath.stem().string();
            const auto loadedClassName = FindLoadedClassForShortName(loadedClassNames, shortName);
            if (!loadedClassName.empty())
            {
                return loadedClassName;
            }

            return shortName;
        }

        std::vector<ScriptAssetOption> CollectProjectScriptAssetOptions(const assets::Project &project,
                                                                        const std::vector<std::string> &loadedClassNames)
        {
            std::vector<ScriptAssetOption> options;
            for (const auto &assetEntry : project.GetManifest().assetEntries)
            {
                if (!EndsWithInsensitive(assetEntry.reference, ".cs"))
                {
                    continue;
                }

                ScriptAssetOption option;
                option.reference = assetEntry.reference;
                option.displayName = assetEntry.reference;
                if (StartsWith(option.displayName, assets::Project::kProjectAssetScheme))
                {
                    option.displayName.erase(0, assets::Project::kProjectAssetScheme.size());
                }

                const auto assetPath = project.ResolveAssetReference(assetEntry.reference);
                option.sourcePath = assetPath;
                option.className = ResolveScriptClassName(assetPath, loadedClassNames);
                option.classLoaded = !option.className.empty() &&
                                     std::find(loadedClassNames.begin(), loadedClassNames.end(), option.className) != loadedClassNames.end();
                options.push_back(std::move(option));
            }

            std::sort(options.begin(), options.end(),
                      [](const ScriptAssetOption &left, const ScriptAssetOption &right)
                      {
                          return left.displayName < right.displayName;
                      });
            return options;
        }

        std::vector<ScriptAssetOption> CollectBuiltinScriptAssetOptions(const std::vector<std::string> &loadedClassNames)
        {
            std::vector<ScriptAssetOption> options;
            const auto examplesDirectory = FindScriptCoreExamplesDirectory();
            if (examplesDirectory.empty())
            {
                return options;
            }

            std::error_code errorCode;
            for (const auto &entry : std::filesystem::directory_iterator(examplesDirectory, errorCode))
            {
                if (errorCode || !entry.is_regular_file() || !EndsWithInsensitive(entry.path().filename().string(), ".cs"))
                {
                    continue;
                }

                ScriptAssetOption option;
                option.sourcePath = entry.path().lexically_normal();
                const auto filename = entry.path().filename().string();
                option.reference = "engine://builtin/script/examples/" + filename;
                option.displayName = "Examples/" + filename;
                option.className = ResolveScriptClassName(option.sourcePath, loadedClassNames);
                option.classLoaded = !option.className.empty() &&
                                     std::find(loadedClassNames.begin(), loadedClassNames.end(), option.className) != loadedClassNames.end();
                option.engineBuiltin = true;
                if (IsBuiltinExampleScript(option.className))
                {
                    options.push_back(std::move(option));
                }
            }

            std::sort(options.begin(), options.end(),
                      [](const ScriptAssetOption &left, const ScriptAssetOption &right)
                      {
                          return left.displayName < right.displayName;
                      });
            return options;
        }

        const std::vector<ScriptAssetOption> &GetCachedProjectScriptAssetOptions(
            const assets::Project &project,
            const std::vector<std::string> &loadedClassNames)
        {
            auto &cache = GetScriptAssetOptionsCache();
            const auto &assetEntries = project.GetManifest().assetEntries;
            const auto *assetEntriesData = assetEntries.data();
            const std::size_t assetEntryCount = assetEntries.size();
            const std::string_view firstReference = assetEntryCount > 0 ? assetEntries.front().reference : std::string_view{};
            const std::string_view middleReference = assetEntryCount > 0 ? assetEntries[assetEntryCount / 2].reference : std::string_view{};
            const std::string_view lastReference = assetEntryCount > 0 ? assetEntries.back().reference : std::string_view{};

            if (cache.project == &project &&
                cache.assetEntriesData == assetEntriesData &&
                cache.assetEntryCount == assetEntryCount &&
                cache.firstReference == firstReference &&
                cache.middleReference == middleReference &&
                cache.lastReference == lastReference &&
                cache.loadedClassNames == loadedClassNames)
            {
                return cache.options;
            }

            cache.options = CollectProjectScriptAssetOptions(project, loadedClassNames);
            cache.project = &project;
            cache.assetEntriesData = assetEntriesData;
            cache.assetEntryCount = assetEntryCount;
            cache.firstReference = firstReference;
            cache.middleReference = middleReference;
            cache.lastReference = lastReference;
            cache.loadedClassNames = loadedClassNames;
            return cache.options;
        }

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

        const std::vector<AssetReferenceOption> &GetCachedAssetReferenceOptions(const assets::Project *project,
                                                                                assets::ProjectAssetType type)
        {
            static std::array<AssetReferenceOptionsCacheEntry,
                              static_cast<std::size_t>(assets::ProjectAssetType::RmlDocument) + 1>
                cacheEntries;

            auto &cacheEntry = cacheEntries[static_cast<std::size_t>(type)];
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

        std::string GetAssetReferencePreview(const std::vector<AssetReferenceOption> &options,
                                             const std::string &reference,
                                             std::string fallback)
        {
            if (reference.empty())
            {
                return fallback;
            }

            for (const auto &option : options)
            {
                if (option.reference == reference)
                {
                    return option.displayName;
                }
            }

            return reference;
        }

        std::optional<std::string> AcceptDroppedMaterialAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Material)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            return droppedReference;
        }

        std::optional<std::string> AcceptDroppedPrefabAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Prefab)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            return droppedReference;
        }

        std::optional<std::string> AcceptDroppedScriptableObjectReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::ScriptableObject)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            return droppedReference;
        }

        std::string ReadScriptableObjectType(const assets::Project *project, const std::string &reference)
        {
            if (!project)
            {
                return {};
            }
            std::ifstream input(project->ResolveAssetReference(reference));
            std::string header;
            std::getline(input, header);
            constexpr std::string_view prefix = "SCRIPTABLE\t";
            return header.rfind(prefix, 0) == 0 ? header.substr(prefix.size()) : std::string{};
        }

        std::optional<std::string> AcceptDroppedMeshAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Mesh)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            return droppedReference;
        }

        std::optional<std::string> AcceptDroppedAnimationAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        const auto assetType = assets::Project::GetAssetTypeForReference(reference);
                        if (assetType == assets::ProjectAssetType::Animation ||
                            assetType == assets::ProjectAssetType::AnimationClip)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            return droppedReference;
        }

        std::optional<std::string> AcceptDroppedAnimationGraphAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::AnimationGraph)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            return droppedReference;
        }

        std::optional<std::string> AcceptDroppedParticleSystemAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::ParticleSystem)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            return droppedReference;
        }

        std::optional<std::string> AcceptDroppedTextureAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Texture)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            return droppedReference;
        }

        std::optional<std::string> AcceptDroppedAudioAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Audio)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            return droppedReference;
        }

        bool AssignMaterialAssetToSlot(scene::MeshComponent &meshComponent,
                                       size_t materialSlotIndex,
                                       const std::string &materialAssetReference,
                                       core::Engine &engine)
        {
            if (assets::Project::GetAssetTypeForReference(materialAssetReference) != assets::ProjectAssetType::Material)
            {
                return false;
            }

            auto *materialAsset = engine.GetAssetManager().LoadMaterialAsset(materialAssetReference);
            if (!materialAsset)
            {
                return false;
            }

            if (meshComponent.GetSubmeshIndex() >= 0)
            {
                const auto submeshIndex = static_cast<std::size_t>(meshComponent.GetSubmeshIndex());
                meshComponent.SetMaterialForSubmesh(submeshIndex, materialAsset);
                meshComponent.SetMaterialAssetForSubmesh(submeshIndex, materialAssetReference);
            }
            else
            {
                meshComponent.SetMaterialForMaterialSlot(materialSlotIndex, materialAsset);
                meshComponent.SetMaterialAssetForMaterialSlot(materialSlotIndex, materialAssetReference);
            }
            return true;
        }

        bool AssignMeshAsset(scene::MeshComponent &meshComponent,
                             const std::string &meshAssetReference,
                             core::Engine &engine)
        {
            if (assets::Project::GetAssetTypeForReference(meshAssetReference) != assets::ProjectAssetType::Mesh)
            {
                return false;
            }

            auto *mesh = engine.GetAssetManager().LoadMeshAsset(meshAssetReference);
            if (!mesh)
            {
                return false;
            }

            meshComponent.SetMesh(mesh);
            meshComponent.SetMeshAssetReference(meshAssetReference);
            if (assets::Project::IsEngineAssetReference(meshAssetReference))
            {
                meshComponent.SetModelObjectIdentity(meshAssetReference, 1);
            }
            else
            {
                const auto &metadata = engine.GetAssetManager().GetMeshAssetMetadata(meshAssetReference);
                if (metadata.sourceAssetId.empty() || metadata.sourceObjectId == 0) return false;
                meshComponent.SetModelObjectIdentity(metadata.sourceAssetId, metadata.sourceObjectId);
            }
            meshComponent.SetUseGeneratedLods(false);

            const auto &materialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(meshAssetReference);
            std::vector<render::Material *> loadedMaterials;
            loadedMaterials.reserve((std::max<std::size_t>)(materialReferences.size(), 1));
            for (const auto &materialReference : materialReferences)
            {
                loadedMaterials.push_back(engine.GetAssetManager().LoadMaterialAsset(materialReference));
            }
            if (loadedMaterials.empty())
            {
                loadedMaterials.push_back(engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
            }
            meshComponent.SetMaterials(loadedMaterials);
            for (size_t materialSlotIndex = 0; materialSlotIndex < materialReferences.size(); ++materialSlotIndex)
            {
                meshComponent.SetMaterialAssetForMaterialSlot(materialSlotIndex, materialReferences[materialSlotIndex]);
            }
            return true;
        }

        bool AssignAnimationAsset(scene::AnimationComponent &animationComponent,
                                  const std::string &animationAssetReference)
        {
            const auto assetType = assets::Project::GetAssetTypeForReference(animationAssetReference);
            if (assetType != assets::ProjectAssetType::Animation &&
                assetType != assets::ProjectAssetType::AnimationClip)
            {
                return false;
            }

            return animationComponent.SetAnimationAssetReference(animationAssetReference);
        }

        bool AssignAnimationGraphAsset(scene::AnimationComponent &animationComponent,
                                       const std::string &animationGraphAssetReference)
        {
            if (assets::Project::GetAssetTypeForReference(animationGraphAssetReference) != assets::ProjectAssetType::AnimationGraph)
            {
                return false;
            }

            return animationComponent.SetAnimationGraphAssetReference(animationGraphAssetReference);
        }

        bool AssignMaterialAssetToSubmesh(scene::MeshComponent &meshComponent,
                                          size_t submeshIndex,
                                          const std::string &materialAssetReference,
                                          core::Engine &engine)
        {
            if (assets::Project::GetAssetTypeForReference(materialAssetReference) != assets::ProjectAssetType::Material)
            {
                return false;
            }

            auto *materialAsset = engine.GetAssetManager().LoadMaterialAsset(materialAssetReference);
            if (!materialAsset)
            {
                return false;
            }

            meshComponent.SetMaterialForSubmesh(submeshIndex, materialAsset);
            meshComponent.SetMaterialAssetForSubmesh(submeshIndex, materialAssetReference);
            return true;
        }

        bool AssignMaterialAssetToTerrain(scene::TerrainComponent &terrainComponent,
                                          const std::string &materialAssetReference,
                                          core::Engine &engine)
        {
            if (assets::Project::GetAssetTypeForReference(materialAssetReference) != assets::ProjectAssetType::Material)
            {
                return false;
            }

            auto *materialAsset = engine.GetAssetManager().LoadMaterialAsset(materialAssetReference);
            if (!materialAsset)
            {
                return false;
            }

            terrainComponent.SetMaterial(materialAsset);
            terrainComponent.SetMaterialAssetReference(materialAssetReference);
            return true;
        }

        bool AssignMaterialAssetToSpline(scene::SplineComponent &splineComponent,
                                         const std::string &materialAssetReference,
                                         core::Engine &engine)
        {
            if (assets::Project::GetAssetTypeForReference(materialAssetReference) != assets::ProjectAssetType::Material)
            {
                return false;
            }

            auto *materialAsset = engine.GetAssetManager().LoadMaterialAsset(materialAssetReference);
            if (!materialAsset)
            {
                return false;
            }

            splineComponent.SetMaterial(materialAsset);
            splineComponent.SetMaterialAssetReference(materialAssetReference);
            return true;
        }

        std::vector<std::string> BuildMaterialSlotUsageSummaries(const scene::MeshComponent &meshComponent)
        {
            struct Usage
            {
                std::size_t count = 0;
                std::string examples;
            };

            std::vector<Usage> usage((std::max)(meshComponent.GetMaterials().size(), static_cast<std::size_t>(1)));
            const auto *mesh = meshComponent.GetMesh();
            if (mesh)
            {
                for (std::size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshCount(); ++submeshIndex)
                {
                    const auto &submesh = mesh->GetSubmesh(submeshIndex);
                    const auto slotIndex = static_cast<std::size_t>(submesh.materialIndex);
                    if (slotIndex >= usage.size())
                    {
                        usage.resize(slotIndex + 1);
                    }
                    auto &slotUsage = usage[slotIndex];
                    if (slotUsage.count < 3)
                    {
                        if (!slotUsage.examples.empty())
                        {
                            slotUsage.examples += ", ";
                        }
                        slotUsage.examples += submesh.name.empty() ? "Submesh " + std::to_string(submeshIndex) : submesh.name;
                    }
                    ++slotUsage.count;
                }
            }

            std::vector<std::string> summaries;
            summaries.reserve(usage.size());
            for (const auto &slotUsage : usage)
            {
                if (slotUsage.count == 0)
                {
                    summaries.emplace_back("Unused by this mesh");
                    continue;
                }
                std::string examples = slotUsage.examples;
                if (slotUsage.count > 3)
                {
                    examples += ", +" + std::to_string(slotUsage.count - 3) + " more";
                }
                summaries.push_back("Used by " + std::to_string(slotUsage.count) + " submesh" +
                                    (slotUsage.count == 1 ? ": " : "es: ") + examples);
            }
            return summaries;
        }

        const ScriptAssetOption *FindScriptAssetOptionForClassName(const std::vector<ScriptAssetOption> &options,
                                                                   std::string_view className)
        {
            for (const auto &option : options)
            {
                if (option.className == className)
                {
                    return &option;
                }
            }

            return nullptr;
        }

        std::string EscapeRegexText(std::string_view text)
        {
            std::string escaped;
            escaped.reserve(text.size() * 2);
            for (char character : text)
            {
                if (std::string_view(R"(\.^$|()[]{}*+?)").find(character) != std::string_view::npos)
                {
                    escaped.push_back('\\');
                }
                escaped.push_back(character);
            }
            return escaped;
        }

        std::string EscapeCSharpStringLiteral(std::string_view text)
        {
            std::string escaped = "\"";
            for (char character : text)
            {
                switch (character)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '"':
                    escaped += "\\\"";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped.push_back(character);
                    break;
                }
            }
            escaped += '"';
            return escaped;
        }

        std::string FormatFloatLiteral(float value)
        {
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream.precision(9);
            stream << value;
            std::string literal = stream.str();
            if (literal.find_first_of(".eE") == std::string::npos)
            {
                literal += ".0";
            }
            literal += "f";
            return literal;
        }

        std::string FormatDoubleLiteral(double value)
        {
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream.precision(17);
            stream << value;
            std::string literal = stream.str();
            if (literal.find_first_of(".eE") == std::string::npos)
            {
                literal += ".0";
            }
            return literal;
        }

        std::optional<std::string> ToCSharpDefaultLiteral(scripting::ScriptFieldType fieldType,
                                                          const scripting::ScriptFieldValue &value)
        {
            switch (fieldType)
            {
            case scripting::ScriptFieldType::Boolean:
                return std::get<bool>(value) ? "true" : "false";
            case scripting::ScriptFieldType::Int32:
                return std::to_string(std::get<int32_t>(value));
            case scripting::ScriptFieldType::Float:
                return FormatFloatLiteral(std::get<float>(value));
            case scripting::ScriptFieldType::Double:
                return FormatDoubleLiteral(std::get<double>(value));
            case scripting::ScriptFieldType::String:
            case scripting::ScriptFieldType::PrefabAsset:
            case scripting::ScriptFieldType::ScriptableObjectAsset:
            case scripting::ScriptFieldType::MaterialAsset:
                return EscapeCSharpStringLiteral(std::get<std::string>(value));
            case scripting::ScriptFieldType::Vector2:
            {
                const auto vector = std::get<glm::vec2>(value);
                return "new Vector2(" + FormatFloatLiteral(vector.x) + ", " + FormatFloatLiteral(vector.y) + ")";
            }
            case scripting::ScriptFieldType::Vector3:
            {
                const auto vector = std::get<glm::vec3>(value);
                return "new Vector3(" + FormatFloatLiteral(vector.x) + ", " + FormatFloatLiteral(vector.y) + ", " + FormatFloatLiteral(vector.z) + ")";
            }
            case scripting::ScriptFieldType::EntityId:
                return std::to_string(std::get<uint32_t>(value)) + "u";
            case scripting::ScriptFieldType::GameObject:
            case scripting::ScriptFieldType::MeshComponent:
            case scripting::ScriptFieldType::CameraComponent:
            case scripting::ScriptFieldType::LightComponent:
            case scripting::ScriptFieldType::RigidbodyComponent:
            case scripting::ScriptFieldType::ColliderComponent:
            case scripting::ScriptFieldType::AnimationComponent:
            case scripting::ScriptFieldType::CanvasComponent:
            case scripting::ScriptFieldType::RectTransformComponent:
            case scripting::ScriptFieldType::UIImageComponent:
            case scripting::ScriptFieldType::UITextComponent:
            case scripting::ScriptFieldType::UIButtonComponent:
            case scripting::ScriptFieldType::ParticleSystemComponent:
            case scripting::ScriptFieldType::SoundEmitterComponent:
                return std::get<uint32_t>(value) == 0 ? std::optional<std::string>{"null"} : std::nullopt;
            case scripting::ScriptFieldType::None:
            default:
                return std::nullopt;
            }
        }

        struct SaveScriptDefaultsResult
        {
            int updated = 0;
            int skipped = 0;
            std::string error;
        };

        SaveScriptDefaultsResult SaveScriptDefaultsToSource(
            const std::filesystem::path &sourcePath,
            const std::vector<scripting::ScriptFieldDefinition> &fields,
            const std::unordered_map<std::string, scripting::ScriptFieldValue> &fieldValues)
        {
            SaveScriptDefaultsResult result;
            if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
            {
                result.error = "Could not find the script source file.";
                return result;
            }

            std::ifstream input(sourcePath);
            if (!input.is_open())
            {
                result.error = "Could not read the script source file.";
                return result;
            }

            std::ostringstream sourceStream;
            sourceStream << input.rdbuf();
            std::string source = sourceStream.str();
            std::string updatedSource = source;

            for (const auto &field : fields)
            {
                scripting::ScriptFieldValue fieldValue = scripting::IsFieldValueCompatible(field.type, field.defaultValue)
                                                             ? field.defaultValue
                                                             : scripting::MakeDefaultFieldValue(field.type);
                if (const auto iterator = fieldValues.find(field.name); iterator != fieldValues.end())
                {
                    fieldValue = iterator->second;
                }
                if (!scripting::IsFieldValueCompatible(field.type, fieldValue))
                {
                    ++result.skipped;
                    continue;
                }

                const auto literal = ToCSharpDefaultLiteral(field.type, fieldValue);
                if (!literal)
                {
                    ++result.skipped;
                    continue;
                }

                const std::regex fieldPattern(
                    "(\\[SerializedField\\][^;]*?\\b" + EscapeRegexText(field.name) + "\\b[^;=]*)(\\s*=\\s*[^;]*)?(;)");
                const std::string replacement = "$1 = " + *literal + "$3";
                const std::string nextSource = std::regex_replace(
                    updatedSource,
                    fieldPattern,
                    replacement,
                    std::regex_constants::format_first_only);
                if (nextSource == updatedSource)
                {
                    ++result.skipped;
                    continue;
                }

                updatedSource = nextSource;
                ++result.updated;
            }

            if (updatedSource == source)
            {
                return result;
            }

            std::ofstream output(sourcePath, std::ios::trunc);
            if (!output.is_open())
            {
                result.error = "Could not write the script source file.";
                return result;
            }

            output << updatedSource;
            return result;
        }

        std::string SanitizeScriptIdentifier(std::string_view text)
        {
            std::string identifier;
            identifier.reserve(text.size());

            for (const char rawCharacter : text)
            {
                const unsigned char character = static_cast<unsigned char>(rawCharacter);
                if (std::isalnum(character) != 0 || rawCharacter == '_')
                {
                    if (identifier.empty() && std::isdigit(character) != 0)
                    {
                        identifier.push_back('_');
                    }

                    identifier.push_back(rawCharacter);
                    continue;
                }

                if (!identifier.empty() && identifier.back() != '_')
                {
                    identifier.push_back('_');
                }
            }

            while (!identifier.empty() && identifier.back() == '_')
            {
                identifier.pop_back();
            }

            return identifier;
        }

        glm::vec3 ParseVec3Property(const std::string &value)
        {
            glm::vec3 parsedValue{0.0f};
            std::sscanf(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }

        std::array<char, kInspectorPathBufferSize> &GetLightmapPathBuffer(const scene::Entity &entity, uint32_t materialSlot)
        {
            static std::unordered_map<std::string, std::array<char, kInspectorPathBufferSize>> lightmapPathBuffers;
            const std::string key = std::to_string(entity.GetID()) + ":" + std::to_string(materialSlot);
            return lightmapPathBuffers[key];
        }

        std::array<char, kInspectorPathBufferSize> &GetTerrainHeightMapPathBuffer(const scene::Entity &entity)
        {
            static std::unordered_map<scene::EntityID, std::array<char, kInspectorPathBufferSize>> heightMapPathBuffers;
            return heightMapPathBuffers[entity.GetID()];
        }

        std::array<char, 128> &GetFoliageTypeNameBuffer(scene::EntityID entityId, std::size_t typeIndex)
        {
            static std::unordered_map<std::string, std::array<char, 128>> nameBuffers;
            return nameBuffers[std::to_string(entityId) + ":" + std::to_string(typeIndex)];
        }

        const char *GetComponentDisplayName(const scene::Component &component)
        {
            if (dynamic_cast<const scene::MeshComponent *>(&component))
            {
                return "Mesh Component";
            }
            if (dynamic_cast<const scene::DecalComponent *>(&component))
                return "Decal Component";
            if (dynamic_cast<const scene::TerrainComponent *>(&component))
            {
                return "Terrain Component";
            }
            if (dynamic_cast<const scene::FoliageComponent *>(&component))
            {
                return "Foliage Component";
            }
            if (dynamic_cast<const scene::ClothComponent *>(&component))
            {
                return "Cloth Component";
            }
            if (dynamic_cast<const scene::ParticleSystemComponent *>(&component))
            {
                return "Particle System Component";
            }
            if (dynamic_cast<const scene::SplineComponent *>(&component))
            {
                return "Spline Track Component";
            }
            if (dynamic_cast<const scene::OceanComponent *>(&component))
            {
                return "Ocean Component";
            }
            if (dynamic_cast<const scene::AnimationComponent *>(&component))
            {
                return "Animation Component";
            }
            if (dynamic_cast<const scene::ActiveRagdollComponent *>(&component))
            {
                return "Active Ragdoll Component";
            }
            if (dynamic_cast<const scene::SkeletonAttachmentComponent *>(&component))
            {
                return "Skeleton Attachment Component";
            }
            if (dynamic_cast<const scene::CameraComponent *>(&component))
            {
                return "Camera Component";
            }
            if (dynamic_cast<const scene::LightComponent *>(&component))
            {
                return "Light Component";
            }
            if (dynamic_cast<const scene::RigidbodyComponent *>(&component))
            {
                return "Rigidbody Component";
            }
            if (dynamic_cast<const scene::NavAgentComponent *>(&component))
                return "Navigation Agent Component";
            if (dynamic_cast<const scene::NavigationMeshComponent *>(&component))
                return "Navigation Mesh Component";
            if (dynamic_cast<const scene::ColliderComponent *>(&component))
            {
                return "Collider Component";
            }
            if (dynamic_cast<const scene::IblCaptureComponent *>(&component))
            {
                return "IBL Capture Component";
            }
            if (dynamic_cast<const scene::VolumetricCloudComponent *>(&component))
            {
                return "Volumetric Cloud Component";
            }
            if (dynamic_cast<const scene::PhysicalSkyComponent *>(&component))
            {
                return "Physical Sky Component";
            }
            if (dynamic_cast<const scene::ScriptComponent *>(&component))
            {
                return "Script Component";
            }
            if (dynamic_cast<const scene::SoundEmitterComponent *>(&component))
            {
                return "Sound Emitter Component";
            }
            if (dynamic_cast<const scene::SoundListenerComponent *>(&component))
            {
                return "Sound Listener Component";
            }
            if (const auto *canvas = dynamic_cast<const scene::CanvasComponent *>(&component))
            {
                return canvas->GetBackend() == scene::UIRenderBackend::RmlUi
                           ? "RmlUi Canvas Component"
                           : "Legacy Native Canvas Component";
            }
            if (dynamic_cast<const scene::RmlWidgetComponent *>(&component))
                return "RML Widget Component (Deprecated)";
            if (dynamic_cast<const scene::RectTransformComponent *>(&component))
            {
                return "Rect Transform Component";
            }
            if (dynamic_cast<const scene::UIImageComponent *>(&component))
            {
                return "UI Image Component";
            }
            if (dynamic_cast<const scene::UITextComponent *>(&component))
            {
                return "UI Text Component";
            }
            if (dynamic_cast<const scene::UIButtonComponent *>(&component))
            {
                return "UI Button Component";
            }

            return typeid(component).name();
        }

        std::string GetComponentPrefabTypeName(const scene::Component &component)
        {
            if (dynamic_cast<const scene::MeshComponent *>(&component))
                return "MeshComponent";
            if (dynamic_cast<const scene::DecalComponent *>(&component))
                return "DecalComponent";
            if (dynamic_cast<const scene::TerrainComponent *>(&component))
                return "TerrainComponent";
            if (dynamic_cast<const scene::FoliageComponent *>(&component))
                return "FoliageComponent";
            if (dynamic_cast<const scene::ClothComponent *>(&component))
                return "ClothComponent";
            if (dynamic_cast<const scene::ParticleSystemComponent *>(&component))
                return "ParticleSystemComponent";
            if (dynamic_cast<const scene::AnimationComponent *>(&component))
                return "AnimationComponent";
            if (dynamic_cast<const scene::ActiveRagdollComponent *>(&component))
                return "ActiveRagdollComponent";
            if (dynamic_cast<const scene::SkeletonAttachmentComponent *>(&component))
                return "SkeletonAttachmentComponent";
            if (dynamic_cast<const scene::CameraComponent *>(&component))
                return "CameraComponent";
            if (dynamic_cast<const scene::LightComponent *>(&component))
                return "LightComponent";
            if (dynamic_cast<const scene::RigidbodyComponent *>(&component))
                return "RigidbodyComponent";
            if (dynamic_cast<const scene::NavAgentComponent *>(&component))
                return "NavAgentComponent";
            if (dynamic_cast<const scene::NavigationMeshComponent *>(&component))
                return "NavigationMeshComponent";
            if (dynamic_cast<const scene::ColliderComponent *>(&component))
                return "ColliderComponent";
            if (dynamic_cast<const scene::IblCaptureComponent *>(&component))
                return "IblCaptureComponent";
            if (dynamic_cast<const scene::VolumetricCloudComponent *>(&component))
                return "VolumetricCloudComponent";
            if (dynamic_cast<const scene::PhysicalSkyComponent *>(&component))
                return "PhysicalSkyComponent";
            if (dynamic_cast<const scene::ScriptComponent *>(&component))
                return "ScriptComponent";
            if (dynamic_cast<const scene::SoundEmitterComponent *>(&component))
                return "SoundEmitterComponent";
            if (dynamic_cast<const scene::SoundListenerComponent *>(&component))
                return "SoundListenerComponent";
            if (dynamic_cast<const scene::CanvasComponent *>(&component))
                return "CanvasComponent";
            if (dynamic_cast<const scene::RmlWidgetComponent *>(&component))
                return "RmlWidgetComponent";
            if (dynamic_cast<const scene::RectTransformComponent *>(&component))
                return "RectTransformComponent";
            if (dynamic_cast<const scene::UIImageComponent *>(&component))
                return "UIImageComponent";
            if (dynamic_cast<const scene::UITextComponent *>(&component))
                return "UITextComponent";
            if (dynamic_cast<const scene::UIButtonComponent *>(&component))
                return "UIButtonComponent";
            if (dynamic_cast<const scene::SplineComponent *>(&component))
                return "SplineComponent";
            if (dynamic_cast<const scene::OceanComponent *>(&component))
                return "OceanComponent";
            return {};
        }

        void CollectEntitiesRecursive(scene::Entity *entity, std::vector<scene::Entity *> &entities)
        {
            if (!entity)
            {
                return;
            }

            entities.push_back(entity);
            for (auto *child : entity->GetChildren())
            {
                CollectEntitiesRecursive(child, entities);
            }
        }

        bool SceneHasAnyCamera(const scene::Scene *scene)
        {
            if (!scene)
            {
                return false;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            for (auto *candidate : entities)
            {
                if (candidate && candidate->GetComponent<scene::CameraComponent>())
                {
                    return true;
                }
            }

            return false;
        }

        void SetSceneMainCamera(scene::Scene *scene, scene::CameraComponent *selectedCamera, bool isMainCamera)
        {
            if (!selectedCamera)
            {
                return;
            }

            if (!scene || !isMainCamera)
            {
                selectedCamera->SetMainCamera(isMainCamera);
                return;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            for (auto *entity : entities)
            {
                if (!entity)
                {
                    continue;
                }

                if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>())
                {
                    cameraComponent->SetMainCamera(cameraComponent == selectedCamera);
                }
            }
        }

        bool CanAddComponentType(const scene::Entity &entity, AddableComponentType componentType)
        {
            switch (componentType)
            {
            case AddableComponentType::Mesh:
                return !entity.HasComponent<scene::MeshComponent>();
            case AddableComponentType::Decal:
                return !entity.HasComponent<scene::DecalComponent>();
            case AddableComponentType::Terrain:
                return !entity.HasComponent<scene::TerrainComponent>();
            case AddableComponentType::Foliage:
                return !entity.HasComponent<scene::FoliageComponent>();
            case AddableComponentType::Cloth:
                return !entity.HasComponent<scene::ClothComponent>();
            case AddableComponentType::ParticleSystem:
                return !entity.HasComponent<scene::ParticleSystemComponent>();
            case AddableComponentType::Spline:
                return !entity.HasComponent<scene::SplineComponent>();
            case AddableComponentType::Ocean:
                return !entity.HasComponent<scene::OceanComponent>();
            case AddableComponentType::Animation:
                return !entity.HasComponent<scene::AnimationComponent>();
            case AddableComponentType::ActiveRagdoll:
                return entity.HasComponent<scene::AnimationComponent>() &&
                       !entity.HasComponent<scene::ActiveRagdollComponent>();
            case AddableComponentType::Camera:
                return !entity.HasComponent<scene::CameraComponent>();
            case AddableComponentType::Light:
                return !entity.HasComponent<scene::LightComponent>();
            case AddableComponentType::Rigidbody:
                return !entity.HasComponent<scene::RigidbodyComponent>();
            case AddableComponentType::NavAgent:
                return !entity.HasComponent<scene::NavAgentComponent>();
            case AddableComponentType::NavigationMesh:
                return !entity.HasComponent<scene::NavigationMeshComponent>();
            case AddableComponentType::Collider:
                return !entity.HasComponent<scene::ColliderComponent>();
            case AddableComponentType::IblCapture:
                return !entity.HasComponent<scene::IblCaptureComponent>();
            case AddableComponentType::PhysicalSky:
                return !entity.HasComponent<scene::PhysicalSkyComponent>();
            case AddableComponentType::VolumetricCloud:
                return !entity.HasComponent<scene::VolumetricCloudComponent>();
            case AddableComponentType::Script:
                return true;
            case AddableComponentType::SoundEmitter:
                return !entity.HasComponent<scene::SoundEmitterComponent>();
            case AddableComponentType::SoundListener:
                return !entity.HasComponent<scene::SoundListenerComponent>();
            case AddableComponentType::Canvas:
            case AddableComponentType::LegacyCanvas:
                return !entity.HasComponent<scene::CanvasComponent>();
            case AddableComponentType::RmlWidget:
                return !entity.HasComponent<scene::RmlWidgetComponent>();
            case AddableComponentType::RectTransform:
                return !entity.HasComponent<scene::RectTransformComponent>();
            case AddableComponentType::UIImage:
                return !entity.HasComponent<scene::UIImageComponent>();
            case AddableComponentType::UIText:
                return !entity.HasComponent<scene::UITextComponent>();
            case AddableComponentType::UIButton:
                return !entity.HasComponent<scene::UIButtonComponent>();
            default:
                return false;
            }
        }

        std::optional<AddableComponentType> RenderAddComponentMenu(const scene::Entity &entity)
        {
            std::optional<AddableComponentType> selectedType;
            const auto renderItem = [&](const char *label, AddableComponentType type)
            {
                if (ImGui::MenuItem(label, nullptr, false, CanAddComponentType(entity, type)))
                {
                    selectedType = type;
                }
            };

            if (ImGui::BeginMenu("Rendering"))
            {
                renderItem("Mesh", AddableComponentType::Mesh);
                renderItem("Decal", AddableComponentType::Decal);
                renderItem("Terrain", AddableComponentType::Terrain);
                renderItem("Spline Track", AddableComponentType::Spline);
                renderItem("Ocean", AddableComponentType::Ocean);
                renderItem("Foliage", AddableComponentType::Foliage);
                renderItem("Cloth", AddableComponentType::Cloth);
                renderItem("Particle System", AddableComponentType::ParticleSystem);
                renderItem("Camera", AddableComponentType::Camera);
                renderItem("Light", AddableComponentType::Light);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Animation"))
            {
                renderItem("Animation", AddableComponentType::Animation);
                renderItem("Active Ragdoll", AddableComponentType::ActiveRagdoll);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Physics"))
            {
                renderItem("Rigidbody", AddableComponentType::Rigidbody);
                renderItem("Collider", AddableComponentType::Collider);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("AI"))
            {
                renderItem("Navigation Agent", AddableComponentType::NavAgent);
                renderItem("Navigation Mesh", AddableComponentType::NavigationMesh);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Environment"))
            {
                renderItem("IBL Capture", AddableComponentType::IblCapture);
                renderItem("Physical Sky", AddableComponentType::PhysicalSky);
                renderItem("Volumetric Cloud", AddableComponentType::VolumetricCloud);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Scripting"))
            {
                renderItem("Script", AddableComponentType::Script);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Audio"))
            {
                renderItem("Sound Emitter", AddableComponentType::SoundEmitter);
                renderItem("Sound Listener", AddableComponentType::SoundListener);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("UI"))
            {
                renderItem("RmlUi Canvas", AddableComponentType::Canvas);
                if (ImGui::BeginMenu("Legacy Native UI"))
                {
                    renderItem("Native Canvas", AddableComponentType::LegacyCanvas);
                    renderItem("Rect Transform", AddableComponentType::RectTransform);
                    renderItem("Image", AddableComponentType::UIImage);
                    renderItem("Text", AddableComponentType::UIText);
                    renderItem("Button", AddableComponentType::UIButton);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            return selectedType;
        }

        void AddComponentToEntity(scene::Entity &entity, AddableComponentType componentType)
        {
            auto &engine = core::Engine::GetInstance();
            switch (componentType)
            {
            case AddableComponentType::Mesh:
            {
                auto *meshComponent = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                    .mesh = nullptr,
                    .material = engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                });
                if (meshComponent)
                {
                    meshComponent->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                }
                break;
            }
            case AddableComponentType::Decal:
                entity.CreateComponent<scene::DecalComponent>();
                break;
            case AddableComponentType::Terrain:
            {
                auto *terrainComponent = entity.CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{
                    .material = engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                });
                if (terrainComponent)
                {
                    terrainComponent->SetMaterialAssetReference(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                }
                break;
            }
            case AddableComponentType::Foliage:
            {
                auto *foliageComponent = entity.CreateComponent<scene::FoliageComponent>();
                if (foliageComponent)
                {
                    foliageComponent->SetMaterialAssetReference(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                }
                break;
            }
            case AddableComponentType::Cloth:
                entity.CreateComponent<scene::ClothComponent>();
                break;
            case AddableComponentType::ParticleSystem:
                entity.CreateComponent<scene::ParticleSystemComponent>();
                break;
            case AddableComponentType::Spline:
            {
                auto *splineComponent = entity.CreateComponent<scene::SplineComponent>(scene::SplineComponentConfig{
                    .material = engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                    .materialAssetReference = std::string(assets::Project::kBuiltinDefaultShadedMaterialReference),
                });
                if (splineComponent)
                {
                    splineComponent->Rebuild();
                }
                break;
            }
            case AddableComponentType::Ocean:
                entity.CreateComponent<scene::OceanComponent>();
                break;
            case AddableComponentType::Camera:
            {
                const bool sceneAlreadyHasCamera = SceneHasAnyCamera(entity.GetScene());
                auto *cameraComponent = entity.CreateComponent<scene::CameraComponent>(new render::Camera(render::CameraConfig{
                    .fovY = 60.0f,
                    .nearPlane = 0.1f,
                    .farPlane = 100.0f,
                }));
                if (cameraComponent)
                {
                    cameraComponent->SetMainCamera(!sceneAlreadyHasCamera);
                }
                break;
            }
            case AddableComponentType::Animation:
                entity.CreateComponent<scene::AnimationComponent>();
                break;
            case AddableComponentType::ActiveRagdoll:
                entity.CreateComponent<scene::ActiveRagdollComponent>();
                break;
            case AddableComponentType::Light:
                entity.CreateComponent<scene::LightComponent>();
                break;
            case AddableComponentType::Rigidbody:
                entity.CreateComponent<scene::RigidbodyComponent>();
                break;
            case AddableComponentType::NavAgent:
                entity.CreateComponent<scene::NavAgentComponent>();
                break;
            case AddableComponentType::NavigationMesh:
                entity.CreateComponent<scene::NavigationMeshComponent>();
                break;
            case AddableComponentType::Collider:
                entity.CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{
                    .shape = entity.HasComponent<scene::TerrainComponent>()
                                 ? scene::ColliderShape::Terrain
                             : entity.HasComponent<scene::SplineComponent>() ? scene::ColliderShape::Mesh
                                                                             : scene::ColliderShape::Box,
                });
                break;
            case AddableComponentType::IblCapture:
                entity.CreateComponent<scene::IblCaptureComponent>();
                break;
            case AddableComponentType::PhysicalSky:
                entity.CreateComponent<scene::PhysicalSkyComponent>();
                break;
            case AddableComponentType::VolumetricCloud:
                entity.CreateComponent<scene::VolumetricCloudComponent>();
                break;
            case AddableComponentType::Script:
                entity.CreateComponent<scene::ScriptComponent>(scene::ScriptComponentConfig{});
                break;
            case AddableComponentType::SoundEmitter:
                entity.CreateComponent<scene::SoundEmitterComponent>();
                break;
            case AddableComponentType::SoundListener:
                entity.CreateComponent<scene::SoundListenerComponent>();
                break;
            case AddableComponentType::Canvas:
                if (auto *canvas = entity.CreateComponent<scene::CanvasComponent>())
                    canvas->SetBackend(scene::UIRenderBackend::RmlUi);
                break;
            case AddableComponentType::LegacyCanvas:
                if (auto *canvas = entity.CreateComponent<scene::CanvasComponent>())
                    canvas->SetBackend(scene::UIRenderBackend::Native);
                break;
            case AddableComponentType::RmlWidget:
                entity.CreateComponent<scene::RmlWidgetComponent>();
                break;
            case AddableComponentType::RectTransform:
                entity.CreateComponent<scene::RectTransformComponent>();
                break;
            case AddableComponentType::UIImage:
                entity.CreateComponent<scene::UIImageComponent>();
                break;
            case AddableComponentType::UIText:
                entity.CreateComponent<scene::UITextComponent>();
                break;
            case AddableComponentType::UIButton:
                entity.CreateComponent<scene::UIButtonComponent>();
                break;
            default:
                break;
            }
        }
    }

    void InspectorPanel::Initialize()
    {
        // Initialization code for the InspectorPanel
    }

    void InspectorPanel::RenderSceneEnvironmentInspector(scene::Scene &scene) const
    {
        auto &engine = core::Engine::GetInstance();
        auto &editorShell = EditorShell::GetInstance();
        static std::array<char, kInspectorPathBufferSize> environmentPathBuffer{};
        static std::string cachedEnvironmentPath;

        if (cachedEnvironmentPath != scene.GetEnvironmentMapPath())
        {
            cachedEnvironmentPath = scene.GetEnvironmentMapPath();
            std::fill(environmentPathBuffer.begin(), environmentPathBuffer.end(), '\0');
            strncpy_s(environmentPathBuffer.data(), environmentPathBuffer.size(), cachedEnvironmentPath.c_str(), _TRUNCATE);
        }

        ImGui::TextUnformatted("Scene Settings");
        if (!ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::InputText("HDRI Path", environmentPathBuffer.data(), environmentPathBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("...##Environment"))
        {
#ifdef _WIN32
            OPENFILENAMEA ofn = {};
            char fileName[MAX_PATH] = "";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFilter = "Environment Maps\0*.hdr;*.pfm;*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All Files\0*.*\0";
            ofn.lpstrFile = fileName;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn))
            {
                strncpy_s(environmentPathBuffer.data(), environmentPathBuffer.size(), fileName, _TRUNCATE);
            }
#endif
        }

        ImGui::BeginDisabled(std::strlen(environmentPathBuffer.data()) == 0);
        if (ImGui::Button("Load HDRI"))
        {
            const std::string selectedPath(environmentPathBuffer.data());
            auto *environmentTexture = engine.GetTextureManager().LoadEnvironmentTextureFromFile(selectedPath.c_str());
            scene.SetEnvironmentMap(environmentTexture, selectedPath);
            cachedEnvironmentPath = scene.GetEnvironmentMapPath();
            editorShell.MarkSceneDirty();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Clear HDRI"))
        {
            scene.ClearEnvironmentMap();
            cachedEnvironmentPath.clear();
            std::fill(environmentPathBuffer.begin(), environmentPathBuffer.end(), '\0');
            editorShell.MarkSceneDirty();
        }

        float environmentIntensity = scene.GetEnvironmentIntensity();
        if (ImGui::DragFloat("Environment Intensity", &environmentIntensity, 0.01f, 0.0f, 32.0f))
        {
            scene.SetEnvironmentIntensity(environmentIntensity);
            editorShell.MarkSceneDirty();
        }

        ImGui::Text("Sky / IBL: %s", scene.HasEnvironmentMap() ? "loaded" : (scene.GetEnvironmentMapPath().empty() ? "not set" : "failed to load"));
    }

    bool InspectorPanel::RenderPropertyEditor(scene::Property &property) const
    {
        switch (property.type)
        {
        case scene::PropertyType::Float:
        {
            float value = std::stof(property.value);
            if (ImGui::DragFloat(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::Int:
        {
            int value = std::stoi(property.value);
            if (ImGui::DragInt(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::String:
        {
            char buffer[256];
            strncpy_s(buffer, sizeof(buffer), property.value.c_str(), _TRUNCATE);
            if (ImGui::InputText(property.name.c_str(), buffer, sizeof(buffer)))
            {
                property.value = std::string(buffer);
                return true;
            }
            break;
        }
        case scene::PropertyType::Vec3:
        {
            glm::vec3 value = ParseVec3Property(property.value);
            if (ImGui::DragFloat3(property.name.c_str(), &value.x))
            {
                property.value = std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
                return true;
            }
            break;
        }
        case scene::PropertyType::Vec2:
        {
            glm::vec2 value{0.0f, 0.0f};
            std::sscanf(property.value.c_str(), "%f,%f", &value.x, &value.y);
            if (ImGui::DragFloat2(property.name.c_str(), &value.x))
            {
                property.value = std::to_string(value.x) + "," + std::to_string(value.y);
                return true;
            }
            break;
        }
        case scene::PropertyType::Double:
        {
            double value = std::stod(property.value);
            if (ImGui::InputDouble(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::Bool:
        {
            bool value = (property.value == "true" || property.value == "True" || property.value == "1");
            if (ImGui::Checkbox(property.name.c_str(), &value))
            {
                property.value = value ? "true" : "false";
                return true;
            }
            break;
        }
        case scene::PropertyType::Color:
        {
            glm::vec4 value{1.0f};
            std::sscanf(property.value.c_str(), "%f,%f,%f,%f", &value.r, &value.g, &value.b, &value.a);
            if (ImGui::ColorEdit4(property.name.c_str(), &value.r))
            {
                property.value = std::to_string(value.r) + "," + std::to_string(value.g) + "," +
                                 std::to_string(value.b) + "," + std::to_string(value.a);
                return true;
            }
            break;
        }
        case scene::PropertyType::Enum:
        {
            if (property.enumOptions.empty())
            {
                ImGui::Text("%s: <No options>", property.name.c_str());
                break;
            }

            int currentIndex = 0;
            try
            {
                currentIndex = std::stoi(property.value);
            }
            catch (...)
            {
                const auto option = std::find(property.enumOptions.begin(), property.enumOptions.end(), property.value);
                currentIndex = option != property.enumOptions.end() ? static_cast<int>(std::distance(property.enumOptions.begin(), option)) : 0;
                property.value = std::to_string(currentIndex);
            }

            currentIndex = std::clamp(currentIndex, 0, static_cast<int>(property.enumOptions.size()) - 1);
            if (property.value != std::to_string(currentIndex))
            {
                property.value = std::to_string(currentIndex);
            }

            if (ImGui::BeginCombo(property.name.c_str(), property.enumOptions[static_cast<size_t>(currentIndex)].c_str()))
            {
                for (size_t i = 0; i < property.enumOptions.size(); ++i)
                {
                    bool isSelected = (currentIndex == static_cast<int>(i));
                    if (ImGui::Selectable(property.enumOptions[i].c_str(), isSelected))
                    {
                        currentIndex = static_cast<int>(i);
                        property.value = std::to_string(currentIndex);
                        ImGui::EndCombo();
                        return true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case scene::PropertyType::Entity:
        {
            scene::EntityID selectedId = 0;
            try { selectedId = static_cast<scene::EntityID>(std::stoul(property.value)); }
            catch (...) { property.value = "0"; }

            auto *currentScene = core::Engine::GetInstance().GetScene();
            auto *selectedEntity = currentScene && selectedId != 0 ? currentScene->FindEntityByID(selectedId) : nullptr;
            const std::string preview = selectedEntity ? selectedEntity->GetName() : "None";
            bool changed = false;
            if (ImGui::BeginCombo(property.name.c_str(), preview.c_str()))
            {
                if (ImGui::Selectable("None", selectedId == 0))
                {
                    property.value = "0";
                    changed = true;
                }
                const auto renderEntities = [&](scene::Entity *entity, const auto &self) -> void
                {
                    if (!entity) return;
                    const bool selected = entity->GetID() == selectedId;
                    if (ImGui::Selectable((entity->GetName() + "##" + std::to_string(entity->GetID())).c_str(), selected))
                    {
                        property.value = std::to_string(entity->GetID());
                        changed = true;
                    }
                    for (auto *child : entity->GetChildren()) self(child, self);
                };
                if (currentScene)
                    for (auto *root : currentScene->GetRootEntities()) renderEntities(root, renderEntities);
                ImGui::EndCombo();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kHierarchyEntityDragDropPayload))
                {
                    auto *dropped = *static_cast<scene::Entity *const *>(payload->Data);
                    if (dropped)
                    {
                        property.value = std::to_string(dropped->GetID());
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (selectedEntity)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("ID %u", selectedId);
            }
            return changed;
        }
        default:
            break;
        }

        return false;
    }

    bool InspectorPanel::RenderScriptComponentEditor(scene::ScriptComponent &scriptComponent, scene::Entity &entity) const
    {
        (void)entity;

        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        auto &scriptEngine = core::Engine::GetInstance().GetScriptEngine();
        const auto classNames = scriptEngine.GetClassNames();
        static const std::vector<ScriptAssetOption> noScriptAssetOptions;
        const auto &scriptAssetOptions = project != nullptr
                                             ? GetCachedProjectScriptAssetOptions(*project, classNames)
                                             : noScriptAssetOptions;
        const auto builtinScriptOptions = CollectBuiltinScriptAssetOptions(classNames);
        const std::string currentSource = scriptComponent.GetSource();
        std::string previewValue = currentSource.empty() ? "<None>" : currentSource;
        if (const auto *selectedAsset = FindScriptAssetOptionForClassName(scriptAssetOptions, currentSource))
        {
            previewValue = selectedAsset->displayName;
        }
        else if (const auto *selectedBuiltin = FindScriptAssetOptionForClassName(builtinScriptOptions, currentSource))
        {
            previewValue = selectedBuiltin->displayName;
        }

        bool changed = false;
        if (ImGui::BeginCombo("Source", previewValue.c_str()))
        {
            const bool isNoneSelected = currentSource.empty();
            if (ImGui::Selectable("<None>", isNoneSelected))
            {
                scriptComponent.SetSource({});
                changed = true;
            }
            if (isNoneSelected)
            {
                ImGui::SetItemDefaultFocus();
            }

            if (!scriptAssetOptions.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Scripts in Assets");
                for (const auto &option : scriptAssetOptions)
                {
                    const bool isSelected = option.className == currentSource;
                    std::string label = option.displayName;
                    if (!option.classLoaded)
                    {
                        label += " (not loaded)";
                    }
                    label += "##" + option.reference;

                    if (ImGui::Selectable(label.c_str(), isSelected))
                    {
                        scriptComponent.SetSource(option.className);
                        changed = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            if (!builtinScriptOptions.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Engine Builtin Scripts");
                for (const auto &option : builtinScriptOptions)
                {
                    const bool isSelected = option.className == currentSource;
                    std::string label = option.displayName;
                    if (!option.classLoaded)
                    {
                        label += " (not loaded)";
                    }
                    label += "##" + option.reference;

                    if (ImGui::Selectable(label.c_str(), isSelected))
                    {
                        scriptComponent.SetSource(option.className);
                        changed = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            std::unordered_set<std::string> assetClassNames;
            assetClassNames.reserve(scriptAssetOptions.size() + builtinScriptOptions.size());
            for (const auto &option : scriptAssetOptions)
            {
                assetClassNames.insert(option.className);
            }
            for (const auto &option : builtinScriptOptions)
            {
                assetClassNames.insert(option.className);
            }

            bool hasLooseLoadedClasses = false;
            for (const auto &className : classNames)
            {
                if (!assetClassNames.contains(className))
                {
                    hasLooseLoadedClasses = true;
                    break;
                }
            }

            if (hasLooseLoadedClasses)
            {
                ImGui::Separator();
                ImGui::TextDisabled("Other Loaded Classes");
                for (const auto &className : classNames)
                {
                    if (assetClassNames.contains(className))
                    {
                        continue;
                    }

                    const bool isSelected = className == currentSource;
                    if (ImGui::Selectable(className.c_str(), isSelected))
                    {
                        scriptComponent.SetSource(className);
                        changed = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            ImGui::EndCombo();
        }

        if (project)
        {
            const bool scriptAuthoringDisabled = editorShell.IsRuntimeExportProject();
            ImGui::SameLine();
            if (ImGui::Button("Refresh##ScriptSources"))
            {
                project->RefreshAssetRegistry();
                InvalidateScriptAssetOptionsCache();
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(scriptAuthoringDisabled);
            if (ImGui::Button("New##ScriptSource"))
            {
                ImGui::OpenPopup("Create Script");
            }
            ImGui::EndDisabled();

            static std::array<char, kNewScriptNameBufferSize> newScriptNameBuffer{};
            static std::string createScriptErrorMessage;
            if (ImGui::BeginPopupModal("Create Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("Name", newScriptNameBuffer.data(), newScriptNameBuffer.size());
                ImGui::TextDisabled("Creates Assets/Scripts/<Name>.cs");
                if (!createScriptErrorMessage.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", createScriptErrorMessage.c_str());
                }

                if (ImGui::Button("Create"))
                {
                    std::string createdClassName;
                    if (editorShell.CreateScriptAsset(newScriptNameBuffer.data(), &createdClassName, &createScriptErrorMessage))
                    {
                        scriptComponent.SetSource(createdClassName);
                        changed = true;
                        editorShell.BuildProjectScripts();
                        std::fill(newScriptNameBuffer.begin(), newScriptNameBuffer.end(), '\0');
                        createScriptErrorMessage.clear();
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    std::fill(newScriptNameBuffer.begin(), newScriptNameBuffer.end(), '\0');
                    createScriptErrorMessage.clear();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            if (scriptAuthoringDisabled)
            {
                ImGui::TextDisabled("Script authoring is disabled for exported runtime bundles.");
            }
        }

        const ScriptAssetOption *selectedSourceOption = FindScriptAssetOptionForClassName(scriptAssetOptions, currentSource);
        if (!selectedSourceOption)
        {
            selectedSourceOption = FindScriptAssetOptionForClassName(builtinScriptOptions, currentSource);
        }

        const auto serializedFields = scriptComponent.GetSerializedFields();
        const auto fieldValues = scriptComponent.GetFieldValuesSnapshot();
        if (!currentSource.empty() && selectedSourceOption && !serializedFields.empty())
        {
            ImGui::BeginDisabled(selectedSourceOption->sourcePath.empty() || editorShell.IsRuntimeExportProject());
            if (ImGui::Button("Set Values As Script Defaults"))
            {
                const auto saveResult = SaveScriptDefaultsToSource(selectedSourceOption->sourcePath, serializedFields, fieldValues);
                if (!saveResult.error.empty())
                {
                    editorShell.SetStatusMessage(saveResult.error);
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, saveResult.error);
                }
                else
                {
                    std::string message = "Updated " + std::to_string(saveResult.updated) + " script default" +
                                          (saveResult.updated == 1 ? "" : "s") + ".";
                    if (saveResult.skipped > 0)
                    {
                        message += " Skipped " + std::to_string(saveResult.skipped) + ".";
                    }
                    editorShell.SetStatusMessage(message);
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, message);
                    changed = saveResult.updated > 0 || changed;
                }
            }
            ImGui::EndDisabled();
        }

        if (currentSource.empty() && scriptAssetOptions.empty() && builtinScriptOptions.empty() && classNames.empty())
        {
            ImGui::TextDisabled(project ? "No scripts were found in the asset directory or loaded assembly."
                                        : "Open a project to browse script assets.");
        }
        else if (!currentSource.empty() && !scriptEngine.HasClass(currentSource))
        {
            ImGui::TextDisabled("Selected script is not present in the loaded assembly yet. Build scripts to compile new or changed sources.");
        }
        else if (classNames.empty())
        {
            ImGui::TextDisabled("No script classes are loaded.");
        }

        std::optional<std::vector<scene::Entity *>> sceneEntities;
        int fieldIndex = 0;
        for (const auto &field : serializedFields)
        {
            std::optional<scripting::ScriptFieldValue> fieldValue;
            if (const auto fieldIterator = fieldValues.find(field.name); fieldIterator != fieldValues.end())
            {
                fieldValue = fieldIterator->second;
            }
            if (!fieldValue.has_value())
            {
                fieldValue = scripting::IsFieldValueCompatible(field.type, field.defaultValue)
                                 ? field.defaultValue
                                 : scripting::MakeDefaultFieldValue(field.type);
            }

            ImGui::PushID(fieldIndex++);
            switch (field.type)
            {
            case scripting::ScriptFieldType::Boolean:
            {
                bool value = std::get<bool>(*fieldValue);
                if (ImGui::Checkbox(field.name.c_str(), &value))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::Int32:
            {
                int value = std::get<int32_t>(*fieldValue);
                if (ImGui::DragInt(field.name.c_str(), &value))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, static_cast<int32_t>(value));
                }
                break;
            }
            case scripting::ScriptFieldType::Float:
            {
                float value = std::get<float>(*fieldValue);
                if (ImGui::DragFloat(field.name.c_str(), &value, 0.01f))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::Double:
            {
                double value = std::get<double>(*fieldValue);
                if (ImGui::InputDouble(field.name.c_str(), &value))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::String:
            {
                char buffer[256];
                const auto &value = std::get<std::string>(*fieldValue);
                strncpy_s(buffer, sizeof(buffer), value.c_str(), _TRUNCATE);
                if (ImGui::InputText(field.name.c_str(), buffer, sizeof(buffer)))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, std::string(buffer));
                }
                break;
            }
            case scripting::ScriptFieldType::MaterialAsset:
            {
                const auto &options = GetCachedAssetReferenceOptions(
                    editorShell.GetProject(), assets::ProjectAssetType::Material);
                const auto &value = std::get<std::string>(*fieldValue);
                const std::string preview = GetAssetReferencePreview(options, value, "<None>");
                if (ImGui::BeginCombo(field.name.c_str(), preview.c_str()))
                {
                    const bool noneSelected = value.empty();
                    if (ImGui::Selectable("<None>", noneSelected))
                    {
                        changed |= scriptComponent.SetFieldValue(field.name, std::string{});
                    }
                    if (noneSelected)
                        ImGui::SetItemDefaultFocus();

                    for (const auto &option : options)
                    {
                        const bool selected = option.reference == value;
                        if (ImGui::Selectable(option.displayName.c_str(), selected))
                        {
                            changed |= scriptComponent.SetFieldValue(field.name, option.reference);
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (auto droppedReference = AcceptDroppedMaterialAssetReference())
                {
                    changed |= scriptComponent.SetFieldValue(field.name, *droppedReference);
                }
                break;
            }
            case scripting::ScriptFieldType::PrefabAsset:
            {
                const auto &options = GetCachedAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Prefab);
                const auto &value = std::get<std::string>(*fieldValue);
                const std::string preview = GetAssetReferencePreview(options, value, "<None>");
                if (ImGui::BeginCombo(field.name.c_str(), preview.c_str()))
                {
                    const bool noneSelected = value.empty();
                    if (ImGui::Selectable("<None>", noneSelected))
                    {
                        changed |= scriptComponent.SetFieldValue(field.name, std::string{});
                    }
                    if (noneSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }

                    for (const auto &option : options)
                    {
                        const bool selected = option.reference == value;
                        if (ImGui::Selectable(option.displayName.c_str(), selected))
                        {
                            changed |= scriptComponent.SetFieldValue(field.name, option.reference);
                        }
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (auto droppedReference = AcceptDroppedPrefabAssetReference())
                {
                    changed |= scriptComponent.SetFieldValue(field.name, *droppedReference);
                }
                break;
            }
            case scripting::ScriptFieldType::ScriptableObjectAsset:
            {
                const auto &allOptions = GetCachedAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::ScriptableObject);
                const auto &value = std::get<std::string>(*fieldValue);
                const auto matchesType = [&](const AssetReferenceOption &option)
                {
                    if (field.referenceTypeName.empty())
                        return true;
                    const auto actualType = ReadScriptableObjectType(editorShell.GetProject(), option.reference);
                    const auto *actualDefinition = core::Engine::GetInstance().GetScriptEngine().FindClass(actualType);
                    return actualType == field.referenceTypeName ||
                           (actualDefinition && std::find(actualDefinition->assignableTypeNames.begin(), actualDefinition->assignableTypeNames.end(), field.referenceTypeName) != actualDefinition->assignableTypeNames.end());
                };
                std::string preview = value.empty() ? "<None>" : value;
                for (const auto &option : allOptions)
                {
                    if (option.reference == value)
                    {
                        preview = option.displayName;
                        break;
                    }
                }
                if (ImGui::BeginCombo(field.name.c_str(), preview.c_str()))
                {
                    if (ImGui::Selectable("<None>", value.empty()))
                    {
                        changed |= scriptComponent.SetFieldValue(field.name, std::string{});
                    }
                    for (const auto &option : allOptions)
                    {
                        if (!matchesType(option))
                        {
                            continue;
                        }
                        const bool selected = option.reference == value;
                        if (ImGui::Selectable(option.displayName.c_str(), selected))
                        {
                            changed |= scriptComponent.SetFieldValue(field.name, option.reference);
                        }
                    }
                    ImGui::EndCombo();
                }
                if (auto droppedReference = AcceptDroppedScriptableObjectReference(); droppedReference && matchesType({*droppedReference, *droppedReference}))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, *droppedReference);
                }
                break;
            }
            case scripting::ScriptFieldType::Vector2:
            {
                auto value = std::get<glm::vec2>(*fieldValue);
                if (ImGui::DragFloat2(field.name.c_str(), &value.x, 0.01f))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::Vector3:
            {
                auto value = std::get<glm::vec3>(*fieldValue);
                if (ImGui::DragFloat3(field.name.c_str(), &value.x, 0.01f))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::EntityId:
            case scripting::ScriptFieldType::GameObject:
            case scripting::ScriptFieldType::MeshComponent:
            case scripting::ScriptFieldType::CameraComponent:
            case scripting::ScriptFieldType::LightComponent:
            case scripting::ScriptFieldType::RigidbodyComponent:
            case scripting::ScriptFieldType::ColliderComponent:
            case scripting::ScriptFieldType::AnimationComponent:
            case scripting::ScriptFieldType::CanvasComponent:
            case scripting::ScriptFieldType::RectTransformComponent:
            case scripting::ScriptFieldType::UIImageComponent:
            case scripting::ScriptFieldType::UITextComponent:
            case scripting::ScriptFieldType::UIButtonComponent:
            case scripting::ScriptFieldType::ParticleSystemComponent:
            case scripting::ScriptFieldType::SoundEmitterComponent:
            {
                uint32_t selectedEntityId = std::get<uint32_t>(*fieldValue);
                scene::Scene *scene = entity.GetScene();
                if (!sceneEntities.has_value())
                {
                    sceneEntities.emplace();
                    if (scene)
                    {
                        for (auto *rootEntity : scene->GetRootEntities())
                        {
                            CollectEntitiesRecursive(rootEntity, *sceneEntities);
                        }
                    }
                }
                const auto &entities = *sceneEntities;

                auto isCompatibleEntity = [field](const scene::Entity &candidate) -> bool
                {
                    switch (field.type)
                    {
                    case scripting::ScriptFieldType::MeshComponent:
                        return candidate.HasComponent<scene::MeshComponent>();
                    case scripting::ScriptFieldType::CameraComponent:
                        return candidate.HasComponent<scene::CameraComponent>();
                    case scripting::ScriptFieldType::LightComponent:
                        return candidate.HasComponent<scene::LightComponent>();
                    case scripting::ScriptFieldType::RigidbodyComponent:
                        return candidate.HasComponent<scene::RigidbodyComponent>();
                    case scripting::ScriptFieldType::ColliderComponent:
                        return candidate.HasComponent<scene::ColliderComponent>();
                    case scripting::ScriptFieldType::AnimationComponent:
                        return candidate.HasComponent<scene::AnimationComponent>();
                    case scripting::ScriptFieldType::CanvasComponent:
                        return candidate.HasComponent<scene::CanvasComponent>();
                    case scripting::ScriptFieldType::RectTransformComponent:
                        return candidate.HasComponent<scene::RectTransformComponent>();
                    case scripting::ScriptFieldType::UIImageComponent:
                        return candidate.HasComponent<scene::UIImageComponent>();
                    case scripting::ScriptFieldType::UITextComponent:
                        return candidate.HasComponent<scene::UITextComponent>();
                    case scripting::ScriptFieldType::UIButtonComponent:
                        return candidate.HasComponent<scene::UIButtonComponent>();
                    case scripting::ScriptFieldType::ParticleSystemComponent:
                        return candidate.HasComponent<scene::ParticleSystemComponent>();
                    case scripting::ScriptFieldType::SoundEmitterComponent:
                        return candidate.HasComponent<scene::SoundEmitterComponent>();
                    case scripting::ScriptFieldType::EntityId:
                    case scripting::ScriptFieldType::GameObject:
                    default:
                        return true;
                    }
                };

                std::string previewLabel = "<None>";
                if (scene)
                {
                    if (auto *selectedEntity = scene->FindEntityByID(selectedEntityId))
                    {
                        previewLabel = selectedEntity->GetName().empty()
                                           ? ("Entity " + std::to_string(selectedEntityId))
                                           : selectedEntity->GetName();
                    }
                }

                if (ImGui::BeginCombo(field.name.c_str(), previewLabel.c_str()))
                {
                    const bool isNoneSelected = selectedEntityId == 0;
                    if (ImGui::Selectable("<None>", isNoneSelected))
                    {
                        changed |= scriptComponent.SetFieldValue(field.name, uint32_t{0});
                    }
                    if (isNoneSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }

                    for (auto *candidate : entities)
                    {
                        if (!candidate || !isCompatibleEntity(*candidate))
                        {
                            continue;
                        }

                        const bool isSelected = candidate->GetID() == selectedEntityId;
                        std::string label = candidate->GetName().empty()
                                                ? ("Entity " + std::to_string(candidate->GetID()))
                                                : (candidate->GetName() + "##" + std::to_string(candidate->GetID()));
                        if (ImGui::Selectable(label.c_str(), isSelected))
                        {
                            changed |= scriptComponent.SetFieldValue(field.name, candidate->GetID());
                        }
                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    ImGui::EndCombo();
                }
                break;
            }
            case scripting::ScriptFieldType::None:
            default:
                ImGui::TextDisabled("Unsupported field: %s", field.name.c_str());
                break;
            }
            ImGui::PopID();
        }

        if (!currentSource.empty() && scriptComponent.GetSerializedFields().empty())
        {
            ImGui::TextDisabled("Selected script exposes no serialized fields.");
        }

        return changed;
    }

    void InspectorPanel::RenderCameraPostProcessEditor(scene::CameraComponent &cameraComponent) const
    {
        if (!ImGui::TreeNode("Post Processing"))
        {
            return;
        }

        const auto presetOptions = CollectAssetReferenceOptions(EditorShell::GetInstance().GetProject(), assets::ProjectAssetType::PostProcessPreset);
        const std::string &presetReference = cameraComponent.GetPostProcessPresetAssetReference();
        const std::string presetLabel = presetReference.empty() ? "None" : presetReference;
        if (ImGui::BeginCombo("Preset", presetLabel.c_str()))
        {
            if (ImGui::Selectable("None", presetReference.empty()))
            {
                if (cameraComponent.SetPostProcessPresetAssetReference({}))
                    EditorShell::GetInstance().MarkSceneDirty();
            }
            for (const auto &option : presetOptions)
            {
                const bool selected = option.reference == presetReference;
                if (ImGui::Selectable(option.displayName.c_str(), selected))
                {
                    if (cameraComponent.SetPostProcessPresetAssetReference(option.reference))
                        EditorShell::GetInstance().MarkSceneDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (!presetReference.empty())
        {
            ImGui::TextDisabled("Changes below can be saved back to the shared preset.");
        }
        else
            ImGui::TextDisabled("Legacy inline stack (select a preset to share settings).");

        const auto &registeredTypes = render::GetRegisteredPostProcessEffectTypes();
        static int selectedEffectTypeIndex = 0;
        if (!registeredTypes.empty())
        {
            selectedEffectTypeIndex = std::clamp(selectedEffectTypeIndex, 0, static_cast<int>(registeredTypes.size()) - 1);
            if (ImGui::BeginCombo("Add Effect", registeredTypes[selectedEffectTypeIndex].c_str()))
            {
                for (int index = 0; index < static_cast<int>(registeredTypes.size()); ++index)
                {
                    const bool isSelected = (selectedEffectTypeIndex == index);
                    if (ImGui::Selectable(registeredTypes[index].c_str(), isSelected))
                    {
                        selectedEffectTypeIndex = index;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("Add") && selectedEffectTypeIndex >= 0 && selectedEffectTypeIndex < static_cast<int>(registeredTypes.size()))
            {
                cameraComponent.AddPostProcessEffectByType(registeredTypes[selectedEffectTypeIndex]);
            }
        }

        for (size_t effectIndex = 0; effectIndex < cameraComponent.GetPostProcessEffects().size(); ++effectIndex)
        {
            auto *effect = cameraComponent.GetPostProcessEffect(effectIndex);
            if (!effect)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(effectIndex));
            const bool effectTreeOpen = ImGui::TreeNode(effect->GetDisplayName().c_str());
            if (ImGui::BeginDragDropSource())
            {
                const size_t draggedIndex = effectIndex;
                ImGui::SetDragDropPayload(kPostProcessEffectDragDropPayload, &draggedIndex, sizeof(draggedIndex));
                ImGui::TextUnformatted(effect->GetDisplayName().c_str());
                ImGui::EndDragDropSource();
            }
            bool reorderedEffect = false;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kPostProcessEffectDragDropPayload))
                {
                    const size_t draggedIndex = *static_cast<const size_t *>(payload->Data);
                    if (draggedIndex != effectIndex)
                    {
                        cameraComponent.MovePostProcessEffect(draggedIndex, effectIndex);
                        reorderedEffect = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (reorderedEffect)
            {
                if (effectTreeOpen)
                {
                    ImGui::TreePop();
                }
                ImGui::PopID();
                break;
            }
            if (effectTreeOpen)
            {
                bool isEnabled = effect->IsEnabled();
                if (ImGui::Checkbox("Enabled", &isEnabled))
                {
                    effect->SetEnabled(isEnabled);
                    EditorShell::GetInstance().MarkSceneDirty();
                }

                if (ImGui::Button("Up") && effectIndex > 0)
                {
                    cameraComponent.MovePostProcessEffect(effectIndex, effectIndex - 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && effectIndex + 1 < cameraComponent.GetPostProcessEffects().size())
                {
                    cameraComponent.MovePostProcessEffect(effectIndex, effectIndex + 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove"))
                {
                    cameraComponent.RemovePostProcessEffect(effectIndex);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }

                auto parameters = effect->GetParameters();
                bool parametersChanged = false;
                for (auto &parameter : parameters)
                {
                    scene::Property property{
                        .name = parameter.name,
                        .type = scene::PropertyType::String,
                        .value = parameter.value,
                        .enumOptions = parameter.enumOptions,
                    };

                    switch (parameter.type)
                    {
                    case render::PostProcessParameterType::Float:
                        property.type = scene::PropertyType::Float;
                        break;
                    case render::PostProcessParameterType::Int:
                        property.type = scene::PropertyType::Int;
                        break;
                    case render::PostProcessParameterType::Bool:
                        property.type = scene::PropertyType::Bool;
                        break;
                    case render::PostProcessParameterType::Enum:
                        property.type = scene::PropertyType::Enum;
                        break;
                    case render::PostProcessParameterType::String:
                    default:
                        property.type = scene::PropertyType::String;
                        break;
                    }

                    parametersChanged |= RenderPropertyEditor(property);
                    parameter.value = property.value;
                    parameter.enumOptions = property.enumOptions;
                }

                if (parametersChanged)
                {
                    effect->SetParameters(parameters);
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (!presetReference.empty() && ImGui::Button("Save Preset"))
        {
            std::string errorMessage;
            auto preset = assets::CapturePostProcessPreset(cameraComponent.GetPostProcessEffects());
            if (!core::Engine::GetInstance().GetAssetManager().SavePostProcessPresetAsset(presetReference, preset, &errorMessage))
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error, errorMessage);
        }
        ImGui::TreePop();
    }

    void InspectorPanel::RenderEditorCameraInspector(EditorShell::EditorViewportCamera &camera) const
    {
        ImGui::TextUnformatted("Editor Camera");
        auto &editorShell = EditorShell::GetInstance();

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat3("Position", &camera.position.x, 0.05f);
            ImGui::DragFloat("Yaw", &camera.yawDegrees, 0.1f);
            ImGui::DragFloat("Pitch", &camera.pitchDegrees, 0.1f, -89.0f, 89.0f);
        }

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::DragFloat("Move Speed", &camera.moveSpeed, 0.05f, 0.1f, 1000.0f, "%.2f"))
            {
                camera.moveSpeed = std::clamp(camera.moveSpeed, 0.1f, 1000.0f);
                editorShell.MarkProjectDirty();
            }

            ImGui::Text("Current Fly Speed: %.2f", camera.moveSpeed * camera.speedAdjustment);

            float fov = camera.camera.GetFOV();
            if (ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 179.0f))
            {
                camera.camera.SetFOV(fov);
                editorShell.MarkProjectDirty();
            }

            float nearPlane = camera.camera.GetNearPlane();
            if (ImGui::DragFloat("Near Plane", &nearPlane, 0.01f, 0.001f, camera.camera.GetFarPlane() - 0.001f))
            {
                camera.camera.SetNearPlane(nearPlane);
                editorShell.MarkProjectDirty();
            }

            float farPlane = camera.camera.GetFarPlane();
            const float minFarPlane = camera.camera.GetNearPlane() + 0.001f;
            // Large aerial and ocean scenes need intersections far beyond the
            // previous 10 km editor ceiling. Camera::SetFarPlane itself has no
            // such cap, so do not impose one in the inspector.
            if (ImGui::DragFloat("Far Plane", &farPlane, 10.0f, minFarPlane, 1000000.0f, "%.1f"))
            {
                camera.camera.SetFarPlane(farPlane);
                editorShell.MarkProjectDirty();
            }
        }

        RenderEditorCameraPostProcessEditor(camera);
    }

    void InspectorPanel::RenderEditorCameraPostProcessEditor(EditorShell::EditorViewportCamera &camera) const
    {
        if (!ImGui::TreeNode("Post Processing"))
        {
            return;
        }

        const auto presetOptions = CollectAssetReferenceOptions(EditorShell::GetInstance().GetProject(), assets::ProjectAssetType::PostProcessPreset);
        const std::string &presetReference = camera.GetPostProcessPresetAssetReference();
        const std::string presetLabel = presetReference.empty() ? "None" : presetReference;
        if (ImGui::BeginCombo("Preset", presetLabel.c_str()))
        {
            if (ImGui::Selectable("None", presetReference.empty()))
            {
                if (camera.SetPostProcessPresetAssetReference({}))
                    EditorShell::GetInstance().MarkProjectDirty();
            }
            for (const auto &option : presetOptions)
            {
                const bool selected = option.reference == presetReference;
                if (ImGui::Selectable(option.displayName.c_str(), selected))
                {
                    if (camera.SetPostProcessPresetAssetReference(option.reference))
                        EditorShell::GetInstance().MarkProjectDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (!presetReference.empty())
        {
            ImGui::TextDisabled("Changes below can be saved back to the shared preset.");
        }
        else
            ImGui::TextDisabled("Legacy inline stack (select a preset to share settings).");

        const auto &registeredTypes = render::GetRegisteredPostProcessEffectTypes();
        static int selectedEffectTypeIndex = 0;
        if (!registeredTypes.empty())
        {
            selectedEffectTypeIndex = std::clamp(selectedEffectTypeIndex, 0, static_cast<int>(registeredTypes.size()) - 1);
            if (ImGui::BeginCombo("Add Effect", registeredTypes[selectedEffectTypeIndex].c_str()))
            {
                for (int index = 0; index < static_cast<int>(registeredTypes.size()); ++index)
                {
                    const bool isSelected = (selectedEffectTypeIndex == index);
                    if (ImGui::Selectable(registeredTypes[index].c_str(), isSelected))
                    {
                        selectedEffectTypeIndex = index;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("Add") && selectedEffectTypeIndex >= 0 && selectedEffectTypeIndex < static_cast<int>(registeredTypes.size()))
            {
                camera.AddPostProcessEffectByType(registeredTypes[selectedEffectTypeIndex]);
            }
        }

        for (size_t effectIndex = 0; effectIndex < camera.GetPostProcessEffects().size(); ++effectIndex)
        {
            auto *effect = camera.GetPostProcessEffect(effectIndex);
            if (!effect)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(effectIndex));
            const bool effectTreeOpen = ImGui::TreeNode(effect->GetDisplayName().c_str());
            if (ImGui::BeginDragDropSource())
            {
                const size_t draggedIndex = effectIndex;
                ImGui::SetDragDropPayload(kEditorPostProcessEffectDragDropPayload, &draggedIndex, sizeof(draggedIndex));
                ImGui::TextUnformatted(effect->GetDisplayName().c_str());
                ImGui::EndDragDropSource();
            }
            bool reorderedEffect = false;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kEditorPostProcessEffectDragDropPayload))
                {
                    const size_t draggedIndex = *static_cast<const size_t *>(payload->Data);
                    if (draggedIndex != effectIndex)
                    {
                        camera.MovePostProcessEffect(draggedIndex, effectIndex);
                        reorderedEffect = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (reorderedEffect)
            {
                if (effectTreeOpen)
                {
                    ImGui::TreePop();
                }
                ImGui::PopID();
                break;
            }
            if (effectTreeOpen)
            {
                bool isEnabled = effect->IsEnabled();
                if (ImGui::Checkbox("Enabled", &isEnabled))
                {
                    effect->SetEnabled(isEnabled);
                    EditorShell::GetInstance().MarkProjectDirty();
                }

                if (ImGui::Button("Up") && effectIndex > 0)
                {
                    camera.MovePostProcessEffect(effectIndex, effectIndex - 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && effectIndex + 1 < camera.GetPostProcessEffects().size())
                {
                    camera.MovePostProcessEffect(effectIndex, effectIndex + 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove"))
                {
                    camera.RemovePostProcessEffect(effectIndex);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }

                auto parameters = effect->GetParameters();
                bool parametersChanged = false;
                for (auto &parameter : parameters)
                {
                    scene::Property property{
                        .name = parameter.name,
                        .type = scene::PropertyType::String,
                        .value = parameter.value,
                        .enumOptions = parameter.enumOptions,
                    };

                    switch (parameter.type)
                    {
                    case render::PostProcessParameterType::Float:
                        property.type = scene::PropertyType::Float;
                        break;
                    case render::PostProcessParameterType::Int:
                        property.type = scene::PropertyType::Int;
                        break;
                    case render::PostProcessParameterType::Bool:
                        property.type = scene::PropertyType::Bool;
                        break;
                    case render::PostProcessParameterType::Enum:
                        property.type = scene::PropertyType::Enum;
                        break;
                    case render::PostProcessParameterType::String:
                    default:
                        property.type = scene::PropertyType::String;
                        break;
                    }

                    parametersChanged |= RenderPropertyEditor(property);
                    parameter.value = property.value;
                    parameter.enumOptions = property.enumOptions;
                }

                if (parametersChanged)
                {
                    effect->SetParameters(parameters);
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (!presetReference.empty() && ImGui::Button("Save Preset"))
        {
            std::string errorMessage;
            auto preset = assets::CapturePostProcessPreset(camera.GetPostProcessEffects());
            if (!core::Engine::GetInstance().GetAssetManager().SavePostProcessPresetAsset(presetReference, preset, &errorMessage))
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error, errorMessage);
        }
        ImGui::TreePop();
    }

    void InspectorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        if (editorShell.IsEditorCameraSelected())
        {
            RenderEditorCameraInspector(editorShell.GetEditorCamera());
            return;
        }

        auto entity = editorShell.GetSelectedEntity();
        if (!entity)
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            if (!scene)
            {
                ImGui::Text("No entity selected.");
                return;
            }

            RenderSceneEnvironmentInspector(*scene);
            return;
        }

        ImGui::Text("Entity Name: %s", entity->GetName().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("ID: %u", entity->GetID());
        auto isActive = entity->IsSelfActive();
        if (ImGui::Checkbox("Active", &isActive))
        {
            entity->SetActive(isActive);
            entity->AddPrefabOverride("Active");
            editorShell.MarkSceneDirty();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Hierarchy: %s", entity->IsActive() ? "Active" : "Inactive");

        {
            static std::unordered_map<scene::EntityID, std::array<char, 256>> tagBuffers;
            static std::unordered_map<scene::EntityID, std::string> cachedTagText;

            const auto entityId = entity->GetID();
            const std::string currentTagText = JoinTags(entity->GetTags());
            auto &cachedText = cachedTagText[entityId];
            auto &tagBuffer = tagBuffers[entityId];
            if (cachedText != currentTagText)
            {
                cachedText = currentTagText;
                std::fill(tagBuffer.begin(), tagBuffer.end(), '\0');
                strncpy_s(tagBuffer.data(), tagBuffer.size(), cachedText.c_str(), _TRUNCATE);
            }

            if (ImGui::InputText("Tags", tagBuffer.data(), tagBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue) ||
                ImGui::IsItemDeactivatedAfterEdit())
            {
                const std::string editedText(tagBuffer.data());
                if (editedText != currentTagText)
                {
                    entity->SetTags(ParseTags(editedText));
                    cachedText = JoinTags(entity->GetTags());
                    std::fill(tagBuffer.begin(), tagBuffer.end(), '\0');
                    strncpy_s(tagBuffer.data(), tagBuffer.size(), cachedText.c_str(), _TRUNCATE);
                    entity->AddPrefabOverride("Tags");
                    editorShell.MarkSceneDirty();
                }
            }
        }

        if (!entity->GetPrefabSource().empty())
        {
            ImGui::Separator();
            ImGui::Text("Prefab: %s", entity->GetPrefabSource().c_str());
            ImGui::TextDisabled(entity->IsPrefabInstanceRoot() ? "Instance Root" : "Nested Prefab Entity");
            ImGui::TextDisabled("Overrides: %zu", entity->GetPrefabOverrides().size());
            ImGui::BeginDisabled(!entity->IsPrefabInstanceRoot());
            if (ImGui::Button("Update From Prefab"))
            {
                std::string errorMessage;
                if (scene::Prefab::UpdateInstance(*entity, &errorMessage))
                {
                    editorShell.MarkSceneDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Updated prefab instance.");
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                    errorMessage.empty() ? "Failed to update prefab instance." : errorMessage);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply To Prefab"))
            {
                std::string errorMessage;
                if (scene::Prefab::ApplyInstanceToPrefab(*entity, &errorMessage))
                {
                    if (auto *project = editorShell.GetProject())
                    {
                        project->RefreshAssetRegistry();
                    }
                    if (auto *currentScene = core::Engine::GetInstance().GetScene())
                    {
                        scene::Prefab::UpdateInstances(*currentScene, entity->GetPrefabSource());
                    }
                    entity->ClearPrefabOverridesRecursive();
                    editorShell.MarkSceneDirty();
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Applied instance overrides to prefab.");
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                    errorMessage.empty() ? "Failed to apply prefab." : errorMessage);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Unpack"))
            {
                entity->ClearPrefabLinkRecursive();
                editorShell.MarkSceneDirty();
            }
            ImGui::EndDisabled();
        }

        {
            // Transform
            auto position = entity->GetPosition();
            auto rotation = entity->GetRotation();
            auto scale = entity->GetScale();
            const auto originalPosition = position;
            const auto originalRotation = rotation;
            const auto originalScale = scale;

            if (ImGui::CollapsingHeader("Transform"))
            {
                bool transformChanged = false;
                transformChanged |= ImGui::DragFloat3("Position", &position.x, 0.01f);
                transformChanged |= ImGui::DragFloat3("Rotation", &rotation.x, 0.1f);
                transformChanged |= ImGui::DragFloat3("Scale", &scale.x, 0.01f);

                entity->SetPosition(position);
                entity->SetRotation(rotation);
                entity->SetScale(scale);
                if (transformChanged)
                {
                    if (position != originalPosition)
                    {
                        entity->AddPrefabOverride("Transform.Position");
                    }
                    if (rotation != originalRotation)
                    {
                        entity->AddPrefabOverride("Transform.Rotation");
                    }
                    if (scale != originalScale)
                    {
                        entity->AddPrefabOverride("Transform.Scale");
                    }
                    editorShell.MarkSceneDirty();
                }
            }

            // Mesh Import UI (if entity has MeshComponent)
            if (auto *meshComponent = entity->GetComponent<PlutoGE::scene::MeshComponent>())
            {
                auto &engine = PlutoGE::core::Engine::GetInstance();
                RefreshMaterialSlotUsageSummaries(*meshComponent);
                const auto &materialSlotUsageSummaries = m_materialSlotUsageSummaries;

                ImGui::Separator();
                ImGui::Text("Static Mesh");
                const auto &meshAssetOptions = GetCachedAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Mesh);
                std::string meshPreview = meshComponent->GetMeshAssetReference().empty() ? "None" : meshComponent->GetMeshAssetReference();
                for (const auto &option : meshAssetOptions)
                {
                    if (option.reference == meshComponent->GetMeshAssetReference())
                    {
                        meshPreview = option.displayName;
                        break;
                    }
                }

                if (ImGui::BeginCombo("Mesh Asset", meshPreview.c_str()))
                {
                    for (const auto &option : meshAssetOptions)
                    {
                        const bool selected = option.reference == meshComponent->GetMeshAssetReference();
                        if (ImGui::Selectable(option.displayName.c_str(), selected))
                        {
                            if (AssignMeshAsset(*meshComponent, option.reference, engine))
                            {
                                editorShell.MarkSceneDirty();
                            }
                        }

                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (auto droppedMeshReference = AcceptDroppedMeshAssetReference())
                {
                    if (AssignMeshAsset(*meshComponent, *droppedMeshReference, engine))
                    {
                        editorShell.MarkSceneDirty();
                    }
                }

                if (meshComponent->GetMesh())
                {
                    const auto *mesh = meshComponent->GetMesh();
                    ImGui::TextDisabled("Mesh object: %zu draw submeshes | %zu material slots",
                                        mesh->GetSubmeshCount(),
                                        materialSlotUsageSummaries.size());
                    if (meshComponent->GetMeshAssetReference().empty())
                    {
                        ImGui::TextDisabled("Runtime mesh (not backed by an imported artifact)");
                    }
                    else
                    {
                        ImGui::TextWrapped("Artifact: %s", meshComponent->GetMeshAssetReference().c_str());
                        ImGui::TextWrapped("Model asset ID: %s", meshComponent->GetModelAssetId().c_str());
                        ImGui::Text("Local object ID: %llu", static_cast<unsigned long long>(meshComponent->GetModelObjectId()));
                        const auto &metadata = engine.GetAssetManager().GetMeshAssetMetadata(meshComponent->GetMeshAssetReference());
                        if (!metadata.sourceAssetReference.empty())
                        {
                            ImGui::TextWrapped("Imported from: %s", metadata.sourceAssetReference.c_str());
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Assign a generated .plutomesh asset from the content browser.");
                }

                glm::vec3 meshPositionOffset = meshComponent->GetMeshPositionOffset();
                glm::vec3 meshRotationOffset = meshComponent->GetMeshRotationOffset();
                bool meshOffsetChanged = ImGui::DragFloat3("Mesh Position Offset", &meshPositionOffset.x, 0.01f);
                meshOffsetChanged |= ImGui::DragFloat3("Mesh Rotation Offset", &meshRotationOffset.x, 0.1f);
                if (meshOffsetChanged)
                {
                    meshComponent->SetMeshPositionOffset(meshPositionOffset);
                    meshComponent->SetMeshRotationOffset(meshRotationOffset);
                    editorShell.MarkSceneDirty();
                }

                if (meshComponent->GetMesh() &&
                    meshComponent->GetMesh()->GetSubmeshCount() > 0 &&
                    ImGui::CollapsingHeader("Submesh Transform Offsets"))
                {
                    ImGui::TextWrapped("Offsets are applied only to the selected draw submesh. "
                                       "Skinned submeshes continue to share this entity's animation.");
                    for (size_t submeshIndex = 0; submeshIndex < meshComponent->GetMesh()->GetSubmeshCount(); ++submeshIndex)
                    {
                        const auto &submesh = meshComponent->GetMesh()->GetSubmesh(submeshIndex);
                        const std::string label = submesh.name.empty()
                                                      ? "Submesh " + std::to_string(submeshIndex)
                                                      : std::to_string(submeshIndex) + " - " + submesh.name;
                        ImGui::PushID(static_cast<int>(submeshIndex));
                        if (ImGui::TreeNode(label.c_str()))
                        {
                            glm::vec3 position = meshComponent->GetSubmeshPositionOffset(submeshIndex);
                            glm::vec3 rotation = meshComponent->GetSubmeshRotationOffset(submeshIndex);
                            bool changed = ImGui::DragFloat3("Position", &position.x, 0.01f);
                            changed |= ImGui::DragFloat3("Rotation", &rotation.x, 0.1f);
                            if (changed)
                            {
                                meshComponent->SetSubmeshPositionOffset(submeshIndex, position);
                                meshComponent->SetSubmeshRotationOffset(submeshIndex, rotation);
                                editorShell.MarkSceneDirty();
                            }
                            if (ImGui::Button("Reset"))
                            {
                                meshComponent->SetSubmeshPositionOffset(submeshIndex, glm::vec3(0.0f));
                                meshComponent->SetSubmeshRotationOffset(submeshIndex, glm::vec3(0.0f));
                                editorShell.MarkSceneDirty();
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::BeginDisabled(meshComponent->GetMeshAssetReference().empty());
                if (ImGui::Button("Reset Materials To Mesh Defaults"))
                {
                    if (AssignMeshAsset(*meshComponent, meshComponent->GetMeshAssetReference(), engine))
                    {
                        editorShell.MarkSceneDirty();
                    }
                }
                ImGui::EndDisabled();

                const auto &materialAssetOptions = GetCachedAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Material);
                const int isolatedSubmeshIndex = meshComponent->GetSubmeshIndex();
                const bool editingIsolatedSubmesh = isolatedSubmeshIndex >= 0 && meshComponent->GetMesh() &&
                                                    static_cast<size_t>(isolatedSubmeshIndex) < meshComponent->GetMesh()->GetSubmeshCount();
                if (meshComponent->GetMesh() && meshComponent->GetMesh()->GetSubmeshCount() > 1 && editingIsolatedSubmesh)
                {
                    const auto &submesh = meshComponent->GetMesh()->GetSubmesh(static_cast<size_t>(isolatedSubmeshIndex));
                    ImGui::TextDisabled("Editing submesh %d, material slot %u.", isolatedSubmeshIndex, submesh.materialIndex);
                }

                ImGui::Separator();
                if (ImGui::CollapsingHeader("Material Slots", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    std::vector<size_t> materialSlotIndices;
                    if (editingIsolatedSubmesh)
                    {
                        materialSlotIndices.push_back(static_cast<size_t>(meshComponent->GetMesh()->GetSubmesh(static_cast<size_t>(isolatedSubmeshIndex)).materialIndex));
                    }
                    else
                    {
                        const size_t materialSlotCount = materialSlotUsageSummaries.size();
                        materialSlotIndices.reserve(materialSlotCount);
                        for (size_t materialSlotIndex = 0; materialSlotIndex < materialSlotCount; ++materialSlotIndex)
                        {
                            materialSlotIndices.push_back(materialSlotIndex);
                        }
                    }

                    const auto &meshDefaultMaterialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(meshComponent->GetMeshAssetReference());
                    for (size_t materialSlotListIndex = 0; materialSlotListIndex < materialSlotIndices.size(); ++materialSlotListIndex)
                    {
                        const size_t materialSlotIndex = materialSlotIndices[materialSlotListIndex];
                        ImGui::PushID(static_cast<int>(materialSlotIndex));
                        ImGui::Text("Slot %zu", materialSlotIndex);
                        ImGui::SameLine();
                        ImGui::TextDisabled("(drop a material asset here)");
                        static const std::string emptyUsageSummary;
                        const std::string &slotUsageSummary = materialSlotIndex < materialSlotUsageSummaries.size()
                                                                  ? materialSlotUsageSummaries[materialSlotIndex]
                                                                  : emptyUsageSummary;
                        if (!slotUsageSummary.empty())
                        {
                            ImGui::TextDisabled("%s", slotUsageSummary.c_str());
                        }

                        const std::string materialAssetReference = meshComponent->GetMaterialAssetForMaterialSlot(materialSlotIndex);
                        const std::string materialPreview = GetAssetReferencePreview(materialAssetOptions, materialAssetReference, "Inline / Imported");
                        if (ImGui::BeginCombo("Material Asset", materialPreview.c_str()))
                        {
                            for (const auto &option : materialAssetOptions)
                            {
                                const bool selected = option.reference == materialAssetReference;
                                if (ImGui::Selectable(option.displayName.c_str(), selected))
                                {
                                    if (AssignMaterialAssetToSlot(*meshComponent, materialSlotIndex, option.reference, engine))
                                    {
                                        editorShell.MarkSceneDirty();
                                    }
                                }

                                if (selected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::BeginDisabled(materialAssetReference.empty() ||
                                             assets::Project::IsEngineAssetReference(materialAssetReference));
                        if (ImGui::Button("Open Material Editor"))
                        {
                            editorShell.OpenMaterialAsset(materialAssetReference);
                        }
                        ImGui::EndDisabled();

                        if (auto droppedMaterialReference = AcceptDroppedMaterialAssetReference())
                        {
                            if (AssignMaterialAssetToSlot(*meshComponent, materialSlotIndex, *droppedMaterialReference, engine))
                            {
                                editorShell.MarkSceneDirty();
                            }
                        }
                        const bool hasMeshDefaultMaterial = materialSlotIndex < meshDefaultMaterialReferences.size() &&
                                                            !meshDefaultMaterialReferences[materialSlotIndex].empty();
                        ImGui::BeginDisabled(!hasMeshDefaultMaterial || materialAssetReference == meshDefaultMaterialReferences[materialSlotIndex]);
                        if (ImGui::Button("Use Mesh Default"))
                        {
                            if (AssignMaterialAssetToSlot(*meshComponent, materialSlotIndex, meshDefaultMaterialReferences[materialSlotIndex], engine))
                            {
                                editorShell.MarkSceneDirty();
                            }
                        }
                        ImGui::EndDisabled();

                        if (materialSlotListIndex + 1 < materialSlotIndices.size())
                        {
                            ImGui::Spacing();
                        }
                        ImGui::PopID();
                    }
                }

                if (meshComponent->GetMesh() && meshComponent->GetMesh()->GetSubmeshCount() > 1)
                {
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader(editingIsolatedSubmesh ? "Selected Submesh Material" : "Submesh Materials",
                                                editingIsolatedSubmesh ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                    {
                        const auto *inspectedMesh = meshComponent->GetMesh();
                        if (m_inspectedSubmeshList != inspectedMesh)
                        {
                            m_inspectedSubmeshList = inspectedMesh;
                            m_selectedSubmeshIndex = inspectedMesh->GetSubmeshCount() > 0 ? 0 : -1;
                            m_submeshFilter.fill('\0');
                            m_appliedSubmeshFilter.clear();
                            RefreshFilteredSubmeshIndices(*inspectedMesh);
                        }

                        if (editingIsolatedSubmesh)
                        {
                            m_selectedSubmeshIndex = isolatedSubmeshIndex;
                        }
                        else
                        {
                            const std::string_view filter(m_submeshFilter.data());
                            const bool filterChanged = ImGui::InputTextWithHint("##SubmeshFilter", "Filter submeshes...", m_submeshFilter.data(), m_submeshFilter.size());
                            if (filterChanged || m_appliedSubmeshFilter != filter)
                            {
                                RefreshFilteredSubmeshIndices(*inspectedMesh);
                            }

                            ImGui::BeginChild("SubmeshList", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders);
                            ImGuiListClipper clipper;
                            clipper.Begin(static_cast<int>(m_filteredSubmeshIndices.size()));
                            while (clipper.Step())
                            {
                                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                                {
                                    const auto candidateIndex = m_filteredSubmeshIndices[static_cast<std::size_t>(row)];
                                    const auto &candidate = inspectedMesh->GetSubmesh(candidateIndex);
                                    const std::string candidateLabel = candidate.name.empty()
                                                                           ? "Submesh " + std::to_string(candidateIndex)
                                                                           : candidate.name;
                                    ImGui::PushID(static_cast<int>(candidateIndex));
                                    if (ImGui::Selectable(candidateLabel.c_str(), m_selectedSubmeshIndex == static_cast<int>(candidateIndex)))
                                    {
                                        m_selectedSubmeshIndex = static_cast<int>(candidateIndex);
                                    }
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndChild();
                        }

                        if (m_selectedSubmeshIndex >= 0 &&
                            static_cast<std::size_t>(m_selectedSubmeshIndex) < inspectedMesh->GetSubmeshCount())
                        {
                            const size_t submeshIndex = static_cast<std::size_t>(m_selectedSubmeshIndex);
                            const auto &submesh = meshComponent->GetMesh()->GetSubmesh(submeshIndex);
                            auto *material = meshComponent->GetMaterialForSubmesh(submeshIndex);

                            ImGui::PushID(static_cast<int>(submeshIndex));
                            const std::string submeshLabel = submesh.name.empty()
                                                                 ? std::string("Submesh ") + std::to_string(submeshIndex)
                                                                 : submesh.name;
                            ImGui::SeparatorText(submeshLabel.c_str());
                            {
                                ImGui::Text("Material Slot: %u", submesh.materialIndex);
                                ImGui::Text("Indices: %u", submesh.indexCount);

                                if (material)
                                {
                                    const std::string submeshMaterialAssetReference = meshComponent->GetMaterialAssetForSubmesh(submeshIndex);
                                    const std::string inheritedMaterialAssetReference = meshComponent->GetMaterialAssetForMaterialSlot(submesh.materialIndex);
                                    const std::string activeMaterialAssetReference = submeshMaterialAssetReference.empty() ? inheritedMaterialAssetReference : submeshMaterialAssetReference;
                                    const std::string materialPreview = GetAssetReferencePreview(
                                        materialAssetOptions,
                                        activeMaterialAssetReference,
                                        submeshMaterialAssetReference.empty() ? "Inherits Material Slot" : "Inline Override");

                                    if (ImGui::BeginCombo("Material Asset", materialPreview.c_str()))
                                    {
                                        for (const auto &option : materialAssetOptions)
                                        {
                                            const bool selected = option.reference == activeMaterialAssetReference;
                                            if (ImGui::Selectable(option.displayName.c_str(), selected))
                                            {
                                                if (AssignMaterialAssetToSubmesh(*meshComponent, submeshIndex, option.reference, engine))
                                                {
                                                    editorShell.MarkSceneDirty();
                                                    material = meshComponent->GetMaterialForSubmesh(submeshIndex);
                                                }
                                            }

                                            if (selected)
                                            {
                                                ImGui::SetItemDefaultFocus();
                                            }
                                        }
                                        ImGui::EndCombo();
                                    }
                                    ImGui::BeginDisabled(activeMaterialAssetReference.empty() ||
                                                         assets::Project::IsEngineAssetReference(activeMaterialAssetReference));
                                    if (ImGui::Button("Open Material Editor"))
                                    {
                                        editorShell.OpenMaterialAsset(activeMaterialAssetReference);
                                    }
                                    ImGui::EndDisabled();

                                    if (auto droppedMaterialReference = AcceptDroppedMaterialAssetReference())
                                    {
                                        if (AssignMaterialAssetToSubmesh(*meshComponent, submeshIndex, *droppedMaterialReference, engine))
                                        {
                                            editorShell.MarkSceneDirty();
                                            material = meshComponent->GetMaterialForSubmesh(submeshIndex);
                                        }
                                    }

                                    const bool materialUsesAssetReference =
                                        !meshComponent->GetMaterialAssetForSubmesh(submeshIndex).empty() ||
                                        !meshComponent->GetMaterialAssetForMaterialSlot(submesh.materialIndex).empty();
                                    if (materialUsesAssetReference)
                                    {
                                        ImGui::TextDisabled("Using shared material asset. Make it unique to edit inline values.");
                                        ImGui::BeginDisabled();
                                    }

                                    const auto &materialConfig = material->GetConfig();
                                    float color[4] = {
                                        materialConfig.color.r,
                                        materialConfig.color.g,
                                        materialConfig.color.b,
                                        materialConfig.color.a,
                                    };
                                    if (ImGui::ColorEdit4("Color", color))
                                    {
                                        material->SetColor(glm::vec4(color[0], color[1], color[2], color[3]));
                                        editorShell.MarkSceneDirty();
                                    }

                                    float metallic = materialConfig.metallic;
                                    if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f))
                                    {
                                        material->SetMetallic(metallic);
                                        editorShell.MarkSceneDirty();
                                    }

                                    float roughness = materialConfig.roughness;
                                    if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.04f, 1.0f))
                                    {
                                        material->SetRoughness(roughness);
                                        editorShell.MarkSceneDirty();
                                    }

                                    float emission[3] = {
                                        materialConfig.emission.r,
                                        materialConfig.emission.g,
                                        materialConfig.emission.b,
                                    };
                                    if (ImGui::ColorEdit3("Emission", emission, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
                                    {
                                        material->SetEmission(glm::vec3(
                                            emission[0] > 0.0f ? emission[0] : 0.0f,
                                            emission[1] > 0.0f ? emission[1] : 0.0f,
                                            emission[2] > 0.0f ? emission[2] : 0.0f));
                                        editorShell.MarkSceneDirty();
                                    }

                                    bool flipNormalY = materialConfig.flipNormalY;
                                    if (ImGui::Checkbox("Flip Normal Y", &flipNormalY))
                                    {
                                        material->SetFlipNormalY(flipNormalY);
                                        editorShell.MarkSceneDirty();
                                    }

                                    ImGui::Text("Textures: Albedo %s | Normal %s | Metallic/Roughness %s",
                                                materialConfig.albedoTexture ? "yes" : "no",
                                                materialConfig.normalTexture ? "yes" : "no",
                                                materialConfig.metallicTexture || materialConfig.roughnessTexture ? "yes" : "no");

                                    auto &lightmapPathBuffer = GetLightmapPathBuffer(*entity, static_cast<uint32_t>(submeshIndex));
                                    ImGui::InputText("Lightmap Path", lightmapPathBuffer.data(), lightmapPathBuffer.size());
                                    ImGui::SameLine();
                                    if (ImGui::Button("...##Lightmap"))
                                    {
#ifdef _WIN32
                                        OPENFILENAMEA ofn = {};
                                        char fileName[MAX_PATH] = "";
                                        ofn.lStructSize = sizeof(ofn);
                                        ofn.hwndOwner = nullptr;
                                        ofn.lpstrFilter = "Texture Files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr\0All Files\0*.*\0";
                                        ofn.lpstrFile = fileName;
                                        ofn.nMaxFile = MAX_PATH;
                                        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                                        if (GetOpenFileNameA(&ofn))
                                        {
                                            strncpy_s(lightmapPathBuffer.data(), lightmapPathBuffer.size(), fileName, _TRUNCATE);
                                        }
#endif
                                    }

                                    ImGui::BeginDisabled(std::strlen(lightmapPathBuffer.data()) == 0);
                                    if (ImGui::Button("Load Lightmap"))
                                    {
                                        auto *lightmapTexture = engine.GetTextureManager().LoadLightmapFromFile(lightmapPathBuffer.data());
                                        material->SetLightmapTexture(lightmapTexture);
                                    }
                                    ImGui::EndDisabled();

                                    ImGui::SameLine();
                                    if (ImGui::Button("Clear Lightmap"))
                                    {
                                        material->SetLightmapTexture(nullptr);
                                    }

                                    ImGui::Text("Baked Lightmap: %s", materialConfig.lightmapTexture ? "yes" : "no");

                                    if (meshComponent->IsStatic() && materialConfig.lightmapTexture && meshComponent->GetMesh() && !meshComponent->GetMesh()->HasUsableLightmapUvsForSubmesh(submeshIndex))
                                    {
                                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Baking is using UV0 because TEXCOORD_1 / UV2 is missing. UV2 is still preferred to avoid overlap artifacts.");
                                    }

                                    if (materialUsesAssetReference)
                                    {
                                        ImGui::EndDisabled();
                                    }

                                    if (ImGui::Button("Make Unique Override"))
                                    {
                                        auto *overrideMaterial = new render::Material(material->GetConfig());
                                        meshComponent->SetMaterialForSubmesh(submeshIndex, overrideMaterial);
                                        meshComponent->SetMaterialAssetForSubmesh(submeshIndex, {});
                                        editorShell.MarkSceneDirty();
                                    }
                                }
                                else
                                {
                                    ImGui::Text("No material assigned.");
                                }
                            }
                            ImGui::PopID();
                        }
                    }
                }
            }

            if (auto *animationComponent = entity->GetComponent<PlutoGE::scene::AnimationComponent>())
            {
                ImGui::Separator();
                ImGui::Text("Animation");
                auto animationAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Animation);
                auto animationClipAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::AnimationClip);
                animationAssetOptions.insert(animationAssetOptions.end(), animationClipAssetOptions.begin(), animationClipAssetOptions.end());
                std::sort(animationAssetOptions.begin(), animationAssetOptions.end(),
                          [](const AssetReferenceOption &left, const AssetReferenceOption &right)
                          {
                              return left.displayName < right.displayName;
                          });
                std::string animationPreview = GetAssetReferencePreview(animationAssetOptions,
                                                                        animationComponent->GetSourceAnimationPath(),
                                                                        "None");
                if (ImGui::BeginCombo("Animation Asset", animationPreview.c_str()))
                {
                    for (const auto &option : animationAssetOptions)
                    {
                        const bool selected = option.reference == animationComponent->GetSourceAnimationPath();
                        if (ImGui::Selectable(option.displayName.c_str(), selected))
                        {
                            if (AssignAnimationAsset(*animationComponent, option.reference))
                            {
                                entity->AddPrefabOverride("Component:AnimationComponent:SourceAnimation");
                                editorShell.MarkSceneDirty();
                            }
                        }

                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (auto droppedAnimationReference = AcceptDroppedAnimationAssetReference())
                {
                    if (AssignAnimationAsset(*animationComponent, *droppedAnimationReference))
                    {
                        entity->AddPrefabOverride("Component:AnimationComponent:SourceAnimation");
                        editorShell.MarkSceneDirty();
                    }
                }

                const auto animationGraphAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::AnimationGraph);
                std::string animationGraphPreview = GetAssetReferencePreview(animationGraphAssetOptions,
                                                                             animationComponent->GetAnimationGraphAssetReference(),
                                                                             "None");
                if (ImGui::BeginCombo("Animation Graph", animationGraphPreview.c_str()))
                {
                    for (const auto &option : animationGraphAssetOptions)
                    {
                        const bool selected = option.reference == animationComponent->GetAnimationGraphAssetReference();
                        if (ImGui::Selectable(option.displayName.c_str(), selected))
                        {
                            if (AssignAnimationGraphAsset(*animationComponent, option.reference))
                            {
                                entity->AddPrefabOverride("Component:AnimationComponent:AnimationGraph");
                                editorShell.MarkSceneDirty();
                            }
                        }

                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (auto droppedAnimationGraphReference = AcceptDroppedAnimationGraphAssetReference())
                {
                    if (AssignAnimationGraphAsset(*animationComponent, *droppedAnimationGraphReference))
                    {
                        entity->AddPrefabOverride("Component:AnimationComponent:AnimationGraph");
                        editorShell.MarkSceneDirty();
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(animationComponent->GetAnimationGraphAssetReference().empty());
                if (ImGui::Button("Clear##AnimationGraph"))
                {
                    animationComponent->SetAnimationGraphAssetReference({});
                    entity->AddPrefabOverride("Component:AnimationComponent:AnimationGraph");
                    editorShell.MarkSceneDirty();
                }
                ImGui::EndDisabled();

                ImGui::TextDisabled("Clips: %d", animationComponent->GetClipCount());
                if (animationComponent->GetClipCount() > 0)
                {
                    std::vector<std::string> clipNames;
                    clipNames.reserve(animationComponent->GetClips().size());
                    for (const auto &clip : animationComponent->GetClips())
                    {
                        clipNames.push_back(clip.name.empty() ? "Unnamed" : clip.name);
                    }

                    int currentClipIndex = animationComponent->GetCurrentClipIndex();
                    const char *currentClipLabel = currentClipIndex >= 0 && currentClipIndex < static_cast<int>(clipNames.size())
                                                       ? clipNames[static_cast<size_t>(currentClipIndex)].c_str()
                                                       : "None";
                    if (ImGui::BeginCombo("Clip", currentClipLabel))
                    {
                        for (int clipIndex = 0; clipIndex < static_cast<int>(clipNames.size()); ++clipIndex)
                        {
                            const bool selected = currentClipIndex == clipIndex;
                            if (ImGui::Selectable(clipNames[static_cast<size_t>(clipIndex)].c_str(), selected))
                            {
                                animationComponent->SetCurrentClipIndex(clipIndex);
                                entity->AddPrefabOverride("Component:AnimationComponent:CurrentClipIndex");
                                editorShell.MarkSceneDirty();
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                if (ImGui::TreeNode("Animation Graph"))
                {
                    using AnimationParameterType = scene::AnimationComponent::AnimationParameterType;
                    using AnimationConditionMode = scene::AnimationComponent::AnimationConditionMode;

                    auto &states = animationComponent->GetGraphStates();
                    auto &parameters = animationComponent->GetGraphParameters();
                    bool graphChanged = false;

                    if (!states.empty())
                    {
                        int defaultStateIndex = animationComponent->GetDefaultStateIndex();
                        const char *defaultStateLabel = defaultStateIndex >= 0 && defaultStateIndex < static_cast<int>(states.size())
                                                            ? states[static_cast<size_t>(defaultStateIndex)].name.c_str()
                                                            : "None";
                        if (ImGui::BeginCombo("Default State", defaultStateLabel))
                        {
                            for (int stateIndex = 0; stateIndex < static_cast<int>(states.size()); ++stateIndex)
                            {
                                const bool selected = defaultStateIndex == stateIndex;
                                const std::string label = states[static_cast<size_t>(stateIndex)].name.empty()
                                                              ? "State " + std::to_string(stateIndex)
                                                              : states[static_cast<size_t>(stateIndex)].name;
                                if (ImGui::Selectable(label.c_str(), selected))
                                {
                                    animationComponent->SetDefaultStateIndex(stateIndex);
                                    graphChanged = true;
                                }
                                if (selected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }

                    if (ImGui::Button("Add State"))
                    {
                        animationComponent->AddState("State " + std::to_string(states.size()), animationComponent->GetCurrentClipIndex());
                        graphChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Float"))
                    {
                        animationComponent->AddParameter("Speed", AnimationParameterType::Float);
                        graphChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Bool"))
                    {
                        animationComponent->AddParameter("IsWalking", AnimationParameterType::Bool);
                        graphChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Trigger"))
                    {
                        animationComponent->AddParameter("Jump", AnimationParameterType::Trigger);
                        graphChanged = true;
                    }

                    if (ImGui::TreeNode("Parameters"))
                    {
                        constexpr const char *parameterTypeLabels[] = {"Float", "Int", "Bool", "Trigger"};
                        int parameterToRemove = -1;
                        for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex)
                        {
                            auto &parameter = parameters[static_cast<size_t>(parameterIndex)];
                            ImGui::PushID(parameterIndex);
                            char nameBuffer[128]{};
                            std::strncpy(nameBuffer, parameter.name.c_str(), sizeof(nameBuffer) - 1);
                            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                            {
                                parameter.name = nameBuffer;
                                graphChanged = true;
                            }

                            int typeIndex = static_cast<int>(parameter.type);
                            if (ImGui::Combo("Type", &typeIndex, parameterTypeLabels, IM_ARRAYSIZE(parameterTypeLabels)))
                            {
                                parameter.type = static_cast<AnimationParameterType>(typeIndex);
                                graphChanged = true;
                            }

                            if (parameter.type == AnimationParameterType::Float)
                            {
                                graphChanged |= ImGui::DragFloat("Value", &parameter.floatValue, 0.01f);
                            }
                            else if (parameter.type == AnimationParameterType::Int)
                            {
                                graphChanged |= ImGui::DragInt("Value", &parameter.intValue);
                            }
                            else
                            {
                                graphChanged |= ImGui::Checkbox("Value", &parameter.boolValue);
                            }

                            if (ImGui::Button("Remove Parameter"))
                            {
                                parameterToRemove = parameterIndex;
                            }
                            ImGui::Separator();
                            ImGui::PopID();
                        }

                        if (parameterToRemove >= 0)
                        {
                            animationComponent->RemoveParameter(parameterToRemove);
                            graphChanged = true;
                        }
                        ImGui::TreePop();
                    }

                    if (ImGui::TreeNode("States"))
                    {
                        int stateToRemove = -1;
                        for (int stateIndex = 0; stateIndex < static_cast<int>(states.size()); ++stateIndex)
                        {
                            auto &state = states[static_cast<size_t>(stateIndex)];
                            ImGui::PushID(stateIndex);
                            const std::string stateLabel = state.name.empty() ? "State " + std::to_string(stateIndex) : state.name;
                            if (ImGui::TreeNode(stateLabel.c_str()))
                            {
                                char nameBuffer[128]{};
                                std::strncpy(nameBuffer, state.name.c_str(), sizeof(nameBuffer) - 1);
                                if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                                {
                                    state.name = nameBuffer;
                                    graphChanged = true;
                                }

                                if (animationComponent->GetClipCount() > 0)
                                {
                                    state.clipIndex = std::clamp(state.clipIndex, 0, animationComponent->GetClipCount() - 1);
                                    const auto &clips = animationComponent->GetClips();
                                    const char *stateClipLabel = clips[static_cast<size_t>(state.clipIndex)].name.empty()
                                                                     ? "Unnamed"
                                                                     : clips[static_cast<size_t>(state.clipIndex)].name.c_str();
                                    if (ImGui::BeginCombo("Clip", stateClipLabel))
                                    {
                                        for (int clipIndex = 0; clipIndex < animationComponent->GetClipCount(); ++clipIndex)
                                        {
                                            const auto &clipName = clips[static_cast<size_t>(clipIndex)].name;
                                            const bool selected = state.clipIndex == clipIndex;
                                            if (ImGui::Selectable(clipName.empty() ? "Unnamed" : clipName.c_str(), selected))
                                            {
                                                state.clipIndex = clipIndex;
                                                graphChanged = true;
                                            }
                                            if (selected)
                                            {
                                                ImGui::SetItemDefaultFocus();
                                            }
                                        }
                                        ImGui::EndCombo();
                                    }
                                }

                                graphChanged |= ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.0f, 10.0f);
                                graphChanged |= ImGui::Checkbox("Loop", &state.loop);

                                if (ImGui::Button("Play State"))
                                {
                                    animationComponent->SetCurrentStateIndex(stateIndex);
                                    animationComponent->Play();
                                    graphChanged = true;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Make Default"))
                                {
                                    animationComponent->SetDefaultStateIndex(stateIndex);
                                    graphChanged = true;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Remove State"))
                                {
                                    stateToRemove = stateIndex;
                                }

                                if (states.size() > 1)
                                {
                                    static int transitionTargetIndex = 0;
                                    transitionTargetIndex = std::clamp(transitionTargetIndex, 0, static_cast<int>(states.size()) - 1);
                                    ImGui::SetNextItemWidth(180.0f);
                                    if (ImGui::BeginCombo("Transition Target", states[static_cast<size_t>(transitionTargetIndex)].name.c_str()))
                                    {
                                        for (int targetIndex = 0; targetIndex < static_cast<int>(states.size()); ++targetIndex)
                                        {
                                            if (targetIndex == stateIndex)
                                            {
                                                continue;
                                            }
                                            const bool selected = transitionTargetIndex == targetIndex;
                                            if (ImGui::Selectable(states[static_cast<size_t>(targetIndex)].name.c_str(), selected))
                                            {
                                                transitionTargetIndex = targetIndex;
                                            }
                                        }
                                        ImGui::EndCombo();
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button("Add Transition") && animationComponent->AddTransition(stateIndex, transitionTargetIndex))
                                    {
                                        graphChanged = true;
                                    }
                                }

                                int transitionToRemove = -1;
                                for (int transitionIndex = 0; transitionIndex < static_cast<int>(state.transitions.size()); ++transitionIndex)
                                {
                                    auto &transition = state.transitions[static_cast<size_t>(transitionIndex)];
                                    ImGui::PushID(transitionIndex);
                                    const std::string transitionLabel = "Transition " + std::to_string(transitionIndex);
                                    if (ImGui::TreeNode(transitionLabel.c_str()))
                                    {
                                        if (!states.empty())
                                        {
                                            transition.destinationStateIndex = std::clamp(transition.destinationStateIndex, 0, static_cast<int>(states.size()) - 1);
                                            if (ImGui::BeginCombo("Destination", states[static_cast<size_t>(transition.destinationStateIndex)].name.c_str()))
                                            {
                                                for (int targetIndex = 0; targetIndex < static_cast<int>(states.size()); ++targetIndex)
                                                {
                                                    const bool selected = transition.destinationStateIndex == targetIndex;
                                                    if (ImGui::Selectable(states[static_cast<size_t>(targetIndex)].name.c_str(), selected))
                                                    {
                                                        transition.destinationStateIndex = targetIndex;
                                                        graphChanged = true;
                                                    }
                                                }
                                                ImGui::EndCombo();
                                            }
                                        }

                                        graphChanged |= ImGui::DragFloat("Blend Duration", &transition.duration, 0.01f, 0.0f, 10.0f);
                                        graphChanged |= ImGui::Checkbox("Has Exit Time", &transition.hasExitTime);
                                        graphChanged |= ImGui::DragFloat("Exit Time", &transition.exitTime, 0.01f, 0.0f, 10.0f);

                                        if (ImGui::Button("Add Condition") && !parameters.empty())
                                        {
                                            scene::AnimationComponent::AnimationCondition condition;
                                            condition.parameterName = parameters.front().name;
                                            transition.conditions.push_back(std::move(condition));
                                            graphChanged = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::Button("Remove Transition"))
                                        {
                                            transitionToRemove = transitionIndex;
                                        }

                                        constexpr const char *conditionModeLabels[] = {"If", "IfNot", "Greater", "Less", "Equals", "NotEqual"};
                                        int conditionToRemove = -1;
                                        for (int conditionIndex = 0; conditionIndex < static_cast<int>(transition.conditions.size()); ++conditionIndex)
                                        {
                                            auto &condition = transition.conditions[static_cast<size_t>(conditionIndex)];
                                            ImGui::PushID(conditionIndex);
                                            if (!parameters.empty())
                                            {
                                                int selectedParameterIndex = (std::max)(0, animationComponent->FindParameterIndex(condition.parameterName));
                                                selectedParameterIndex = std::clamp(selectedParameterIndex, 0, static_cast<int>(parameters.size()) - 1);
                                                if (ImGui::BeginCombo("Parameter", parameters[static_cast<size_t>(selectedParameterIndex)].name.c_str()))
                                                {
                                                    for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex)
                                                    {
                                                        const bool selected = selectedParameterIndex == parameterIndex;
                                                        if (ImGui::Selectable(parameters[static_cast<size_t>(parameterIndex)].name.c_str(), selected))
                                                        {
                                                            condition.parameterName = parameters[static_cast<size_t>(parameterIndex)].name;
                                                            graphChanged = true;
                                                        }
                                                    }
                                                    ImGui::EndCombo();
                                                }
                                            }

                                            int modeIndex = static_cast<int>(condition.mode);
                                            if (ImGui::Combo("Mode", &modeIndex, conditionModeLabels, IM_ARRAYSIZE(conditionModeLabels)))
                                            {
                                                condition.mode = static_cast<AnimationConditionMode>(modeIndex);
                                                graphChanged = true;
                                            }
                                            graphChanged |= ImGui::DragFloat("Threshold", &condition.threshold, 0.01f);
                                            if (ImGui::Button("Remove Condition"))
                                            {
                                                conditionToRemove = conditionIndex;
                                            }
                                            ImGui::Separator();
                                            ImGui::PopID();
                                        }

                                        if (conditionToRemove >= 0)
                                        {
                                            transition.conditions.erase(transition.conditions.begin() + conditionToRemove);
                                            graphChanged = true;
                                        }

                                        ImGui::TreePop();
                                    }
                                    ImGui::PopID();
                                }

                                if (transitionToRemove >= 0)
                                {
                                    animationComponent->RemoveTransition(stateIndex, transitionToRemove);
                                    graphChanged = true;
                                }

                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }

                        if (stateToRemove >= 0)
                        {
                            animationComponent->RemoveState(stateToRemove);
                            graphChanged = true;
                        }
                        ImGui::TreePop();
                    }

                    if (graphChanged)
                    {
                        entity->AddPrefabOverride("Component:AnimationComponent:Graph");
                        editorShell.MarkSceneDirty();
                    }

                    ImGui::TreePop();
                }
            }

            // Components
            if (ImGui::CollapsingHeader("Components"))
            {
                if (ImGui::Button("Add Component...", ImVec2(-1.0f, 0.0f)))
                {
                    ImGui::OpenPopup("AddComponentMenu");
                }

                if (ImGui::BeginPopup("AddComponentMenu"))
                {
                    const auto selectedComponentType = RenderAddComponentMenu(*entity);
                    if (selectedComponentType)
                    {
                        const scene::EntityID entityId = entity->GetID();
                        editorShell.ExecuteSceneEdit("Add Component",
                                                     [entityId, componentType = *selectedComponentType]()
                                                     {
                                                         auto *currentScene = core::Engine::GetInstance().GetScene();
                                                         if (auto *target = currentScene ? currentScene->FindEntityByID(entityId) : nullptr)
                                                         {
                                                             AddComponentToEntity(*target, componentType);
                                                         }
                                                     });
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                int componentIndex = 0;
                scene::Component *componentToRemove = nullptr;
                for (const auto &component : entity->GetComponentBuckets())
                {
                    for (auto *componentPtr : component)
                    {
                        ImGui::PushID(componentIndex++);
                        const bool isComponentOpen = ImGui::TreeNodeEx(GetComponentDisplayName(*componentPtr), ImGuiTreeNodeFlags_DefaultOpen);
                        ImGui::SameLine();
                        if (ImGui::Button("Remove"))
                        {
                            componentToRemove = componentPtr;
                        }

                        if (!isComponentOpen)
                        {
                            ImGui::PopID();
                            if (componentToRemove)
                            {
                                break;
                            }
                            continue;
                        }

                        bool isEnabled = componentPtr->IsEnabled();
                        if (ImGui::Checkbox("Enabled", &isEnabled))
                        {
                            componentPtr->SetEnabled(isEnabled);
                            const auto componentTypeName = GetComponentPrefabTypeName(*componentPtr);
                            if (!componentTypeName.empty())
                            {
                                entity->AddPrefabOverride("Component:" + componentTypeName + ":Enabled");
                            }
                            editorShell.MarkSceneDirty();
                        }

                        std::vector<scene::Property> properties;
                        bool propertiesProvided = false;
                        bool propertiesChanged = false;

                        if (auto *cameraComponent = dynamic_cast<scene::CameraComponent *>(componentPtr))
                        {
                            bool isMainCamera = cameraComponent->IsMainCamera();
                            if (ImGui::Checkbox("Main Camera", &isMainCamera))
                            {
                                SetSceneMainCamera(entity->GetScene(), cameraComponent, isMainCamera);
                            }

                            RenderCameraPostProcessEditor(*cameraComponent);
                        }
                        else if (auto *scriptComponent = dynamic_cast<scene::ScriptComponent *>(componentPtr))
                        {
                            if (RenderScriptComponentEditor(*scriptComponent, *entity))
                            {
                                entity->AddPrefabOverride("Component:ScriptComponent:Source");
                                editorShell.MarkSceneDirty();
                            }
                        }
                        else if (dynamic_cast<scene::LightComponent *>(componentPtr))
                        {
                            ImGui::TextDisabled("Only lights marked Static contribute to Bake Scene.");
                        }
                        else if (auto *iblCaptureComponent = dynamic_cast<scene::IblCaptureComponent *>(componentPtr))
                        {
                            properties = iblCaptureComponent->SerializeEditableProperties();
                            propertiesProvided = true;
                            const bool hasCapture = iblCaptureComponent->GetCaptureTexture() != nullptr;
                            ImGui::Text("Captured HDRI: %s", hasCapture ? "ready" : "not captured");
                            if (ImGui::Button("Capture Scene"))
                            {
                                EditorShell::GetInstance().RequestIblCapture(iblCaptureComponent);
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("Mark Dirty"))
                            {
                                iblCaptureComponent->MarkDirty();
                            }
                        }
                        else if (auto *navigationMesh = dynamic_cast<scene::NavigationMeshComponent *>(componentPtr))
                        {
                            properties = navigationMesh->Serialize();
                            std::erase_if(properties, [](const scene::Property &property)
                                          { return property.name == "Baked"; });
                            propertiesProvided = true;

                            const auto &navigation = navigationMesh->GetNavigation();
                            ImGui::Text("Status: %s", navigation.IsBaked() ? "Baked" : "Not baked");
                            ImGui::Text("Grid: %d x %d", navigation.GetWidth(), navigation.GetDepth());
                            ImGui::Text("Walkable cells: %zu", navigation.GetDebugWalkablePoints().size());
                            if (ImGui::Button("Bake"))
                            {
                                const bool baked = navigationMesh->Bake();
                                editorShell.SetStatusMessage(baked
                                    ? "Navigation mesh baked: " + std::to_string(navigationMesh->GetNavigation().GetDebugWalkablePoints().size()) + " walkable cells."
                                    : "Navigation mesh bake produced no walkable cells. Check its bounds and scene colliders.");
                                entity->AddPrefabOverride("Component:NavigationMeshComponent:Baked");
                                editorShell.MarkSceneDirty();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Clear"))
                            {
                                navigationMesh->Clear();
                                editorShell.SetStatusMessage("Navigation mesh cleared.");
                                entity->AddPrefabOverride("Component:NavigationMeshComponent:Baked");
                                editorShell.MarkSceneDirty();
                            }
                            ImGui::SeparatorText("Bake Settings (World Space)");
                        }
                        else if (auto *canvas = dynamic_cast<scene::CanvasComponent *>(componentPtr))
                        {
                            properties = canvas->Serialize();
                            std::erase_if(properties, [canvas](const scene::Property &property)
                            {
                                if (property.name == "DocumentPath") return true;
                                if (canvas->GetBackend() != scene::UIRenderBackend::RmlUi && property.name == "ContentSource")
                                    return true;
                                const bool simpleText = canvas->GetBackend() == scene::UIRenderBackend::RmlUi &&
                                                        canvas->GetContentSource() == scene::RmlUiContentSource::Text;
                                if (simpleText &&
                                    (property.name == "RenderMode" || property.name == "ScaleMode" ||
                                     property.name == "ScaleFactor" || property.name == "ReferenceResolution" ||
                                     property.name == "MatchWidthOrHeight" || property.name == "ScreenMatchMode" ||
                                     property.name == "WorldSizeMode" || property.name == "FaceCamera" ||
                                     property.name == "SortingOrder"))
                                    return true;
                                const bool worldMode = canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpaceOverlay ||
                                                       canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpace;
                                if (!worldMode && (property.name == "WorldSizeMode" || property.name == "FaceCamera"))
                                    return true;
                                return false;
                            });
                            propertiesProvided = true;

                            if (canvas->GetBackend() == scene::UIRenderBackend::RmlUi)
                            {
                                ImGui::TextWrapped(canvas->GetContentSource() == scene::RmlUiContentSource::Text
                                                       ? "RmlUi text canvas. Text styling comes from UI Text; World Space size comes from entity X/Y scale."
                                                       : "RmlUi document canvas. Layout and controls are authored in the RML/RCSS asset.");
                                if (canvas->GetContentSource() == scene::RmlUiContentSource::Text)
                                {
                                    const auto currentMode = canvas->GetRenderMode();
                                    const char *preview = currentMode == scene::CanvasRenderMode::WorldSpace ? "World Space" :
                                                          currentMode == scene::CanvasRenderMode::WorldSpaceOverlay ? "World Screen Space" :
                                                          "Screen Space";
                                    if (ImGui::BeginCombo("Display Space", preview))
                                    {
                                        struct DisplayMode { const char *name; scene::CanvasRenderMode mode; };
                                        constexpr DisplayMode modes[] = {
                                            {"Screen Space", scene::CanvasRenderMode::ScreenSpaceOverlay},
                                            {"World Space", scene::CanvasRenderMode::WorldSpace},
                                            {"World Screen Space", scene::CanvasRenderMode::WorldSpaceOverlay},
                                        };
                                        for (const auto &option : modes)
                                        {
                                            const bool selected = currentMode == option.mode;
                                            if (ImGui::Selectable(option.name, selected))
                                            {
                                                canvas->SetRenderMode(option.mode);
                                                entity->AddPrefabOverride("Component:CanvasComponent:RenderMode");
                                                editorShell.MarkSceneDirty();
                                            }
                                            if (selected) ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }
                                }
                                if (canvas->GetContentSource() == scene::RmlUiContentSource::Document)
                                {
                                    const auto &documentOptions = GetCachedAssetReferenceOptions(
                                        editorShell.GetProject(), assets::ProjectAssetType::RmlDocument);
                                    const std::string documentPreview = GetAssetReferencePreview(
                                        documentOptions, canvas->GetDocumentPath(), "None");
                                    if (ImGui::BeginCombo("Document", documentPreview.c_str()))
                                    {
                                        if (ImGui::Selectable("None", canvas->GetDocumentPath().empty()))
                                        {
                                            canvas->SetDocumentPath({});
                                            entity->AddPrefabOverride("Component:CanvasComponent:DocumentPath");
                                            editorShell.MarkSceneDirty();
                                        }
                                        for (const auto &option : documentOptions)
                                        {
                                            const bool selected = option.reference == canvas->GetDocumentPath();
                                            if (ImGui::Selectable(option.displayName.c_str(), selected))
                                            {
                                                canvas->SetDocumentPath(option.reference);
                                                entity->AddPrefabOverride("Component:CanvasComponent:DocumentPath");
                                                editorShell.MarkSceneDirty();
                                            }
                                            if (selected) ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }
                                }
                                else if (auto *text = entity->GetComponent<scene::UITextComponent>())
                                {
                                    ImGui::TextWrapped("Generated from this entity's UI Text component. Edit its text and styling below.");
                                    if (text->GetFontPath().empty())
                                        ImGui::TextDisabled("No Font Path: using the engine fallback font.");
                                }
                                else
                                {
                                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                                                       "Text source requires a UI Text component.");
                                    if (ImGui::Button("Add UI Text"))
                                    {
                                        entity->CreateComponent<scene::UITextComponent>();
                                        editorShell.MarkSceneDirty();
                                    }
                                }
                                if (canvas->GetContentSource() == scene::RmlUiContentSource::Document &&
                                    (canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpaceOverlay ||
                                     canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpace))
                                {
                                    ImGui::TextDisabled("World canvases use this entity's Rect Transform size and pivot.");
                                    if (canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpace)
                                        ImGui::TextDisabled("World Units: 100 UI units = 1 world unit. Face Camera off uses entity rotation.");
                                }
                            }
                            else
                            {
                                ImGui::TextWrapped("Legacy native UI canvas. Kept for existing scenes; use an RmlUi Canvas for new UI.");
                            }
                        }
                        else if (auto *rmlWidget = dynamic_cast<scene::RmlWidgetComponent *>(componentPtr))
                        {
                            const auto *ownCanvas = entity->GetComponent<scene::CanvasComponent>();
                            if (ownCanvas && ownCanvas->GetBackend() == scene::UIRenderBackend::RmlUi &&
                                !ownCanvas->GetDocumentPath().empty())
                                ImGui::TextWrapped("Ignored: this RmlUi Canvas owns a Document directly. Remove this deprecated RML Widget after confirming the migration.");
                            else
                                ImGui::TextWrapped("Deprecated: assign the document directly to an RmlUi Canvas. This component remains for existing scenes.");
                            properties = rmlWidget->Serialize();
                            std::erase_if(properties, [](const scene::Property &property)
                                          { return property.name == "Source"; });
                            propertiesProvided = true;

                            const auto &sourceOptions = GetCachedAssetReferenceOptions(
                                editorShell.GetProject(), assets::ProjectAssetType::RmlDocument);
                            const std::string sourcePreview = GetAssetReferencePreview(
                                sourceOptions, rmlWidget->GetSource(), "None");
                            if (ImGui::BeginCombo("Source", sourcePreview.c_str()))
                            {
                                if (ImGui::Selectable("None", rmlWidget->GetSource().empty()))
                                {
                                    rmlWidget->SetSource({});
                                    entity->AddPrefabOverride("Component:RmlWidgetComponent:Source");
                                    editorShell.MarkSceneDirty();
                                }
                                for (const auto &option : sourceOptions)
                                {
                                    const bool selected = option.reference == rmlWidget->GetSource();
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        rmlWidget->SetSource(option.reference);
                                        entity->AddPrefabOverride("Component:RmlWidgetComponent:Source");
                                        editorShell.MarkSceneDirty();
                                    }
                                    if (selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }
                        else if (auto *decalComponent = dynamic_cast<scene::DecalComponent *>(componentPtr))
                        {
                            properties = decalComponent->Serialize();
                            std::erase_if(properties, [](const scene::Property &property)
                                          { return property.name == "MaterialAsset"; });
                            propertiesProvided = true;

                            const auto &materialOptions = GetCachedAssetReferenceOptions(
                                editorShell.GetProject(), assets::ProjectAssetType::Material);
                            const std::string materialPreview = GetAssetReferencePreview(
                                materialOptions,
                                decalComponent->GetMaterialAssetReference(),
                                "None");
                            if (ImGui::BeginCombo("Material Asset", materialPreview.c_str()))
                            {
                                for (const auto &option : materialOptions)
                                {
                                    const bool selected = option.reference == decalComponent->GetMaterialAssetReference();
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        decalComponent->SetMaterialAssetReference(option.reference);
                                        entity->AddPrefabOverride("Component:DecalComponent:MaterialAsset");
                                        editorShell.MarkSceneDirty();
                                    }
                                    if (selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            if (auto droppedReference = AcceptDroppedMaterialAssetReference())
                            {
                                decalComponent->SetMaterialAssetReference(*droppedReference);
                                entity->AddPrefabOverride("Component:DecalComponent:MaterialAsset");
                                editorShell.MarkSceneDirty();
                            }
                        }
                        else if (auto *terrainComponent = dynamic_cast<scene::TerrainComponent *>(componentPtr))
                        {
                            propertiesProvided = true;
                            properties = {
                                {"Static", scene::PropertyType::Bool, terrainComponent->IsStatic() ? "true" : "false"},
                                {"CellSize", scene::PropertyType::Float, std::to_string(terrainComponent->GetCellSize())},
                                {"HeightScale", scene::PropertyType::Float, std::to_string(terrainComponent->GetHeightScale())},
                                {"SurfaceSmoothing", scene::PropertyType::Float, std::to_string(terrainComponent->GetSurfaceSmoothing())},
                                {"ChunkSize", scene::PropertyType::Int, std::to_string(terrainComponent->GetChunkSize())},
                                {"LodCount", scene::PropertyType::Int, std::to_string(terrainComponent->GetLodCount())},
                                {"PaintEnabled", scene::PropertyType::Bool, terrainComponent->IsPaintEnabled() ? "true" : "false"},
                            };

                            ImGui::Text("Resolution: %d x %d", terrainComponent->GetWidth(), terrainComponent->GetDepth());

                            auto &heightMapPathBuffer = GetTerrainHeightMapPathBuffer(*entity);
                            static std::unordered_map<scene::EntityID, std::string> cachedTerrainHeightMapPaths;
                            auto &cachedHeightMapPath = cachedTerrainHeightMapPaths[entity->GetID()];
                            if (cachedHeightMapPath != terrainComponent->GetHeightMapPath())
                            {
                                std::fill(heightMapPathBuffer.begin(), heightMapPathBuffer.end(), '\0');
                                strncpy_s(heightMapPathBuffer.data(), heightMapPathBuffer.size(), terrainComponent->GetHeightMapPath().c_str(), _TRUNCATE);
                                cachedHeightMapPath = terrainComponent->GetHeightMapPath();
                            }

                            ImGui::InputText("Height Map", heightMapPathBuffer.data(), heightMapPathBuffer.size());
                            if (auto droppedReference = AcceptDroppedTextureAssetReference())
                            {
                                std::fill(heightMapPathBuffer.begin(), heightMapPathBuffer.end(), '\0');
                                strncpy_s(heightMapPathBuffer.data(), heightMapPathBuffer.size(), droppedReference->c_str(), _TRUNCATE);
                                if (terrainComponent->LoadHeightMap(*droppedReference))
                                {
                                    cachedHeightMapPath = terrainComponent->GetHeightMapPath();
                                    entity->AddPrefabOverride("Component:TerrainComponent:HeightMap");
                                    entity->AddPrefabOverride("Component:TerrainComponent:HeightSamples");
                                    editorShell.MarkSceneDirty();
                                }
                                else
                                {
                                    editorShell.Log(EditorShell::ConsoleSeverity::Error, "Failed to load terrain height map: " + *droppedReference);
                                }
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("...##TerrainHeightMap"))
                            {
#ifdef _WIN32
                                OPENFILENAMEA ofn = {};
                                char fileName[MAX_PATH] = "";
                                ofn.lStructSize = sizeof(ofn);
                                ofn.hwndOwner = nullptr;
                                ofn.lpstrFilter = "Height Maps\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All Files\0*.*\0";
                                ofn.lpstrFile = fileName;
                                ofn.nMaxFile = MAX_PATH;
                                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                                if (GetOpenFileNameA(&ofn))
                                {
                                    strncpy_s(heightMapPathBuffer.data(), heightMapPathBuffer.size(), fileName, _TRUNCATE);
                                }
#endif
                            }

                            ImGui::SameLine();
                            ImGui::BeginDisabled(std::strlen(heightMapPathBuffer.data()) == 0);
                            if (ImGui::Button("Load##TerrainHeightMap"))
                            {
                                const std::string heightMapPath = heightMapPathBuffer.data();
                                if (terrainComponent->LoadHeightMap(heightMapPath))
                                {
                                    cachedHeightMapPath = terrainComponent->GetHeightMapPath();
                                    entity->AddPrefabOverride("Component:TerrainComponent:HeightMap");
                                    entity->AddPrefabOverride("Component:TerrainComponent:HeightSamples");
                                    editorShell.MarkSceneDirty();
                                }
                                else
                                {
                                    editorShell.Log(EditorShell::ConsoleSeverity::Error, "Failed to load terrain height map: " + heightMapPath);
                                }
                            }
                            ImGui::EndDisabled();

                            const auto materialAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Material);
                            const std::string materialPreview = GetAssetReferencePreview(
                                materialAssetOptions,
                                terrainComponent->GetMaterialAssetReference(),
                                "Default Shaded");
                            if (ImGui::BeginCombo("Material Asset", materialPreview.c_str()))
                            {
                                for (const auto &option : materialAssetOptions)
                                {
                                    const bool selected = option.reference == terrainComponent->GetMaterialAssetReference();
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        if (AssignMaterialAssetToTerrain(*terrainComponent, option.reference, core::Engine::GetInstance()))
                                        {
                                            entity->AddPrefabOverride("Component:TerrainComponent:MaterialAsset");
                                            editorShell.MarkSceneDirty();
                                        }
                                    }

                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            if (auto droppedReference = AcceptDroppedMaterialAssetReference())
                            {
                                if (AssignMaterialAssetToTerrain(*terrainComponent, *droppedReference, core::Engine::GetInstance()))
                                {
                                    entity->AddPrefabOverride("Component:TerrainComponent:MaterialAsset");
                                    editorShell.MarkSceneDirty();
                                }
                            }
                        }
                        else if (auto *splineComponent = dynamic_cast<scene::SplineComponent *>(componentPtr))
                        {
                            propertiesProvided = true;
                            properties = {
                                {"Width", scene::PropertyType::Float, std::to_string(splineComponent->GetWidth())},
                                {"Thickness", scene::PropertyType::Float, std::to_string(splineComponent->GetThickness())},
                                {"SamplesPerSegment", scene::PropertyType::Int, std::to_string(splineComponent->GetSamplesPerSegment())},
                                {"CollisionSamplesPerSegment", scene::PropertyType::Int, std::to_string(splineComponent->GetCollisionSamplesPerSegment())},
                                {"MaxChordError", scene::PropertyType::Float, std::to_string(splineComponent->GetMaxChordError())},
                                {"MaxTangentAngleDegrees", scene::PropertyType::Float, std::to_string(splineComponent->GetMaxTangentAngleDegrees())},
                                {"UvMetersPerTile", scene::PropertyType::Float, std::to_string(splineComponent->GetUvMetersPerTile())},
                                {"Closed", scene::PropertyType::Bool, splineComponent->IsClosed() ? "true" : "false"},
                                {"GenerateMesh", scene::PropertyType::Bool, splineComponent->ShouldGenerateMesh() ? "true" : "false"},
                                {"GenerateCollision", scene::PropertyType::Bool, splineComponent->ShouldGenerateCollision() ? "true" : "false"},
                            };

                            const auto materialAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Material);
                            const std::string materialPreview = GetAssetReferencePreview(
                                materialAssetOptions,
                                splineComponent->GetMaterialAssetReference(),
                                "Default Shaded");
                            if (ImGui::BeginCombo("Material Asset", materialPreview.c_str()))
                            {
                                for (const auto &option : materialAssetOptions)
                                {
                                    const bool selected = option.reference == splineComponent->GetMaterialAssetReference();
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        if (AssignMaterialAssetToSpline(*splineComponent, option.reference, core::Engine::GetInstance()))
                                        {
                                            entity->AddPrefabOverride("Component:SplineComponent:MaterialAsset");
                                            editorShell.MarkSceneDirty();
                                        }
                                    }

                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            if (auto droppedReference = AcceptDroppedMaterialAssetReference())
                            {
                                if (AssignMaterialAssetToSpline(*splineComponent, *droppedReference, core::Engine::GetInstance()))
                                {
                                    entity->AddPrefabOverride("Component:SplineComponent:MaterialAsset");
                                    editorShell.MarkSceneDirty();
                                }
                            }

                            const auto &points = splineComponent->GetPoints();
                            ImGui::Text("Control Points: %zu", points.size());
                            for (std::size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
                            {
                                ImGui::PushID(static_cast<int>(pointIndex));
                                glm::vec3 position = points[pointIndex].position;
                                const std::string pointLabel = "Point " + std::to_string(pointIndex);
                                if (ImGui::DragFloat3(pointLabel.c_str(), &position.x, 0.1f))
                                {
                                    splineComponent->SetPointPosition(pointIndex, position);
                                    entity->AddPrefabOverride("Component:SplineComponent:Points." + std::to_string(pointIndex));
                                    editorShell.MarkSceneDirty();
                                }
                                glm::vec3 rotation = points[pointIndex].rotation;
                                const std::string rotationLabel = "Rotation " + std::to_string(pointIndex);
                                if (ImGui::DragFloat3(rotationLabel.c_str(), &rotation.x, 0.1f))
                                {
                                    splineComponent->SetPointRotation(pointIndex, rotation);
                                    entity->AddPrefabOverride("Component:SplineComponent:PointRotations." + std::to_string(pointIndex));
                                    editorShell.MarkSceneDirty();
                                }
                                ImGui::SameLine();
                                ImGui::BeginDisabled(points.size() <= 2);
                                if (ImGui::Button("Remove"))
                                {
                                    splineComponent->RemovePoint(pointIndex);
                                    entity->AddPrefabOverride("Component:SplineComponent:PointCount");
                                    editorShell.MarkSceneDirty();
                                    ImGui::EndDisabled();
                                    ImGui::PopID();
                                    break;
                                }
                                ImGui::EndDisabled();
                                ImGui::PopID();
                            }

                            if (ImGui::Button("Add Point"))
                            {
                                const glm::vec3 newPoint = points.empty()
                                                               ? glm::vec3(0.0f)
                                                               : points.back().position + glm::vec3(8.0f, 0.0f, 0.0f);
                                splineComponent->AddPoint(newPoint);
                                entity->AddPrefabOverride("Component:SplineComponent:PointCount");
                                editorShell.MarkSceneDirty();
                            }
                        }
                        else if (auto *oceanComponent = dynamic_cast<scene::OceanComponent *>(componentPtr))
                        {
                            propertiesProvided = true;
                            properties = {
                                {"ShallowColor", scene::PropertyType::Color, std::to_string(oceanComponent->GetShallowColor().x) + "," + std::to_string(oceanComponent->GetShallowColor().y) + "," + std::to_string(oceanComponent->GetShallowColor().z) + ",1.0"},
                                {"DeepColor", scene::PropertyType::Color, std::to_string(oceanComponent->GetDeepColor().x) + "," + std::to_string(oceanComponent->GetDeepColor().y) + "," + std::to_string(oceanComponent->GetDeepColor().z) + ",1.0"},
                                {"FoamColor", scene::PropertyType::Color, std::to_string(oceanComponent->GetFoamColor().x) + "," + std::to_string(oceanComponent->GetFoamColor().y) + "," + std::to_string(oceanComponent->GetFoamColor().z) + ",1.0"},
                                {"Opacity", scene::PropertyType::Float, std::to_string(oceanComponent->GetOpacity())},
                                {"Smoothness", scene::PropertyType::Float, std::to_string(oceanComponent->GetSmoothness())},
                                {"MaxVisibilityDepth", scene::PropertyType::Float, std::to_string(oceanComponent->GetMaxVisibilityDepth())},
                                {"UnderwaterFadeStart", scene::PropertyType::Float, std::to_string(oceanComponent->GetUnderwaterFadeStart())},
                                {"UnderwaterFadeSoftness", scene::PropertyType::Float, std::to_string(oceanComponent->GetUnderwaterFadeSoftness())},
                                {"UnderwaterDepthFalloff", scene::PropertyType::Float, std::to_string(oceanComponent->GetUnderwaterDepthFalloff())},
                                {"UnderwaterLightFalloff", scene::PropertyType::Float, std::to_string(oceanComponent->GetUnderwaterLightFalloff())},
                                {"UnderwaterTurbidity", scene::PropertyType::Float, std::to_string(oceanComponent->GetUnderwaterTurbidity())},
                                {"RefractionStrength", scene::PropertyType::Float, std::to_string(oceanComponent->GetRefractionStrength())},
                                {"WaveAmplitude", scene::PropertyType::Float, std::to_string(oceanComponent->GetWaveAmplitude())},
                                {"WaveLength", scene::PropertyType::Float, std::to_string(oceanComponent->GetWaveLength())},
                                {"WaveSpeed", scene::PropertyType::Float, std::to_string(oceanComponent->GetWaveSpeed())},
                                {"WaveChoppiness", scene::PropertyType::Float, std::to_string(oceanComponent->GetWaveChoppiness())},
                                {"FoamDistance", scene::PropertyType::Float, std::to_string(oceanComponent->GetFoamDistance())},
                                {"FoamIntensity", scene::PropertyType::Float, std::to_string(oceanComponent->GetFoamIntensity())},
                                {"InvertAreaMask", scene::PropertyType::Bool, oceanComponent->GetInvertAreaMask() ? "true" : "false"},
                            };

                            const auto &areas = oceanComponent->GetAreas();
                            ImGui::SeparatorText("Area Masks");
                            ImGui::Text("Render full ocean by default. Areas carve holes unless Invert Area Mask is enabled.");
                            for (std::size_t areaIndex = 0; areaIndex < areas.size(); ++areaIndex)
                            {
                                ImGui::PushID(static_cast<int>(areaIndex));
                                const std::string header = "Area " + std::to_string(areaIndex);
                                if (ImGui::TreeNode(header.c_str()))
                                {
                                    const auto &points = areas[areaIndex].points;
                                    for (std::size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
                                    {
                                        ImGui::PushID(static_cast<int>(pointIndex));
                                        glm::vec2 point = points[pointIndex];
                                        const std::string label = "Point " + std::to_string(pointIndex);
                                        if (ImGui::DragFloat2(label.c_str(), &point.x, 0.1f))
                                        {
                                            oceanComponent->SetAreaPoint(areaIndex, pointIndex, point);
                                            entity->AddPrefabOverride("Component:OceanComponent:Areas." + std::to_string(areaIndex) + ".Points." + std::to_string(pointIndex));
                                            editorShell.MarkSceneDirty();
                                        }
                                        ImGui::SameLine();
                                        ImGui::BeginDisabled(points.size() <= 3);
                                        if (ImGui::Button("Remove"))
                                        {
                                            oceanComponent->RemovePoint(areaIndex, pointIndex);
                                            entity->AddPrefabOverride("Component:OceanComponent:Areas." + std::to_string(areaIndex) + ".PointCount");
                                            editorShell.MarkSceneDirty();
                                            ImGui::EndDisabled();
                                            ImGui::PopID();
                                            break;
                                        }
                                        ImGui::EndDisabled();
                                        ImGui::PopID();
                                    }

                                    if (ImGui::Button("Add Point"))
                                    {
                                        const glm::vec2 newPoint = points.empty() ? glm::vec2(0.0f) : points.back() + glm::vec2(6.0f, 0.0f);
                                        oceanComponent->AddPoint(areaIndex, newPoint);
                                        entity->AddPrefabOverride("Component:OceanComponent:Areas." + std::to_string(areaIndex) + ".PointCount");
                                        editorShell.MarkSceneDirty();
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button("Remove Area"))
                                    {
                                        oceanComponent->RemoveArea(areaIndex);
                                        entity->AddPrefabOverride("Component:OceanComponent:AreaCount");
                                        editorShell.MarkSceneDirty();
                                        ImGui::TreePop();
                                        ImGui::PopID();
                                        break;
                                    }

                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }

                            if (ImGui::Button("Add Area"))
                            {
                                oceanComponent->AddArea({
                                    {-12.0f, -12.0f},
                                    {12.0f, -12.0f},
                                    {12.0f, 12.0f},
                                    {-12.0f, 12.0f},
                                });
                                entity->AddPrefabOverride("Component:OceanComponent:AreaCount");
                                editorShell.MarkSceneDirty();
                            }
                        }
                        else if (auto *foliageComponent = dynamic_cast<scene::FoliageComponent *>(componentPtr))
                        {
                            propertiesProvided = true;
                            properties = {
                                {"Static", scene::PropertyType::Bool, foliageComponent->IsStatic() ? "true" : "false"},
                                {"PaintEnabled", scene::PropertyType::Bool, foliageComponent->IsPaintEnabled() ? "true" : "false"},
                                {"BrushRadius", scene::PropertyType::Float, std::to_string(foliageComponent->GetBrushRadius())},
                                {"Density", scene::PropertyType::Int, std::to_string(foliageComponent->GetDensity())},
                                {"MinScale", scene::PropertyType::Float, std::to_string(foliageComponent->GetMinScale())},
                                {"MaxScale", scene::PropertyType::Float, std::to_string(foliageComponent->GetMaxScale())},
                                {"MaxDrawDistance", scene::PropertyType::Float, std::to_string(foliageComponent->GetMaxDrawDistance())},
                                {"MaxShadowDistance", scene::PropertyType::Float, std::to_string(foliageComponent->GetMaxShadowDistance())},
                                {"MinRenderLod", scene::PropertyType::Int, std::to_string(foliageComponent->GetMinRenderLod())},
                                {"MinShadowLod", scene::PropertyType::Int, std::to_string(foliageComponent->GetMinShadowLod())},
                                {"CastShadows", scene::PropertyType::Bool, foliageComponent->GetCastShadows() ? "true" : "false"},
                            };

                            const std::size_t selectedTypeIndex = static_cast<std::size_t>((std::max)(0, foliageComponent->GetSelectedTypeIndex()));
                            auto *selectedType = foliageComponent->GetSelectedType();

                            ImGui::Text("Instances: %zu total, %zu selected", foliageComponent->GetTotalInstanceCount(), foliageComponent->GetSelectedTypeInstanceCount());
                            if (selectedType && selectedType->mesh)
                            {
                                std::size_t maxLodCount = 1;
                                for (std::size_t submeshIndex = 0; submeshIndex < selectedType->mesh->GetSubmeshCount(); ++submeshIndex)
                                {
                                    maxLodCount = (std::max)(maxLodCount, selectedType->mesh->GetSubmeshLodCount(submeshIndex));
                                }
                                ImGui::Text("Mesh LODs: %zu", maxLodCount);
                            }

                            if (ImGui::BeginCombo("Foliage Type", selectedType ? selectedType->name.c_str() : "None"))
                            {
                                for (std::size_t typeIndex = 0; typeIndex < foliageComponent->GetTypeCount(); ++typeIndex)
                                {
                                    const auto *type = foliageComponent->GetType(typeIndex);
                                    const bool selected = static_cast<int>(typeIndex) == foliageComponent->GetSelectedTypeIndex();
                                    const std::string label = type ? type->name : ("Foliage " + std::to_string(typeIndex + 1));
                                    if (ImGui::Selectable(label.c_str(), selected))
                                    {
                                        foliageComponent->SetSelectedTypeIndex(static_cast<int>(typeIndex));
                                        entity->AddPrefabOverride("Component:FoliageComponent:SelectedType");
                                        editorShell.MarkSceneDirty();
                                    }
                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            if (ImGui::Button("Add Type"))
                            {
                                foliageComponent->AddType();
                                entity->AddPrefabOverride("Component:FoliageComponent:FoliageTypeCount");
                                entity->AddPrefabOverride("Component:FoliageComponent:SelectedType");
                                editorShell.MarkSceneDirty();
                            }
                            ImGui::SameLine();
                            ImGui::BeginDisabled(foliageComponent->GetTypeCount() <= 1);
                            if (ImGui::Button("Remove Type"))
                            {
                                foliageComponent->RemoveType(selectedTypeIndex);
                                entity->AddPrefabOverride("Component:FoliageComponent:FoliageTypeCount");
                                entity->AddPrefabOverride("Component:FoliageComponent:SelectedType");
                                editorShell.MarkSceneDirty();
                            }
                            ImGui::EndDisabled();

                            selectedType = foliageComponent->GetSelectedType();
                            if (selectedType)
                            {
                                auto &nameBuffer = GetFoliageTypeNameBuffer(entity->GetID(), selectedTypeIndex);
                                static std::unordered_map<std::string, std::string> cachedFoliageTypeNames;
                                const std::string typeNameKey = std::to_string(entity->GetID()) + ":" + std::to_string(selectedTypeIndex);
                                if (cachedFoliageTypeNames[typeNameKey] != selectedType->name)
                                {
                                    std::fill(nameBuffer.begin(), nameBuffer.end(), '\0');
                                    strncpy_s(nameBuffer.data(), nameBuffer.size(), selectedType->name.c_str(), _TRUNCATE);
                                    cachedFoliageTypeNames[typeNameKey] = selectedType->name;
                                }
                                if (ImGui::InputText("Type Name", nameBuffer.data(), nameBuffer.size()))
                                {
                                    foliageComponent->SetTypeName(selectedTypeIndex, nameBuffer.data());
                                    cachedFoliageTypeNames[typeNameKey] = nameBuffer.data();
                                    entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".Name");
                                    editorShell.MarkSceneDirty();
                                }
                            }

                            auto &engine = core::Engine::GetInstance();
                            const auto meshAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Mesh);
                            std::string meshPreview = !selectedType || selectedType->sourceMeshPath.empty() ? "None" : selectedType->sourceMeshPath;
                            for (const auto &option : meshAssetOptions)
                            {
                                if (selectedType && option.reference == selectedType->sourceMeshPath)
                                {
                                    meshPreview = option.displayName;
                                    break;
                                }
                            }

                            if (ImGui::BeginCombo("Foliage Mesh", meshPreview.c_str()))
                            {
                                for (const auto &option : meshAssetOptions)
                                {
                                    const bool selected = selectedType && option.reference == selectedType->sourceMeshPath;
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        if (auto *mesh = engine.GetAssetManager().LoadMeshAsset(option.reference))
                                        {
                                            const auto &materialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(option.reference);
                                            std::vector<render::Material *> loadedMaterials;
                                            loadedMaterials.reserve((std::max<std::size_t>)(materialReferences.size(), 1));
                                            for (const auto &materialReference : materialReferences)
                                            {
                                                loadedMaterials.push_back(engine.GetAssetManager().LoadMaterialAsset(materialReference));
                                            }
                                            if (loadedMaterials.empty())
                                            {
                                                loadedMaterials.push_back(engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
                                            }
                                            foliageComponent->SetTypeMeshAndMaterials(
                                                selectedTypeIndex,
                                                mesh,
                                                loadedMaterials,
                                                option.reference);
                                            foliageComponent->SetTypeUseGeneratedLods(selectedTypeIndex, false);
                                            entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".SourceMesh");
                                            entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".UseGeneratedLods");
                                            editorShell.MarkSceneDirty();
                                        }
                                    }
                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            selectedType = foliageComponent->GetSelectedType();
                            if (selectedType && selectedType->mesh && selectedType->mesh->GetSubmeshCount() > 1)
                            {
                                const auto submeshChoices = BuildFoliageSubmeshChoices(*selectedType->mesh);
                                std::string submeshPreview = "All Mesh Parts";
                                for (const auto &choice : submeshChoices)
                                {
                                    if (FoliageSubmeshSelectionMatches(*selectedType, choice.indices))
                                    {
                                        submeshPreview = choice.isGroup ? "Group: " + choice.label : choice.label;
                                        break;
                                    }
                                }

                                if (ImGui::BeginCombo("Mesh Part", submeshPreview.c_str()))
                                {
                                    const bool allSelected = FoliageSubmeshSelectionMatches(*selectedType, {});
                                    if (ImGui::Selectable("All Mesh Parts", allSelected))
                                    {
                                        foliageComponent->SetTypeSubmeshIndex(selectedTypeIndex, -1);
                                        entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".SubmeshIndex");
                                        entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".SubmeshIndices");
                                        editorShell.MarkSceneDirty();
                                    }
                                    if (allSelected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }

                                    bool drewSeparator = false;
                                    for (const auto &choice : submeshChoices)
                                    {
                                        if (!choice.isGroup && !drewSeparator)
                                        {
                                            ImGui::Separator();
                                            drewSeparator = true;
                                        }

                                        const std::string label = choice.isGroup ? "Group: " + choice.label : choice.label;
                                        const bool selected = FoliageSubmeshSelectionMatches(*selectedType, choice.indices);
                                        if (ImGui::Selectable(label.c_str(), selected))
                                        {
                                            foliageComponent->SetTypeSubmeshIndices(selectedTypeIndex, choice.indices);
                                            entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".SubmeshIndex");
                                            entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".SubmeshIndices");
                                            editorShell.MarkSceneDirty();
                                        }
                                        if (selected)
                                        {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            }

                            selectedType = foliageComponent->GetSelectedType();
                            const bool canGenerateLodsForFoliage = selectedType &&
                                                                   !selectedType->sourceMeshPath.empty() &&
                                                                   !assets::Project::IsEngineAssetReference(selectedType->sourceMeshPath);
                            ImGui::BeginDisabled(!canGenerateLodsForFoliage);
                            if (ImGui::Button("Generate Foliage LODs"))
                            {
                                try
                                {
                                    const std::string resolvedPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(selectedType->sourceMeshPath);
                                    auto importedMeshAsset = engine.GenerateMeshAssetLods(resolvedPath);
                                    if (importedMeshAsset.mesh)
                                    {
                                        foliageComponent->SetTypeMeshAndMaterials(
                                            selectedTypeIndex,
                                            importedMeshAsset.mesh,
                                            importedMeshAsset.materials,
                                            selectedType->sourceMeshPath);
                                        foliageComponent->SetTypeUseGeneratedLods(selectedTypeIndex, true);
                                        entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".SourceMesh");
                                        entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".UseGeneratedLods");
                                        editorShell.MarkSceneDirty();
                                        editorShell.Log(EditorShell::ConsoleSeverity::Info, "Generated foliage LODs: " + selectedType->sourceMeshPath);
                                    }
                                }
                                catch (const std::exception &exception)
                                {
                                    editorShell.Log(EditorShell::ConsoleSeverity::Error, std::string("Failed to generate foliage LODs: ") + exception.what());
                                }
                                catch (...)
                                {
                                    editorShell.Log(EditorShell::ConsoleSeverity::Error, "Failed to generate foliage LODs.");
                                }
                            }
                            ImGui::EndDisabled();

                            const auto materialAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Material);
                            const std::string materialPreview = GetAssetReferencePreview(
                                materialAssetOptions,
                                selectedType ? selectedType->materialAssetReference : std::string{},
                                selectedType && selectedType->materialAssetReference.empty() ? "Auto From Mesh" : "Default Shaded");
                            if (ImGui::BeginCombo("Material Override", materialPreview.c_str()))
                            {
                                for (const auto &option : materialAssetOptions)
                                {
                                    const bool selected = selectedType && option.reference == selectedType->materialAssetReference;
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        foliageComponent->SetTypeMaterialAssetReference(selectedTypeIndex, option.reference);
                                        entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".MaterialAsset");
                                        editorShell.MarkSceneDirty();
                                    }

                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::SameLine();
                            ImGui::BeginDisabled(!selectedType || selectedType->materialAssetReference.empty());
                            if (ImGui::Button("Auto##FoliageMaterial"))
                            {
                                foliageComponent->ClearTypeMaterialAssetReference(selectedTypeIndex);
                                entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".MaterialAsset");
                                editorShell.MarkSceneDirty();
                            }
                            ImGui::EndDisabled();

                            selectedType = foliageComponent->GetSelectedType();
                            if (selectedType && ImGui::TreeNode("Spatial Cells & Collision"))
                            {
                                float cellSize = selectedType->asset.cellSize;
                                if (ImGui::DragFloat("Cell Size", &cellSize, 1.0f, 1.0f, 256.0f, "%.1f"))
                                {
                                    foliageComponent->SetTypeCellSize(selectedTypeIndex, cellSize);
                                    entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".CellSize");
                                    editorShell.MarkSceneDirty();
                                }

                                bool collisionEnabled = selectedType->asset.collisionEnabled;
                                if (ImGui::Checkbox("Enable Capsule Collision", &collisionEnabled))
                                {
                                    foliageComponent->SetTypeCollisionEnabled(selectedTypeIndex, collisionEnabled);
                                    entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".CollisionEnabled");
                                    editorShell.MarkSceneDirty();
                                }

                                ImGui::BeginDisabled(!collisionEnabled);
                                glm::vec3 center = selectedType->asset.collisionCenter;
                                float radius = selectedType->asset.collisionRadius;
                                float height = selectedType->asset.collisionHeight;
                                bool collisionChanged = ImGui::DragFloat3("Collider Center", &center.x, 0.05f);
                                collisionChanged |= ImGui::DragFloat("Collider Radius", &radius, 0.02f, 0.01f, 100.0f);
                                collisionChanged |= ImGui::DragFloat("Collider Height", &height, 0.05f, 0.02f, 500.0f);
                                if (collisionChanged)
                                {
                                    foliageComponent->SetTypeCollisionCapsule(selectedTypeIndex, center, radius, height);
                                    entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".CollisionCenter");
                                    entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".CollisionRadius");
                                    entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".CollisionHeight");
                                    editorShell.MarkSceneDirty();
                                }
                                ImGui::EndDisabled();
                                ImGui::TreePop();
                            }

                            ImGui::BeginDisabled(foliageComponent->GetSelectedTypeInstanceCount() == 0);
                            if (ImGui::Button("Clear Selected Type"))
                            {
                                foliageComponent->ClearSelectedTypeInstances();
                                entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".Instances");
                                editorShell.MarkSceneDirty();
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            ImGui::BeginDisabled(foliageComponent->GetTotalInstanceCount() == 0);
                            if (ImGui::Button("Clear All Foliage"))
                            {
                                foliageComponent->ClearInstances();
                                entity->AddPrefabOverride("Component:FoliageComponent:Instances");
                                editorShell.MarkSceneDirty();
                            }
                            ImGui::EndDisabled();

                            selectedType = foliageComponent->GetSelectedType();
                            if (selectedType && !selectedType->instances.empty() && ImGui::TreeNode("Foliage Instances"))
                            {
                                static std::unordered_map<std::string, int> selectedFoliageInstances;
                                const std::string instanceSelectionKey = std::to_string(entity->GetID()) + ":" + std::to_string(selectedTypeIndex);
                                int &selectedInstanceIndex = selectedFoliageInstances[instanceSelectionKey];
                                selectedInstanceIndex = std::clamp(selectedInstanceIndex, 0, static_cast<int>(selectedType->instances.size()) - 1);

                                ImGui::SetNextItemWidth(220.0f);
                                if (ImGui::BeginCombo("Instance", ("#" + std::to_string(selectedInstanceIndex)).c_str()))
                                {
                                    for (int instanceIndex = 0; instanceIndex < static_cast<int>(selectedType->instances.size()); ++instanceIndex)
                                    {
                                        const bool selected = selectedInstanceIndex == instanceIndex;
                                        const std::string label = "#" + std::to_string(instanceIndex);
                                        if (ImGui::Selectable(label.c_str(), selected))
                                        {
                                            selectedInstanceIndex = instanceIndex;
                                        }
                                        if (selected)
                                        {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }

                                if (auto *instance = foliageComponent->GetSelectedTypeInstance(static_cast<std::size_t>(selectedInstanceIndex)))
                                {
                                    glm::vec3 position = instance->position;
                                    glm::vec3 rotation = instance->rotationDegrees;
                                    glm::vec3 scale = instance->scale;
                                    bool transformChanged = false;
                                    transformChanged |= ImGui::DragFloat3("Instance Position", &position.x, 0.05f);
                                    transformChanged |= ImGui::DragFloat3("Instance Rotation", &rotation.x, 0.25f);
                                    transformChanged |= ImGui::DragFloat3("Instance Scale", &scale.x, 0.01f, 0.0001f, 100.0f);

                                    if (transformChanged &&
                                        foliageComponent->SetSelectedTypeInstanceTransform(static_cast<std::size_t>(selectedInstanceIndex), position, rotation, scale))
                                    {
                                        entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".Instances");
                                        entity->AddPrefabOverride("Component:FoliageComponent:Instances");
                                        editorShell.MarkSceneDirty();
                                    }

                                    if (ImGui::Button("Delete Instance") &&
                                        foliageComponent->RemoveSelectedTypeInstance(static_cast<std::size_t>(selectedInstanceIndex)))
                                    {
                                        selectedInstanceIndex = std::clamp(selectedInstanceIndex, 0, static_cast<int>(foliageComponent->GetSelectedTypeInstanceCount()) - 1);
                                        entity->AddPrefabOverride("Component:FoliageComponent:Type." + std::to_string(selectedTypeIndex) + ".Instances");
                                        entity->AddPrefabOverride("Component:FoliageComponent:Instances");
                                        editorShell.MarkSceneDirty();
                                    }
                                }

                                ImGui::TreePop();
                            }
                        }

                        if (auto *particleSystemComponent = dynamic_cast<scene::ParticleSystemComponent *>(componentPtr))
                        {
                            const auto particleAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::ParticleSystem);
                            const std::string currentReference = particleSystemComponent->GetParticleSystemAssetReference();
                            const std::string preview = GetAssetReferencePreview(particleAssetOptions, currentReference, "None");
                            if (ImGui::BeginCombo("Particle System Asset", preview.c_str()))
                            {
                                const bool noneSelected = currentReference.empty();
                                if (ImGui::Selectable("None", noneSelected))
                                {
                                    if (particleSystemComponent->SetParticleSystemAssetReference({}))
                                    {
                                        entity->AddPrefabOverride("Component:ParticleSystemComponent:ParticleSystemAsset");
                                        editorShell.MarkSceneDirty();
                                    }
                                }
                                if (noneSelected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }

                                for (const auto &option : particleAssetOptions)
                                {
                                    const bool selected = option.reference == currentReference;
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        if (particleSystemComponent->SetParticleSystemAssetReference(option.reference))
                                        {
                                            entity->AddPrefabOverride("Component:ParticleSystemComponent:ParticleSystemAsset");
                                            editorShell.MarkSceneDirty();
                                        }
                                    }
                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }
                            if (auto droppedParticleReference = AcceptDroppedParticleSystemAssetReference())
                            {
                                if (particleSystemComponent->SetParticleSystemAssetReference(*droppedParticleReference))
                                {
                                    entity->AddPrefabOverride("Component:ParticleSystemComponent:ParticleSystemAsset");
                                    editorShell.MarkSceneDirty();
                                }
                            }
                            if (!currentReference.empty())
                            {
                                ImGui::SameLine();
                                if (ImGui::Button("Open"))
                                {
                                    editorShell.OpenParticleSystemAsset(currentReference);
                                }
                            }

                            propertiesProvided = true;
                            properties.clear();
                        }

                        if (auto *soundEmitterComponent = dynamic_cast<scene::SoundEmitterComponent *>(componentPtr))
                        {
                            const auto &audioOptions = GetCachedAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Audio);
                            const std::string currentReference = soundEmitterComponent->GetClipReference();
                            const std::string preview = GetAssetReferencePreview(audioOptions, currentReference, "None");
                            if (ImGui::BeginCombo("Audio Clip", preview.c_str()))
                            {
                                const bool noneSelected = currentReference.empty();
                                if (ImGui::Selectable("None", noneSelected))
                                {
                                    soundEmitterComponent->SetClipReference({});
                                    entity->AddPrefabOverride("Component:SoundEmitterComponent:Clip");
                                    editorShell.MarkSceneDirty();
                                }
                                if (noneSelected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }

                                for (const auto &option : audioOptions)
                                {
                                    const bool selected = option.reference == currentReference;
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        soundEmitterComponent->SetClipReference(option.reference);
                                        entity->AddPrefabOverride("Component:SoundEmitterComponent:Clip");
                                        editorShell.MarkSceneDirty();
                                    }
                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            if (auto droppedAudioReference = AcceptDroppedAudioAssetReference())
                            {
                                soundEmitterComponent->SetClipReference(*droppedAudioReference);
                                entity->AddPrefabOverride("Component:SoundEmitterComponent:Clip");
                                editorShell.MarkSceneDirty();
                            }

                            ImGui::TextDisabled("WAV clips only in the current runtime.");
                        }

                        if (auto *imageComponent = dynamic_cast<scene::UIImageComponent *>(componentPtr))
                        {
                            const auto &textureOptions = GetCachedAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Texture);
                            const std::string currentReference = imageComponent->GetTexturePath();
                            const std::string preview = GetAssetReferencePreview(textureOptions, currentReference, "None");
                            if (ImGui::BeginCombo("Texture", preview.c_str()))
                            {
                                const bool noneSelected = currentReference.empty();
                                if (ImGui::Selectable("None", noneSelected))
                                {
                                    imageComponent->SetTexturePath({});
                                    entity->AddPrefabOverride("Component:UIImageComponent:TexturePath");
                                    editorShell.MarkSceneDirty();
                                }
                                if (noneSelected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }

                                for (const auto &option : textureOptions)
                                {
                                    const bool selected = option.reference == currentReference;
                                    if (ImGui::Selectable(option.displayName.c_str(), selected))
                                    {
                                        imageComponent->SetTexturePath(option.reference);
                                        entity->AddPrefabOverride("Component:UIImageComponent:TexturePath");
                                        editorShell.MarkSceneDirty();
                                    }

                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }
                            if (auto droppedTextureReference = AcceptDroppedTextureAssetReference())
                            {
                                imageComponent->SetTexturePath(*droppedTextureReference);
                                entity->AddPrefabOverride("Component:UIImageComponent:TexturePath");
                                editorShell.MarkSceneDirty();
                            }

                            properties = imageComponent->Serialize();
                            std::erase_if(properties, [](const scene::Property &property)
                                          { return property.name == "Enabled" || property.name == "TexturePath"; });
                            propertiesProvided = true;
                        }

                        if (!propertiesProvided)
                        {
                            properties = componentPtr->Serialize();
                        }

                        int propertyIndex = 0;
                        for (auto &property : properties)
                        {
                            if (property.name == "PostProcessEffectCount" || property.name == "MainCamera" || property.name == "Primary" || property.name.rfind("PostProcessEffects.", 0) == 0)
                            {
                                continue;
                            }
                            if (dynamic_cast<scene::TerrainComponent *>(componentPtr) &&
                                (property.name == "MaterialAsset" ||
                                 property.name == "HeightMap" ||
                                 property.name == "HeightSamples" ||
                                 property.name == "PaintMode" ||
                                 property.name == "BrushRadius" ||
                                 property.name == "BrushStrength" ||
                                 property.name == "FlattenHeight"))
                            {
                                continue;
                            }
                            if (dynamic_cast<scene::FoliageComponent *>(componentPtr) &&
                                (property.name == "SourceMesh" ||
                                 property.name == "MaterialAsset" ||
                                 property.name == "Instances"))
                            {
                                continue;
                            }
                            if (dynamic_cast<scene::AnimationComponent *>(componentPtr) &&
                                (property.name == "SourceAnimation" ||
                                 property.name == "AnimationGraph" ||
                                 property.name == "CurrentClipIndex" ||
                                 property.name == "ClipCount" ||
                                 property.name.rfind("Clips.", 0) == 0 ||
                                 property.name.rfind("Graph.", 0) == 0))
                            {
                                continue;
                            }
                            if (auto *colliderComponent = dynamic_cast<scene::ColliderComponent *>(componentPtr))
                            {
                                if ((colliderComponent->GetShape() == scene::ColliderShape::Terrain ||
                                     colliderComponent->GetShape() == scene::ColliderShape::Mesh) &&
                                    (property.name == "Center" ||
                                     property.name == "Size" ||
                                     property.name == "Radius" ||
                                     property.name == "Height"))
                                {
                                    continue;
                                }
                            }
                            if (dynamic_cast<scene::SoundEmitterComponent *>(componentPtr) && property.name == "Clip")
                            {
                                continue;
                            }

                            ImGui::PushID(propertyIndex++);
                            bool renderedCustomLightProperty = false;
                            if (auto *lightComponent = dynamic_cast<scene::LightComponent *>(componentPtr);
                                lightComponent && lightComponent->GetLight().type == scene::LightType::Directional)
                            {
                                const auto showHelp = [](const char *text)
                                {
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                    {
                                        ImGui::BeginTooltip();
                                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                                        ImGui::TextUnformatted(text);
                                        ImGui::PopTextWrapPos();
                                        ImGui::EndTooltip();
                                    }
                                };

                                if (property.name == "Shadow Cascade Count")
                                {
                                    ImGui::SeparatorText("Directional Shadow Coverage");
                                    int value = std::stoi(property.value);
                                    if (ImGui::SliderInt("Cascade Count", &value, 1, scene::kMaxDirectionalShadowCascades))
                                    {
                                        property.value = std::to_string(value);
                                        propertiesChanged = true;
                                    }
                                    showHelp("More cascades preserve detail over a longer distance, but require more shadow-map work.");
                                    renderedCustomLightProperty = true;
                                }
                                else if (property.name == "Shadow Distance (0 = Camera Far)")
                                {
                                    float value = std::stof(property.value);
                                    bool useCameraFarPlane = value <= 0.0f;
                                    if (ImGui::Checkbox("Use Camera Far Plane", &useCameraFarPlane))
                                    {
                                        value = useCameraFarPlane ? 0.0f : 150.0f;
                                        property.value = std::to_string(value);
                                        propertiesChanged = true;
                                    }
                                    showHelp("When enabled, shadows extend to the active camera's far clipping plane.");
                                    ImGui::BeginDisabled(useCameraFarPlane);
                                    if (ImGui::DragFloat("Maximum Shadow Distance", &value, 1.0f, 0.1f, 100000.0f, "%.1f m"))
                                    {
                                        property.value = std::to_string(value);
                                        propertiesChanged = true;
                                    }
                                    ImGui::EndDisabled();
                                    showHelp("The far edge of the last cascade. Shorter distances give nearby shadows more texture detail.");
                                    renderedCustomLightProperty = true;
                                }
                                else if (property.name == "Near Shadow Distance (0 = Auto)")
                                {
                                    float value = std::stof(property.value);
                                    bool automatic = value <= 0.0f;
                                    if (ImGui::Checkbox("Automatic First Cascade", &automatic))
                                    {
                                        value = automatic ? 0.0f : 8.0f;
                                        property.value = std::to_string(value);
                                        propertiesChanged = true;
                                    }
                                    showHelp("Automatically places the first split using the cascade distribution setting.");
                                    ImGui::BeginDisabled(automatic);
                                    if (ImGui::DragFloat("First Cascade Ends At", &value, 0.25f, 0.1f, 100000.0f, "%.1f m"))
                                    {
                                        property.value = std::to_string(value);
                                        propertiesChanged = true;
                                    }
                                    ImGui::EndDisabled();
                                    showHelp("Keep this beyond the player and first-person weapon to give their nearby shadows maximum resolution.");
                                    renderedCustomLightProperty = true;
                                }
                                else if (property.name == "Shadow Split Lambda")
                                {
                                    float value = std::stof(property.value);
                                    if (ImGui::SliderFloat("Cascade Distribution", &value, 0.0f, 1.0f, "%.2f"))
                                    {
                                        property.value = std::to_string(value);
                                        propertiesChanged = true;
                                    }
                                    showHelp("0 spreads cascades evenly by distance. 1 concentrates them near the camera for sharper nearby shadows.");
                                    renderedCustomLightProperty = true;
                                }
                                else if (property.name == "Shadow Cascade Blend Distance")
                                {
                                    float value = std::stof(property.value);
                                    if (ImGui::DragFloat("Cascade Transition Width", &value, 0.1f, 0.0f, 1000.0f, "%.1f m"))
                                    {
                                        property.value = std::to_string(value);
                                        propertiesChanged = true;
                                    }
                                    showHelp("Width of the cross-fade around cascade boundaries. Increase it if transitions are visible.");
                                    renderedCustomLightProperty = true;
                                }
                            }
                            if (!renderedCustomLightProperty)
                            {
                                propertiesChanged |= RenderPropertyEditor(property);
                            }
                            ImGui::PopID();
                        }

                        if (auto *lightComponent = dynamic_cast<scene::LightComponent *>(componentPtr);
                            lightComponent && lightComponent->GetLight().type == scene::LightType::Directional)
                        {
                            const auto &light = lightComponent->GetLight();
                            const int activeCascadeCount = std::clamp(
                                light.activeShadowCascadeCount, 0, scene::kMaxDirectionalShadowCascades);
                            if (activeCascadeCount > 0 && light.shadowCascadeSplits[0] > 0.0f)
                            {
                                ImGui::TextDisabled("Active cascade ranges:");
                                float rangeStart = 0.0f;
                                for (int cascadeIndex = 0; cascadeIndex < activeCascadeCount; ++cascadeIndex)
                                {
                                    ImGui::BulletText("%d: %.1f - %.1f m", cascadeIndex + 1,
                                                      rangeStart, light.shadowCascadeSplits[cascadeIndex]);
                                    rangeStart = light.shadowCascadeSplits[cascadeIndex];
                                }
                            }
                            else
                            {
                                ImGui::TextDisabled("Cascade ranges appear after the scene has rendered.");
                            }
                        }

                        if (propertiesChanged)
                        {
                            if (auto *foliageComponent = dynamic_cast<scene::FoliageComponent *>(componentPtr))
                            {
                                for (const auto &property : properties)
                                {
                                    if (property.name == "PaintEnabled")
                                        foliageComponent->SetPaintEnabled(property.value == "true" || property.value == "1");
                                    else if (property.name == "BrushRadius")
                                        foliageComponent->SetBrushRadius(std::stof(property.value));
                                    else if (property.name == "Density")
                                        foliageComponent->SetDensity(std::stoi(property.value));
                                    else if (property.name == "MinScale")
                                        foliageComponent->SetScaleRange(std::stof(property.value), foliageComponent->GetMaxScale());
                                    else if (property.name == "MaxScale")
                                        foliageComponent->SetScaleRange(foliageComponent->GetMinScale(), std::stof(property.value));
                                    else if (property.name == "MaxDrawDistance")
                                        foliageComponent->SetMaxDrawDistance(std::stof(property.value));
                                    else if (property.name == "MaxShadowDistance")
                                        foliageComponent->SetMaxShadowDistance(std::stof(property.value));
                                    else if (property.name == "MinRenderLod")
                                        foliageComponent->SetMinRenderLod(std::stoi(property.value));
                                    else if (property.name == "MinShadowLod")
                                        foliageComponent->SetMinShadowLod(std::stoi(property.value));
                                    else if (property.name == "CastShadows")
                                        foliageComponent->SetCastShadows(property.value == "true" || property.value == "1");
                                    else if (property.name == "Static")
                                        foliageComponent->SetStatic(property.value == "true" || property.value == "1");
                                }
                            }
                            else
                            {
                                componentPtr->Deserialize(properties);
                            }
                            const auto componentTypeName = GetComponentPrefabTypeName(*componentPtr);
                            if (!componentTypeName.empty())
                            {
                                for (const auto &property : properties)
                                {
                                    entity->AddPrefabOverride("Component:" + componentTypeName + ":" + property.name);
                                }
                            }
                            editorShell.MarkSceneDirty();
                        }

                        ImGui::TreePop();
                        ImGui::PopID();

                        if (componentToRemove)
                        {
                            break;
                        }
                    }

                    if (componentToRemove)
                    {
                        break;
                    }
                }

                if (componentToRemove)
                {
                    const scene::EntityID entityId = entity->GetID();
                    editorShell.ExecuteSceneEdit("Remove Component",
                                                 [entityId, componentToRemove]()
                                                 {
                                                     auto *currentScene = core::Engine::GetInstance().GetScene();
                                                     if (auto *target = currentScene ? currentScene->FindEntityByID(entityId) : nullptr)
                                                     {
                                                         target->RemoveComponent(componentToRemove);
                                                     }
                                                 });
                }
            }
        }
    }

    void InspectorPanel::Shutdown()
    {
        // Cleanup code for the InspectorPanel
    }

    void InspectorPanel::RefreshFilteredSubmeshIndices(const render::Mesh &mesh)
    {
        m_filteredSubmeshIndices.clear();
        m_filteredSubmeshIndices.reserve(mesh.GetSubmeshCount());

        const std::string_view filter(m_submeshFilter.data());
        for (std::size_t index = 0; index < mesh.GetSubmeshCount(); ++index)
        {
            const auto &candidate = mesh.GetSubmesh(index);
            const std::string candidateLabel = candidate.name.empty()
                                                   ? "Submesh " + std::to_string(index)
                                                   : candidate.name;
            if (ContainsInsensitive(candidateLabel, filter))
            {
                m_filteredSubmeshIndices.push_back(index);
            }
        }

        m_appliedSubmeshFilter.assign(filter.data(), filter.size());
    }

    void InspectorPanel::RefreshMaterialSlotUsageSummaries(const scene::MeshComponent &meshComponent)
    {
        const auto *mesh = meshComponent.GetMesh();
        const std::size_t materialCount = meshComponent.GetMaterials().size();
        if (m_materialUsageMesh == mesh && m_materialUsageMaterialCount == materialCount && !m_materialSlotUsageSummaries.empty())
        {
            return;
        }

        m_materialSlotUsageSummaries = BuildMaterialSlotUsageSummaries(meshComponent);
        m_materialUsageMesh = mesh;
        m_materialUsageMaterialCount = materialCount;
    }
}
