#include "EditorSession.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/assets/AnimationGraph.h"
#include "PlutoGE/assets/ParticleSystemAsset.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/components/ComponentFactory.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/ui/ModelAssetPipeline.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/scripting/ScriptEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <typeinfo>
#include <type_traits>
#include <fstream>

namespace
{
    using namespace PlutoGE;

    std::string JsonEscape(std::string_view value)
    {
        std::ostringstream output;
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20)
                {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character) << std::dec;
                }
                else
                {
                    output << static_cast<char>(character);
                }
            }
        }
        return output.str();
    }

    int HexDigit(char character)
    {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    }

    std::string Decode(std::string_view value)
    {
        if (!value.empty() && value.front() == '~') value.remove_prefix(1);
        std::string decoded;
        decoded.reserve(value.size());
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            if (value[index] == '%' && index + 2 < value.size())
            {
                const int high = HexDigit(value[index + 1]);
                const int low = HexDigit(value[index + 2]);
                if (high >= 0 && low >= 0)
                {
                    decoded.push_back(static_cast<char>((high << 4) | low));
                    index += 2;
                    continue;
                }
            }
            decoded.push_back(value[index] == '+' ? ' ' : value[index]);
        }
        return decoded;
    }

    std::string EscapeScriptableText(std::string_view value)
    {
        std::string result;
        for (const char character : value)
        {
            if (character == '\\') result += "\\\\";
            else if (character == '\t') result += "\\t";
            else if (character == '\n') result += "\\n";
            else result += character;
        }
        return result;
    }

    std::string SerializeScriptableValue(const scripting::ScriptFieldValue &value)
    {
        return std::visit([](const auto &typedValue) -> std::string
        {
            using T = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<T, std::monostate>) return {};
            else if constexpr (std::is_same_v<T, bool>) return typedValue ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::string>) return typedValue;
            else if constexpr (std::is_same_v<T, glm::vec2>) return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y);
            else if constexpr (std::is_same_v<T, glm::vec3>) return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y) + "," + std::to_string(typedValue.z);
            else return std::to_string(typedValue);
        }, value);
    }

    std::filesystem::path FindScriptProject(const assets::Project &project)
    {
        std::error_code errorCode;
        for (const auto &entry : std::filesystem::directory_iterator(project.GetRootDirectory(), errorCode))
        {
            if (entry.is_regular_file() && entry.path().filename().string().ends_with(".Scripts.csproj"))
                return entry.path();
        }
        return {};
    }

    std::string SanitizeIdentifier(std::string value)
    {
        std::string result;
        for (const unsigned char character : value)
        {
            if (std::isalnum(character) || character == '_') result.push_back(static_cast<char>(character));
        }
        if (!result.empty() && std::isdigit(static_cast<unsigned char>(result.front()))) result.insert(result.begin(), '_');
        return result;
    }

    std::filesystem::path FindScriptCoreProject()
    {
        auto root = std::filesystem::current_path();
        for (int depth = 0; depth < 8 && !root.empty(); ++depth)
        {
            const auto candidate = root / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "PlutoGE.ScriptCore.csproj";
            if (std::filesystem::exists(candidate)) return candidate;
            const auto parent = root.parent_path();
            if (parent == root) break;
            root = parent;
        }
        return {};
    }

    std::string MakeRelativeOrAbsoluteGenericPath(const std::filesystem::path &path,
                                                  const std::filesystem::path &base)
    {
        std::error_code errorCode;
        const auto relative = std::filesystem::relative(path, base, errorCode);
        if (!errorCode && !relative.empty()) return relative.generic_string();
        errorCode.clear();
        const auto absolute = std::filesystem::absolute(path, errorCode);
        return errorCode ? std::string{} : absolute.lexically_normal().generic_string();
    }

    std::filesystem::path EnsureScriptProject(assets::Project &project, std::string &errorMessage)
    {
        const auto scriptCore = FindScriptCoreProject();
        if (scriptCore.empty()) { errorMessage = "Could not locate PlutoGE.ScriptCore.csproj."; return {}; }
        auto name = SanitizeIdentifier(project.GetManifest().name);
        if (name.empty()) name = "PlutoGEProject";
        const auto existingProjectPath = FindScriptProject(project);
        const auto projectPath = existingProjectPath.empty()
                                     ? project.GetRootDirectory() / (name + ".Scripts.csproj")
                                     : existingProjectPath;
        const auto sourceDirectory = project.GetAssetDirectoryPath() / "Scripts";
        const auto outputDirectory = project.GetAssetDirectoryPath() / "Managed";
        std::error_code errorCode;
        std::filesystem::create_directories(sourceDirectory, errorCode);
        std::filesystem::create_directories(outputDirectory, errorCode);
        if (errorCode) { errorMessage = "Could not create the script project directories."; return {}; }
        const auto sourceDirectoryPath = MakeRelativeOrAbsoluteGenericPath(sourceDirectory, projectPath.parent_path());
        const auto outputPath = MakeRelativeOrAbsoluteGenericPath(outputDirectory, projectPath.parent_path());
        const auto coreReference = MakeRelativeOrAbsoluteGenericPath(scriptCore, projectPath.parent_path());
        const auto coreDirectory = MakeRelativeOrAbsoluteGenericPath(scriptCore.parent_path(), projectPath.parent_path());
        if (sourceDirectoryPath.empty() || outputPath.empty() || coreReference.empty() || coreDirectory.empty())
        {
            errorMessage = "Could not construct the script project paths.";
            return {};
        }
        const auto sourcePattern = sourceDirectoryPath + "/**/*.cs";
        std::ofstream output(projectPath, std::ios::trunc);
        if (!output) { errorMessage = "Could not create the script project file."; return {}; }
        output << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
               << "  <PropertyGroup>\n"
               << "    <TargetFramework>net8.0</TargetFramework>\n"
               << "    <ImplicitUsings>enable</ImplicitUsings>\n"
               << "    <Nullable>enable</Nullable>\n"
               << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
               << "    <CopyLocalLockFileAssemblies>true</CopyLocalLockFileAssemblies>\n"
               << "    <AssemblyName>" << name << ".Scripts</AssemblyName>\n"
               << "    <RootNamespace>" << name << ".Scripts</RootNamespace>\n"
               << "    <OutputPath>" << outputPath << "/</OutputPath>\n"
               << "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n"
               << "  </PropertyGroup>\n"
               << "  <ItemGroup><Compile Include=\"" << sourcePattern << "\" /></ItemGroup>\n"
               << "  <ItemGroup><ProjectReference Include=\"" << coreReference << "\" /></ItemGroup>\n"
               << "  <Target Name=\"CopyScriptCoreRuntimeFiles\" AfterTargets=\"Build\">\n"
               << "    <ItemGroup>\n"
               << "      <ScriptCoreRuntimeFiles Include=\"" << coreDirectory << "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.runtimeconfig.json\" />\n"
               << "      <ScriptCoreRuntimeFiles Include=\"" << coreDirectory << "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.deps.json\" Condition=\"Exists('" << coreDirectory << "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.deps.json')\" />\n"
               << "    </ItemGroup>\n"
               << "    <Copy SourceFiles=\"@(ScriptCoreRuntimeFiles)\" DestinationFolder=\"$(OutputPath)\" SkipUnchangedFiles=\"true\" Condition=\"@(ScriptCoreRuntimeFiles) != ''\" />\n"
               << "  </Target>\n"
               << "</Project>\n";
        if (!output.good()) { errorMessage = "Could not write the script project file."; return {}; }
        project.GetManifest().scriptAssembly = project.MakeAssetReference(outputDirectory / (name + ".Scripts.dll"));
        if (!project.Save(&errorMessage)) return {};
        return projectPath;
    }

    bool LaunchProcess(const std::filesystem::path &executable, std::string &errorMessage)
    {
#ifdef _WIN32
        std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
        PROCESS_INFORMATION process{};
        const auto workingDirectory = executable.parent_path().wstring();
        if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            workingDirectory.c_str(), &startup, &process))
        {
            errorMessage = "Built the project but could not launch it.";
            return false;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
#else
        (void)executable;
        errorMessage = "Launching an exported project is only supported on Windows.";
        return false;
#endif
    }

    bool RebuildStandaloneRuntime(const std::filesystem::path &runtimeExecutable, std::string &errorMessage)
    {
        auto buildDirectory = runtimeExecutable.parent_path();
        for (int depth = 0; depth < 8 && !buildDirectory.empty() && !std::filesystem::exists(buildDirectory / "CMakeCache.txt"); ++depth)
        {
            const auto parent = buildDirectory.parent_path();
            if (parent == buildDirectory) break;
            buildDirectory = parent;
        }
        if (buildDirectory.empty() || !std::filesystem::exists(buildDirectory / "CMakeCache.txt"))
        {
            errorMessage = "Could not determine the CMake build directory for PlutoGERuntime.";
            return false;
        }
        const auto configuration = runtimeExecutable.parent_path().filename().string();
        std::string command = "cmake --build \"" + buildDirectory.string() + "\" --target PlutoGERuntime";
        if (configuration == "Debug" || configuration == "Release" || configuration == "RelWithDebInfo" || configuration == "MinSizeRel")
            command += " --config " + configuration;
        if (std::system(command.c_str()) != 0)
        {
            errorMessage = "Failed to rebuild PlutoGERuntime before exporting the project.";
            return false;
        }
        return true;
    }

    const char *ComponentTypeName(const scene::Component &component)
    {
#define PLUTO_COMPONENT_NAME(Type) if (dynamic_cast<const scene::Type *>(&component)) return #Type
        PLUTO_COMPONENT_NAME(MeshComponent);
        PLUTO_COMPONENT_NAME(TerrainComponent);
        PLUTO_COMPONENT_NAME(FoliageComponent);
        PLUTO_COMPONENT_NAME(ClothComponent);
        PLUTO_COMPONENT_NAME(ParticleSystemComponent);
        PLUTO_COMPONENT_NAME(SplineComponent);
        PLUTO_COMPONENT_NAME(OceanComponent);
        PLUTO_COMPONENT_NAME(AnimationComponent);
        PLUTO_COMPONENT_NAME(SkeletonAttachmentComponent);
        PLUTO_COMPONENT_NAME(CameraComponent);
        PLUTO_COMPONENT_NAME(LightComponent);
        PLUTO_COMPONENT_NAME(RigidbodyComponent);
        PLUTO_COMPONENT_NAME(NavAgentComponent);
        PLUTO_COMPONENT_NAME(NavigationMeshComponent);
        PLUTO_COMPONENT_NAME(ColliderComponent);
        PLUTO_COMPONENT_NAME(IblCaptureComponent);
        PLUTO_COMPONENT_NAME(VolumetricCloudComponent);
        PLUTO_COMPONENT_NAME(PhysicalSkyComponent);
        PLUTO_COMPONENT_NAME(ScriptComponent);
        PLUTO_COMPONENT_NAME(SoundEmitterComponent);
        PLUTO_COMPONENT_NAME(SoundListenerComponent);
        PLUTO_COMPONENT_NAME(CanvasComponent);
        PLUTO_COMPONENT_NAME(RectTransformComponent);
        PLUTO_COMPONENT_NAME(UIImageComponent);
        PLUTO_COMPONENT_NAME(UITextComponent);
        PLUTO_COMPONENT_NAME(UIButtonComponent);
#undef PLUTO_COMPONENT_NAME
        return typeid(component).name();
    }

    scene::Component *ComponentAt(scene::Entity &entity, std::size_t requestedIndex)
    {
        std::size_t index = 0;
        for (const auto &bucket : entity.GetComponentBuckets())
        {
            for (auto *component : bucket)
            {
                if (index++ == requestedIndex) return component;
            }
        }
        return nullptr;
    }

    int PostProcessPropertyType(render::PostProcessParameterType type)
    {
        switch (type)
        {
        case render::PostProcessParameterType::Float: return static_cast<int>(scene::PropertyType::Float);
        case render::PostProcessParameterType::Int: return static_cast<int>(scene::PropertyType::Int);
        case render::PostProcessParameterType::Bool: return static_cast<int>(scene::PropertyType::Bool);
        case render::PostProcessParameterType::Enum: return static_cast<int>(scene::PropertyType::Enum);
        case render::PostProcessParameterType::String:
        default: return static_cast<int>(scene::PropertyType::String);
        }
    }

    void WritePostProcessEffectsJson(
        std::ostream &output,
        const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
    {
        output << '[';
        bool firstEffect = true;
        for (const auto &effect : effects)
        {
            if (!effect) continue;
            if (!firstEffect) output << ',';
            firstEffect = false;
            output << "{\"typeName\":\"" << JsonEscape(effect->GetTypeName())
                   << "\",\"displayName\":\"" << JsonEscape(effect->GetDisplayName())
                   << "\",\"enabled\":" << (effect->IsEnabled() ? "true" : "false")
                   << ",\"parameters\":[";
            const auto parameters = effect->GetParameters();
            for (std::size_t parameterIndex = 0; parameterIndex < parameters.size(); ++parameterIndex)
            {
                const auto &parameter = parameters[parameterIndex];
                if (parameterIndex > 0) output << ',';
                output << "{\"name\":\"" << JsonEscape(parameter.name)
                       << "\",\"type\":" << PostProcessPropertyType(parameter.type)
                       << ",\"value\":\"" << JsonEscape(parameter.value) << "\",\"enumOptions\":[";
                for (std::size_t optionIndex = 0; optionIndex < parameter.enumOptions.size(); ++optionIndex)
                {
                    if (optionIndex > 0) output << ',';
                    output << '"' << JsonEscape(parameter.enumOptions[optionIndex]) << '"';
                }
                output << "]}";
            }
            output << "]}";
        }
        output << ']';
    }

    void WriteEntityJson(std::ostream &output, const scene::Entity &entity)
    {
        const auto position = entity.GetPosition();
        const auto rotation = entity.GetRotation();
        const auto scale = entity.GetScale();
        output << "{\"id\":" << entity.GetID()
               << ",\"parentId\":" << (entity.GetParent() ? entity.GetParent()->GetID() : 0)
               << ",\"name\":\"" << JsonEscape(entity.GetName()) << "\""
               << ",\"active\":" << (entity.IsSelfActive() ? "true" : "false")
               << ",\"position\":[" << position.x << ',' << position.y << ',' << position.z << ']'
               << ",\"rotation\":[" << rotation.x << ',' << rotation.y << ',' << rotation.z << ']'
               << ",\"scale\":[" << scale.x << ',' << scale.y << ',' << scale.z << ']'
               << ",\"components\":[";

        bool firstComponent = true;
        for (const auto &bucket : entity.GetComponentBuckets())
        {
            for (const auto *component : bucket)
            {
                if (!component) continue;
                if (!firstComponent) output << ',';
                firstComponent = false;
                output << "{\"type\":\"" << ComponentTypeName(*component) << "\",\"enabled\":"
                       << (component->IsEnabled() ? "true" : "false") << ",\"properties\":[";
                const auto properties = component->Serialize();
                for (std::size_t propertyIndex = 0; propertyIndex < properties.size(); ++propertyIndex)
                {
                    const auto &property = properties[propertyIndex];
                    if (propertyIndex > 0) output << ',';
                    output << "{\"name\":\"" << JsonEscape(property.name) << "\",\"type\":" << static_cast<int>(property.type)
                           << ",\"value\":\"" << JsonEscape(property.value) << "\",\"enumOptions\":[";
                    for (std::size_t optionIndex = 0; optionIndex < property.enumOptions.size(); ++optionIndex)
                    {
                        if (optionIndex > 0) output << ',';
                        output << '\"' << JsonEscape(property.enumOptions[optionIndex]) << '\"';
                    }
                    output << "]}";
                }
                output << ']';
                if (const auto *camera = dynamic_cast<const scene::CameraComponent *>(component))
                {
                    output << ",\"postProcessPresetReference\":\""
                           << JsonEscape(camera->GetPostProcessPresetAssetReference())
                           << "\",\"postProcessEffects\":";
                    WritePostProcessEffectsJson(output, camera->GetPostProcessEffects());
                }
                output << '}';
            }
        }
        output << "]}";
    }

    void CollectEntities(const scene::Entity &entity, std::vector<const scene::Entity *> &entities)
    {
        entities.push_back(&entity);
        for (const auto *child : entity.GetChildren())
        {
            if (child) CollectEntities(*child, entities);
        }
    }

    std::vector<std::unique_ptr<render::IPostProcessEffect>> CreateDefaultEditorPostProcessEffects()
    {
        return assets::InstantiatePostProcessPreset(assets::CreateDefaultPostProcessPresetAsset());
    }

    std::vector<std::unique_ptr<render::IPostProcessEffect>> CreateEditorPostProcessEffects(
        const std::vector<assets::ProjectPostProcessEffect> &serializedEffects)
    {
        std::vector<std::unique_ptr<render::IPostProcessEffect>> effects;
        effects.reserve(serializedEffects.size());
        for (const auto &serializedEffect : serializedEffects)
        {
            auto effect = render::CreatePostProcessEffect(serializedEffect.typeName);
            if (!effect) continue;

            effect->SetEnabled(serializedEffect.enabled);
            auto parameters = effect->GetParameters();
            for (const auto &serializedParameter : serializedEffect.parameters)
            {
                const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                                                    [&serializedParameter](const render::PostProcessParameter &candidate)
                                                    {
                                                        return candidate.name == serializedParameter.name;
                                                    });
                if (parameter == parameters.end()) continue;
                parameter->type = static_cast<render::PostProcessParameterType>(serializedParameter.type);
                parameter->value = serializedParameter.value;
            }
            effect->SetParameters(parameters);
            effects.push_back(std::move(effect));
        }
        return effects;
    }

    std::vector<assets::ProjectPostProcessEffect> CaptureEditorPostProcessEffects(
        const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
    {
        std::vector<assets::ProjectPostProcessEffect> serializedEffects;
        serializedEffects.reserve(effects.size());
        for (const auto &effect : effects)
        {
            if (!effect) continue;
            assets::ProjectPostProcessEffect serializedEffect;
            serializedEffect.typeName = effect->GetTypeName();
            serializedEffect.enabled = effect->IsEnabled();
            for (const auto &parameter : effect->GetParameters())
            {
                serializedEffect.parameters.push_back(assets::ProjectPostProcessParameter{
                    .name = parameter.name,
                    .type = static_cast<int>(parameter.type),
                    .value = parameter.value,
                });
            }
            serializedEffects.push_back(std::move(serializedEffect));
        }
        return serializedEffects;
    }
}

EditorSession::EditorSession(PlutoGE::core::Engine &engine) : m_engine(engine) {}
EditorSession::~EditorSession()
{
    Shutdown();
}

void EditorSession::Shutdown()
{
    if (m_bakeTask) m_bakeTask->Cancel();
    m_bakeTask.reset();
    m_engine.StopRuntime();
    m_engine.SetScene(nullptr);
    m_scene.reset();
    m_editorPostProcessEffects.clear();
    m_editorPostProcessPresetReference.clear();
    m_engine.GetAssetManager().ClearProjectContext();
    m_project.reset();
    m_projectPath.clear();
}

bool EditorSession::Initialize()
{
    m_editorPostProcessEffects = CreateDefaultEditorPostProcessEffects();
    m_editorPostProcessPresetReference.clear();
    auto scene = std::make_unique<PlutoGE::scene::Scene>();
    auto camera = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Main Camera"});
    auto *cameraEntity = scene->AddEntity(std::move(camera));
    cameraEntity->SetPosition({0.0f, 2.0f, 5.0f});
    auto *cameraComponent = static_cast<PlutoGE::scene::CameraComponent *>(PlutoGE::scene::AddComponentByTypeName(*cameraEntity, "CameraComponent"));
    cameraComponent->SetMainCamera(true);
    auto light = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Directional Light"});
    auto *lightEntity = scene->AddEntity(std::move(light));
    auto *lightComponent = static_cast<PlutoGE::scene::LightComponent *>(PlutoGE::scene::AddComponentByTypeName(*lightEntity, "LightComponent"));
    lightComponent->SetLightType(PlutoGE::scene::LightType::Directional);
    lightComponent->SetIntensity(4.0f);
    scene->AddEntity(std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Environment"}));
    return SetScene(std::move(scene), false);
}

PlutoGE::scene::Scene *EditorSession::GetScene() const { return m_scene.get(); }

void EditorSession::Update(float deltaTime)
{
    if (m_bakeTask)
    {
        m_bakeStatus = m_bakeTask->GetStatusMessage();
        if (m_bakeTask->IsFinished())
        {
            const auto result = m_bakeTask->Finalize(*m_scene);
            m_bakeStatus = result.message;
            if (result.succeeded) m_dirty = true;
            m_bakeTask.reset();
        }
    }
    m_engine.UpdateAsyncMeshImports();
    if (m_scene) m_scene->Update(deltaTime);
}

bool EditorSession::SetViewportStats(int submittedRenderCommands, int visibleRenderCommands)
{
    if (m_submittedRenderCommands == submittedRenderCommands && m_visibleRenderCommands == visibleRenderCommands)
    {
        return false;
    }
    m_submittedRenderCommands = submittedRenderCommands;
    m_visibleRenderCommands = visibleRenderCommands;
    return true;
}

bool EditorSession::SetScene(std::unique_ptr<PlutoGE::scene::Scene> scene, bool dirty)
{
    if (!scene) return false;
    m_engine.StopRuntime();
    m_engine.SetScene(nullptr);
    m_scene = std::move(scene);
    m_engine.SetScene(m_scene.get());
    m_selectedEntityId = 0;
    m_dirty = dirty;
    return true;
}

bool EditorSession::CaptureScene(std::string &state) const
{
    std::string error;
    return m_scene && PlutoGE::scene::SceneSerializer::SaveToString(*m_scene, state, &error);
}

bool EditorSession::RestoreScene(const std::string &state, bool dirty)
{
    std::string error;
    auto restored = PlutoGE::scene::SceneSerializer::LoadFromString(state, &error);
    const auto selected = m_selectedEntityId;
    if (!SetScene(std::move(restored), dirty)) return false;
    if (m_scene->FindEntityByID(selected)) m_selectedEntityId = selected;
    return true;
}

bool EditorSession::CommitEdit(const std::string &before)
{
    std::string after;
    if (!CaptureScene(after) || before == after) return false;
    m_undo.push_back({before, std::move(after)});
    if (m_undo.size() > 64) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_dirty = true;
    return true;
}

PlutoGE::scene::Entity *EditorSession::FindEntity(std::uint32_t id) const
{
    return m_scene ? m_scene->FindEntityByID(id) : nullptr;
}

PlutoGE::scene::Entity *EditorSession::GetSelectedEntity() const
{
    return FindEntity(m_selectedEntityId);
}

void EditorSession::SetSelectedEntity(PlutoGE::scene::Entity *entity)
{
    m_selectedEntityId = entity ? entity->GetID() : 0;
}

bool EditorSession::BeginGizmoEdit()
{
    if (m_gizmoEditActive) return true;
    m_gizmoEditBefore.clear();
    m_gizmoEditActive = CaptureScene(m_gizmoEditBefore);
    return m_gizmoEditActive;
}

bool EditorSession::EndGizmoEdit()
{
    if (!m_gizmoEditActive) return false;
    m_gizmoEditActive = false;
    const bool committed = CommitEdit(m_gizmoEditBefore);
    m_gizmoEditBefore.clear();
    return committed;
}

bool EditorSession::HandleCommand(const std::string &commandLine, std::string &errorMessage)
{
    std::istringstream input(commandLine);
    std::string command;
    input >> command;
    if (command == "editor_snapshot") return true;

    if (command == "select")
    {
        input >> m_selectedEntityId;
        if (m_selectedEntityId != 0 && !FindEntity(m_selectedEntityId)) m_selectedEntityId = 0;
        return true;
    }

    if (command == "gizmo_operation")
    {
        std::string operation;
        input >> operation;
        if (operation == "rotate") m_gizmoOperation = GizmoOperation::Rotate;
        else if (operation == "scale") m_gizmoOperation = GizmoOperation::Scale;
        else m_gizmoOperation = GizmoOperation::Translate;
        return true;
    }

    if (command == "gizmo_space")
    {
        std::string space;
        input >> space;
        m_gizmoSpace = space == "world" ? GizmoSpace::World : GizmoSpace::Local;
        return true;
    }

    if (command == "set_editor_camera")
    {
        int gridVisible = m_editorCamera.gridVisible ? 1 : 0;
        input >> m_editorCamera.position.x >> m_editorCamera.position.y >> m_editorCamera.position.z
              >> m_editorCamera.yawDegrees >> m_editorCamera.pitchDegrees
              >> m_editorCamera.fovY >> m_editorCamera.nearPlane >> m_editorCamera.farPlane
              >> m_editorCamera.moveSpeed >> m_editorCamera.speedAdjustment >> gridVisible;
        m_editorCamera.pitchDegrees = std::clamp(m_editorCamera.pitchDegrees, -89.0f, 89.0f);
        m_editorCamera.fovY = std::clamp(m_editorCamera.fovY, 1.0f, 179.0f);
        m_editorCamera.nearPlane = std::max(m_editorCamera.nearPlane, 0.001f);
        m_editorCamera.farPlane = std::max(m_editorCamera.farPlane, m_editorCamera.nearPlane + 0.001f);
        m_editorCamera.moveSpeed = std::clamp(m_editorCamera.moveSpeed, 0.1f, 1000.0f);
        m_editorCamera.speedAdjustment = std::clamp(m_editorCamera.speedAdjustment, 0.1f, 10.0f);
        m_editorCamera.gridVisible = gridVisible != 0;
        return true;
    }

    if (command.rfind("editor_effect_", 0) == 0)
    {
        if (m_engine.IsRuntimeRunning())
        {
            errorMessage = "Stop play mode before editing the editor camera post-process stack.";
            return false;
        }

        if (command == "editor_effect_add")
        {
            std::string type;
            input >> type;
            if (auto effect = PlutoGE::render::CreatePostProcessEffect(Decode(type)))
            {
                m_editorPostProcessEffects.push_back(std::move(effect));
            }
        }
        else if (command == "editor_effect_remove")
        {
            std::size_t index = 0;
            input >> index;
            if (index < m_editorPostProcessEffects.size())
            {
                m_engine.GetWindow().EnsureOpenGLContextCurrent(true);
                m_editorPostProcessEffects.erase(m_editorPostProcessEffects.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
        else if (command == "editor_effect_move")
        {
            std::size_t from = 0, to = 0;
            input >> from >> to;
            if (from < m_editorPostProcessEffects.size() && to < m_editorPostProcessEffects.size() && from != to)
            {
                auto effect = std::move(m_editorPostProcessEffects[from]);
                m_editorPostProcessEffects.erase(m_editorPostProcessEffects.begin() + static_cast<std::ptrdiff_t>(from));
                m_editorPostProcessEffects.insert(m_editorPostProcessEffects.begin() + static_cast<std::ptrdiff_t>(to), std::move(effect));
            }
        }
        else if (command == "editor_effect_enabled")
        {
            std::size_t index = 0;
            int enabled = 1;
            input >> index >> enabled;
            if (index < m_editorPostProcessEffects.size() && m_editorPostProcessEffects[index])
            {
                m_editorPostProcessEffects[index]->SetEnabled(enabled != 0);
            }
        }
        else if (command == "editor_effect_parameter")
        {
            std::size_t effectIndex = 0, parameterIndex = 0;
            std::string value;
            input >> effectIndex >> parameterIndex >> value;
            if (effectIndex < m_editorPostProcessEffects.size() && m_editorPostProcessEffects[effectIndex])
            {
                auto parameters = m_editorPostProcessEffects[effectIndex]->GetParameters();
                if (parameterIndex < parameters.size())
                {
                    parameters[parameterIndex].value = Decode(value);
                    m_editorPostProcessEffects[effectIndex]->SetParameters(parameters);
                }
            }
        }
        else if (command == "editor_effect_preset")
        {
            std::string reference;
            input >> reference;
            const auto decodedReference = Decode(reference);
            if (decodedReference.empty())
            {
                m_editorPostProcessEffects.clear();
                m_editorPostProcessPresetReference.clear();
            }
            else
            {
                bool loaded = false;
                const auto preset = m_engine.GetAssetManager().LoadPostProcessPresetAsset(decodedReference, &loaded);
                if (!loaded)
                {
                    errorMessage = "Could not load editor camera post-process preset: " + decodedReference;
                    return false;
                }
                m_editorPostProcessEffects = PlutoGE::assets::InstantiatePostProcessPreset(preset);
                m_editorPostProcessPresetReference = decodedReference;
            }
        }
        else if (command == "editor_effect_save_preset")
        {
            if (m_editorPostProcessPresetReference.empty())
            {
                errorMessage = "Set an editor camera post-process preset reference before saving.";
                return false;
            }
            const auto preset = PlutoGE::assets::CapturePostProcessPreset(m_editorPostProcessEffects);
            if (!m_engine.GetAssetManager().SavePostProcessPresetAsset(m_editorPostProcessPresetReference, preset, &errorMessage)) return false;
        }
        else if (command == "editor_effect_save_preset_as")
        {
            std::string encodedReference;
            input >> encodedReference;
            const std::string reference = Decode(encodedReference);
            if (!m_project || PlutoGE::assets::Project::GetAssetTypeForReference(reference) != PlutoGE::assets::ProjectAssetType::PostProcessPreset)
            {
                errorMessage = "Choose a project .plutopostprocess asset reference.";
                return false;
            }
            const auto path = m_project->ResolveAssetReference(reference);
            if (std::filesystem::exists(path))
            {
                errorMessage = "A post-process preset already exists at that path.";
                return false;
            }
            const auto preset = PlutoGE::assets::CapturePostProcessPreset(m_editorPostProcessEffects);
            if (!m_engine.GetAssetManager().SavePostProcessPresetAsset(reference, preset, &errorMessage)) return false;
            m_editorPostProcessPresetReference = reference;
            m_project->RefreshAssetRegistry();
        }
        else
        {
            errorMessage = "Unknown editor camera post-process command.";
            return false;
        }
        return true;
    }

    if (command == "reset_editor_camera")
    {
        m_editorCamera = EditorCameraState{};
        return true;
    }

    if (command == "frame_selected")
    {
        if (auto *entity = FindEntity(m_selectedEntityId))
        {
            const glm::vec3 target = entity->GetWorldPosition();
            glm::vec3 direction = target - m_editorCamera.position;
            if (glm::dot(direction, direction) < 0.0001f)
            {
                m_editorCamera.position = target + glm::vec3(0.0f, 2.0f, 6.0f);
                direction = target - m_editorCamera.position;
            }
            direction = glm::normalize(direction);
            m_editorCamera.yawDegrees = glm::degrees(std::atan2(-direction.x, -direction.z));
            m_editorCamera.pitchDegrees = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
        }
        return true;
    }

    if (command == "undo" || command == "redo")
    {
        auto &source = command == "undo" ? m_undo : m_redo;
        auto &destination = command == "undo" ? m_redo : m_undo;
        if (source.empty()) return true;
        auto entry = std::move(source.back());
        source.pop_back();
        const bool restored = RestoreScene(command == "undo" ? entry.before : entry.after, true);
        if (restored) destination.push_back(std::move(entry));
        return true;
    }

    if (command == "new_scene")
    {
        m_scenePath.clear();
        m_undo.clear();
        m_redo.clear();
        return SetScene(std::make_unique<PlutoGE::scene::Scene>(), false);
    }

    if (command == "set_project_settings")
    {
        if (!m_project)
        {
            errorMessage = "No project is open.";
            return false;
        }

        int persist = 0;
        int windowWidth = 0;
        int windowHeight = 0;
        int vSyncEnabled = 0;
        std::string encodedName;
        std::string encodedStartupScene;
        std::string encodedScriptAssembly;
        std::string encodedWindowTitle;
        if (!(input >> persist >> encodedName >> encodedStartupScene >> encodedScriptAssembly >> encodedWindowTitle
                    >> windowWidth >> windowHeight >> vSyncEnabled))
        {
            errorMessage = "Invalid project settings command.";
            return false;
        }

        auto name = Decode(encodedName);
        const auto startupScene = Decode(encodedStartupScene);
        const auto scriptAssembly = Decode(encodedScriptAssembly);
        auto windowTitle = Decode(encodedWindowTitle);
        if (name.empty())
        {
            errorMessage = "Project name cannot be empty.";
            return false;
        }

        std::string startupSceneReference;
        if (!startupScene.empty())
        {
            startupSceneReference = m_project->FindSceneAssetReference(startupScene);
            if (startupSceneReference.empty())
            {
                errorMessage = "The selected startup scene is not a project scene asset.";
                return false;
            }
        }

        if (!scriptAssembly.empty() &&
            PlutoGE::assets::Project::GetAssetTypeForReference(scriptAssembly) != PlutoGE::assets::ProjectAssetType::Assembly)
        {
            errorMessage = "The script assembly must be a project assembly asset.";
            return false;
        }

        auto &manifest = m_project->GetManifest();
        manifest.name = std::move(name);
        manifest.startupScene = std::move(startupSceneReference);
        manifest.scriptAssembly = scriptAssembly;
        manifest.windowTitle = std::move(windowTitle);
        manifest.windowWidth = std::clamp(windowWidth, 64, 16384);
        manifest.windowHeight = std::clamp(windowHeight, 64, 16384);
        manifest.vSyncEnabled = vSyncEnabled != 0;
        m_engine.GetRenderer().SetVSyncEnabled(manifest.vSyncEnabled);

        if (persist != 0 && !m_project->Save(&errorMessage))
        {
            return false;
        }
        return true;
    }

    if (command == "load_project" || command == "create_project")
    {
        std::string encodedPath;
        input >> encodedPath;
        const auto path = Decode(encodedPath);
        std::unique_ptr<PlutoGE::assets::Project> project;
        if (command == "create_project")
        {
            std::string encodedName;
            input >> encodedName;
            project = PlutoGE::assets::Project::Create(path, Decode(encodedName), &errorMessage);
        }
        else
        {
            project = PlutoGE::assets::Project::Load(path, &errorMessage);
        }
        if (!project) return false;

        m_engine.GetAssetManager().SetProjectContext(project->GetRootDirectory().string(), project->GetManifest().assetDirectory);
        project->RefreshAssetRegistry();
        const auto &manifest = project->GetManifest();
        m_engine.GetRenderer().SetVSyncEnabled(manifest.vSyncEnabled);
        m_editorCamera.position = {manifest.editorCamera.positionX, manifest.editorCamera.positionY, manifest.editorCamera.positionZ};
        m_editorCamera.moveSpeed = std::clamp(manifest.editorCamera.moveSpeed, 0.1f, 1000.0f);
        m_editorCamera.speedAdjustment = 1.0f;
        m_editorCamera.yawDegrees = manifest.editorCamera.yawDegrees;
        m_editorCamera.pitchDegrees = std::clamp(manifest.editorCamera.pitchDegrees, -89.0f, 89.0f);
        m_editorCamera.fovY = std::clamp(manifest.editorCamera.fovY, 1.0f, 179.0f);
        m_editorCamera.nearPlane = std::max(manifest.editorCamera.nearPlane, 0.001f);
        m_editorCamera.farPlane = std::max(manifest.editorCamera.farPlane, m_editorCamera.nearPlane + 0.001f);
        m_editorPostProcessEffects = CreateDefaultEditorPostProcessEffects();
        m_editorPostProcessPresetReference.clear();
        bool loadedEditorPostProcessPreset = false;
        if (!manifest.editorCameraPostProcessPreset.empty())
        {
            bool loaded = false;
            const auto preset = m_engine.GetAssetManager().LoadPostProcessPresetAsset(manifest.editorCameraPostProcessPreset, &loaded);
            if (loaded)
            {
                m_editorPostProcessEffects = PlutoGE::assets::InstantiatePostProcessPreset(preset);
                m_editorPostProcessPresetReference = manifest.editorCameraPostProcessPreset;
                loadedEditorPostProcessPreset = true;
            }
        }
        if (!loadedEditorPostProcessPreset && !manifest.editorCameraPostProcessEffects.empty())
        {
            m_editorPostProcessEffects = CreateEditorPostProcessEffects(manifest.editorCameraPostProcessEffects);
        }

        std::unique_ptr<PlutoGE::scene::Scene> loadedScene;
        std::string startupSceneReference;
        std::string startupScenePath;
        bool loadedStartupScene = false;
        if (!manifest.startupScene.empty())
        {
            // FindSceneAssetReference accepts project:// references, absolute
            // paths, legacy relative paths, file names, and scene stems.
            startupSceneReference = project->FindSceneAssetReference(manifest.startupScene);
        }
        else
        {
            // Older editor projects did not persist the active scene. Opening
            // the sole scene is an unambiguous and useful migration path.
            for (const auto &asset : manifest.assetEntries)
            {
                if (asset.type != PlutoGE::assets::ProjectAssetType::Scene) continue;
                if (!startupSceneReference.empty()) { startupSceneReference.clear(); break; }
                startupSceneReference = asset.reference;
            }
        }
        if (!startupSceneReference.empty())
        {
            startupScenePath = project->ResolveAssetReference(startupSceneReference).string();
            loadedScene = PlutoGE::scene::SceneSerializer::Load(startupScenePath, &errorMessage);
            loadedStartupScene = loadedScene != nullptr;
        }
        if (!loadedScene) loadedScene = std::make_unique<PlutoGE::scene::Scene>();

        m_project = std::move(project);
        m_projectPath = path;
        m_scenePath = loadedStartupScene ? startupScenePath : std::string{};
        m_undo.clear();
        m_redo.clear();
        return SetScene(std::move(loadedScene), false);
    }

    if (command == "load_scene")
    {
        std::string encodedPath;
        input >> encodedPath;
        const auto path = Decode(encodedPath);
        auto loaded = PlutoGE::scene::SceneSerializer::Load(path, &errorMessage);
        if (!loaded) return false;
        m_scenePath = path;
        m_undo.clear();
        m_redo.clear();
        return SetScene(std::move(loaded), false);
    }

    if (command == "save_project")
    {
        if (!m_project)
        {
            errorMessage = "No project is open.";
            return false;
        }
        auto &manifest = m_project->GetManifest();
        if (manifest.startupScene.empty() && !m_scenePath.empty() && m_project->IsInAssetDirectory(m_scenePath))
        {
            manifest.startupScene = m_project->MakeAssetReference(m_scenePath);
        }
        manifest.editorCamera = PlutoGE::assets::ProjectEditorCameraSettings{
            .positionX = m_editorCamera.position.x,
            .positionY = m_editorCamera.position.y,
            .positionZ = m_editorCamera.position.z,
            .moveSpeed = m_editorCamera.moveSpeed,
            .yawDegrees = m_editorCamera.yawDegrees,
            .pitchDegrees = m_editorCamera.pitchDegrees,
            .fovY = m_editorCamera.fovY,
            .nearPlane = m_editorCamera.nearPlane,
            .farPlane = m_editorCamera.farPlane,
        };
        manifest.editorCameraPostProcessPreset = m_editorPostProcessPresetReference;
        manifest.editorCameraPostProcessEffects = m_editorPostProcessPresetReference.empty()
                                                    ? CaptureEditorPostProcessEffects(m_editorPostProcessEffects)
                                                    : std::vector<PlutoGE::assets::ProjectPostProcessEffect>{};
        return m_project->Save(&errorMessage);
    }

    if (command == "save_scene")
    {
        std::string encodedPath;
        input >> encodedPath;
        const auto path = Decode(encodedPath);
        if (!path.empty()) m_scenePath = path;
        if (m_scenePath.empty()) { errorMessage = "No scene path was provided."; return false; }
        if (!PlutoGE::scene::SceneSerializer::Save(*m_scene, m_scenePath, &errorMessage)) return false;
        m_dirty = false;
        if (m_project && m_project->IsInAssetDirectory(m_scenePath))
        {
            if (m_project->GetManifest().startupScene.empty())
            {
                m_project->GetManifest().startupScene = m_project->MakeAssetReference(m_scenePath);
            }
            m_project->RefreshAssetRegistry();
            if (!m_project->Save(&errorMessage)) return false;
        }
        return true;
    }

    if (command == "build_project")
    {
        std::string encodedPath;
        int runAfterBuild = 0;
        input >> encodedPath >> runAfterBuild;
        const auto destination = std::filesystem::path(Decode(encodedPath));
        if (!m_project || destination.empty())
        {
            errorMessage = "Open a project and choose an export path before building.";
            return false;
        }
        const auto scriptProject = FindScriptProject(*m_project);
        const auto scriptSourceDirectory = m_project->GetAssetDirectoryPath() / "Scripts";
        if (!m_project->GetManifest().scriptAssembly.empty() || !scriptProject.empty() || std::filesystem::exists(scriptSourceDirectory))
        {
            if (!HandleCommand("build_scripts", errorMessage))
            {
                errorMessage = "Game export stopped because project scripts failed to build. " + errorMessage;
                return false;
            }
        }
        if (!m_scenePath.empty() && !PlutoGE::scene::SceneSerializer::Save(*m_scene, m_scenePath, &errorMessage)) return false;
        if (!m_scenePath.empty() && m_project->IsInAssetDirectory(m_scenePath) && m_project->GetManifest().startupScene.empty())
            m_project->GetManifest().startupScene = m_project->MakeAssetReference(m_scenePath);
        m_project->RefreshAssetRegistry();
        if (!m_project->Save(&errorMessage)) return false;
        const auto runtime = PlutoGE::assets::FindRuntimeExecutable(std::filesystem::current_path());
        if (runtime.empty())
        {
            errorMessage = "Could not find PlutoGERuntime executable to export.";
            return false;
        }
        if (!RebuildStandaloneRuntime(runtime, errorMessage)) return false;
        if (!PlutoGE::assets::ExportStandaloneProject(*m_project, destination, runtime, &errorMessage)) return false;
        m_dirty = false;
        return runAfterBuild == 0 || LaunchProcess(destination, errorMessage);
    }

    if (command == "build_scripts")
    {
        if (!m_project) { errorMessage = "Open a project before building scripts."; return false; }
        const auto projectPath = EnsureScriptProject(*m_project, errorMessage);
        if (projectPath.empty()) return false;
        const bool wasRunning = m_engine.IsRuntimeRunning();
        if (wasRunning) m_engine.StopRuntime();
        auto &scriptEngine = m_engine.GetScriptEngine();
        scriptEngine.Shutdown();
        const auto assembly = m_project->ResolveAssetReference(m_project->GetManifest().scriptAssembly);
        if (assembly.empty())
        {
            scriptEngine.Initialize();
            errorMessage = "The project script assembly path is invalid.";
            return false;
        }
        PlutoGE::scripting::ScriptBuildConfig config;
        config.projectPath = projectPath;
        config.outputDirectory = assembly.parent_path();
        config.configuration = "Debug";
        config.framework = "net8.0";
        const auto result = scriptEngine.BuildProject(config);
        scriptEngine.Initialize();
        if (!result.succeeded)
        {
            errorMessage = "Script build failed with exit code " + std::to_string(result.exitCode) + ".";
            const bool restoredPreviousAssembly = std::filesystem::exists(assembly) && scriptEngine.LoadAssembly(assembly);
            if (wasRunning && restoredPreviousAssembly) m_engine.StartRuntime();
            return false;
        }
        if (!std::filesystem::exists(assembly))
        {
            errorMessage = "Script build completed without producing the configured assembly: " + assembly.string();
            return false;
        }
        if (!assembly.empty() && !scriptEngine.LoadAssembly(assembly))
        {
            errorMessage = scriptEngine.GetLastError();
            return false;
        }
        m_project->RefreshAssetRegistry();
        if (!m_project->Save(&errorMessage))
        {
            if (wasRunning) m_engine.StartRuntime();
            return false;
        }
        if (wasRunning) m_engine.StartRuntime();
        return true;
    }

    if (command == "reload_scripts")
    {
        if (!m_project) { errorMessage = "Open a project before reloading scripts."; return false; }
        auto &scriptEngine = m_engine.GetScriptEngine();
        const bool wasRunning = m_engine.IsRuntimeRunning();
        if (wasRunning) m_engine.StopRuntime();
        scriptEngine.Shutdown();
        scriptEngine.Initialize();
        const auto assembly = m_project->ResolveAssetReference(m_project->GetManifest().scriptAssembly);
        if (assembly.empty() || !std::filesystem::exists(assembly))
        {
            errorMessage = "The configured script assembly does not exist: " + assembly.string();
            return false;
        }
        if (!scriptEngine.LoadAssembly(assembly))
        {
            errorMessage = scriptEngine.GetLastError();
            return false;
        }
        if (wasRunning) m_engine.StartRuntime();
        return true;
    }

    if (command == "create_script")
    {
        std::string encodedName;
        input >> encodedName;
        if (!m_project) { errorMessage = "Open a project before creating scripts."; return false; }
        auto className = SanitizeIdentifier(Decode(encodedName));
        if (className.empty()) { errorMessage = "Enter a valid script class name."; return false; }
        if (EnsureScriptProject(*m_project, errorMessage).empty()) return false;
        const auto scriptPath = m_project->GetAssetDirectoryPath() / "Scripts" / (className + ".cs");
        if (std::filesystem::exists(scriptPath)) { errorMessage = "A script with that name already exists."; return false; }
        std::ofstream output(scriptPath);
        output << "using PlutoGE.ScriptCore;\n\npublic sealed class " << className << " : ScriptBehaviour\n{\n    public override void OnCreate()\n    {\n    }\n\n    public override void OnUpdate(float deltaTime)\n    {\n    }\n}\n";
        if (!output.good()) { errorMessage = "Could not create the script source file."; return false; }
        m_project->RefreshAssetRegistry();
        return m_project->Save(&errorMessage);
    }

    if (command == "bake_scene")
    {
        if (!m_scene || m_bakeTask) { errorMessage = m_bakeTask ? "A scene bake is already running." : "No scene is loaded."; return false; }
        std::string encodedPreset;
        input >> encodedPreset;
        const auto preset = Decode(encodedPreset);
        auto settings = preset == "fast" ? PlutoGE::scene::SceneBakeSettings::FastPreview()
                      : preset == "final" ? PlutoGE::scene::SceneBakeSettings::Final()
                                            : PlutoGE::scene::SceneBakeSettings::BalancedPreview();
        if (preset == "custom")
        {
            int indirect = 1, probes = 1;
            input >> settings.lightmapResolution >> settings.lightmapTileSize >> settings.probeDirectionCount
                  >> settings.indirectBounceSampleCount >> indirect >> settings.probeBounceStrength
                  >> settings.lightmapBounceStrength >> probes;
            settings.bakeIndirectBounce = indirect != 0;
            settings.bakeProbeVolume = probes != 0;
        }
        PlutoGE::scene::SceneBakeResult immediate;
        PlutoGE::scene::SceneBaker baker;
        m_bakeTask = baker.BeginBake(*m_scene, settings, &immediate);
        if (!m_bakeTask)
        {
            errorMessage = immediate.message.empty() ? "Could not start scene bake." : immediate.message;
            return false;
        }
        m_bakeStatus = m_bakeTask->GetStatusMessage();
        return true;
    }

    if (command == "cancel_bake")
    {
        if (m_bakeTask) { m_bakeTask->Cancel(); m_bakeStatus = "Cancelling bake…"; }
        return true;
    }

    if (command == "force_show_cursor")
    {
        int enabled = 0;
        input >> enabled;
        m_engine.GetWindow().SetCursorLockOverride(enabled != 0);
        if (enabled) m_engine.GetWindow().SetEditorCursorLocked(false);
        return true;
    }

    if (command == "viewport_debug_view")
    {
        int view = 0;
        input >> view;
        view = std::clamp(view, 0, static_cast<int>(PlutoGE::render::PostProcessDebugView::Lod));
        m_engine.GetRenderer().SetPostProcessDebugView(static_cast<PlutoGE::render::PostProcessDebugView>(view));
        return true;
    }

    if (command == "viewport_settings")
    {
        int view = 0, debugShapes = 1, snapEnabled = 0;
        input >> view >> debugShapes >> snapEnabled >> m_translateSnap >> m_rotateSnap >> m_scaleSnap;
        view = std::clamp(view, 0, static_cast<int>(PlutoGE::render::PostProcessDebugView::Lod));
        m_engine.GetRenderer().SetPostProcessDebugView(static_cast<PlutoGE::render::PostProcessDebugView>(view));
        m_debugShapes = debugShapes != 0;
        m_snapEnabled = snapEnabled != 0;
        m_translateSnap = std::max(0.001f, m_translateSnap);
        m_rotateSnap = std::max(0.1f, m_rotateSnap);
        m_scaleSnap = std::max(0.001f, m_scaleSnap);
        return true;
    }

    if (command == "scene_environment")
    {
        std::string encodedPath;
        float intensity = 1.0f;
        input >> encodedPath >> intensity;
        const auto path = Decode(encodedPath);
        if (path.empty()) m_scene->ClearEnvironmentMap();
        else
        {
            auto *texture = m_engine.GetTextureManager().LoadEnvironmentTextureFromFile(path.c_str());
            if (!texture) { errorMessage = "Failed to load the environment map."; return false; }
            m_scene->SetEnvironmentMap(texture, path);
        }
        m_scene->SetEnvironmentIntensity(std::max(0.0f, intensity));
        m_dirty = true;
        return true;
    }

    if (command == "runtime")
    {
        int start = 0;
        input >> start;
        if (start && !m_engine.IsRuntimeRunning())
        {
            CaptureScene(m_runtimeSnapshot);
            m_engine.StartRuntime();
        }
        else if (!start && m_engine.IsRuntimeRunning())
        {
            m_engine.StopRuntime();
            if (!m_runtimeSnapshot.empty()) RestoreScene(m_runtimeSnapshot, m_dirty);
            m_runtimeSnapshot.clear();
        }
        return true;
    }

    if (m_engine.IsRuntimeRunning())
    {
        errorMessage = "Stop play mode before editing the scene.";
        return false;
    }

    std::string before;
    CaptureScene(before);

    if (command == "refresh_assets")
    {
        if (!m_project)
        {
            errorMessage = "Open a project before refreshing assets.";
            return false;
        }
        m_project->RefreshAssetRegistry();
        if (!m_project->Save(&errorMessage)) return false;
        return true;
    }

    if (command == "create_asset")
    {
        std::string encodedType, encodedReference, encodedClassName;
        input >> encodedType >> encodedReference >> encodedClassName;
        const std::string type = Decode(encodedType);
        const std::string reference = Decode(encodedReference);
        const std::string className = Decode(encodedClassName);
        if (!m_project || !reference.starts_with("project://"))
        {
            errorMessage = "Open a project before creating assets.";
            return false;
        }
        const auto path = m_project->ResolveAssetReference(reference);
        if (path.empty() || std::filesystem::exists(path))
        {
            errorMessage = "An asset already exists at that path.";
            return false;
        }

        bool saved = false;
        if (type == "material" && PlutoGE::assets::Project::GetAssetTypeForReference(reference) == PlutoGE::assets::ProjectAssetType::Material)
        {
            PlutoGE::render::MaterialConfig material;
            material.color = {0.82f, 0.84f, 0.88f, 1.0f};
            material.roughness = 0.55f;
            saved = m_engine.GetAssetManager().SaveMaterialAsset(reference, material, &errorMessage);
        }
        else if (type == "post-process" && PlutoGE::assets::Project::GetAssetTypeForReference(reference) == PlutoGE::assets::ProjectAssetType::PostProcessPreset)
        {
            saved = m_engine.GetAssetManager().SavePostProcessPresetAsset(reference, PlutoGE::assets::CreateDefaultPostProcessPresetAsset(), &errorMessage);
        }
        else if (type == "particle-system" && PlutoGE::assets::Project::GetAssetTypeForReference(reference) == PlutoGE::assets::ProjectAssetType::ParticleSystem)
        {
            saved = m_engine.GetAssetManager().SaveParticleSystemAsset(reference, PlutoGE::assets::CreateDefaultParticleSystemAsset(), &errorMessage);
        }
        else if (type == "shader-graph" && PlutoGE::assets::Project::GetAssetTypeForReference(reference) == PlutoGE::assets::ProjectAssetType::ShaderGraph)
        {
            saved = m_engine.GetAssetManager().SaveShaderGraphAsset(reference, PlutoGE::render::CreateDefaultShaderGraph(), &errorMessage);
        }
        else if (type == "animation-graph" && PlutoGE::assets::Project::GetAssetTypeForReference(reference) == PlutoGE::assets::ProjectAssetType::AnimationGraph)
        {
            saved = m_engine.GetAssetManager().SaveAnimationGraphAsset(reference, PlutoGE::assets::CreateDefaultAnimationGraphAsset(), &errorMessage);
        }
        else if (type == "scriptable-object" && PlutoGE::assets::Project::GetAssetTypeForReference(reference) == PlutoGE::assets::ProjectAssetType::ScriptableObject)
        {
            const auto *definition = m_engine.GetScriptEngine().FindClass(className);
            if (!definition || definition->kind != PlutoGE::scripting::ScriptClassKind::ScriptableObject)
            {
                errorMessage = "Choose a concrete ScriptableObject class.";
                return false;
            }
            std::error_code errorCode;
            std::filesystem::create_directories(path.parent_path(), errorCode);
            std::ofstream output(path, std::ios::trunc);
            if (!output.is_open()) errorMessage = "Could not create the ScriptableObject asset.";
            else
            {
                output << "SCRIPTABLE\t" << EscapeScriptableText(className) << '\n';
                for (const auto &field : definition->fields)
                {
                    if (!field.serialized) continue;
                    output << "FIELD\t" << EscapeScriptableText(field.name) << '\t'
                           << static_cast<int>(field.type) << '\t'
                           << EscapeScriptableText(SerializeScriptableValue(field.defaultValue)) << '\n';
                }
                saved = output.good();
                if (!saved) errorMessage = "Could not write the ScriptableObject asset.";
            }
        }
        else errorMessage = "Unsupported asset type or file extension.";

        if (!saved) return false;
        m_project->RefreshAssetRegistry();
        return true;
    }

    if (command == "create")
    {
        std::string preset;
        std::uint32_t parentId = 0;
        input >> preset >> parentId;
        const std::string presetName = Decode(preset);
        auto entity = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = presetName == "Empty Entity" ? "New Entity" : presetName});
        auto *created = m_scene->AddEntity(std::move(entity), FindEntity(parentId));
        if (!created)
        {
            errorMessage = "Could not create the preset entity.";
            return false;
        }
        if (presetName == "Cube")
        {
            auto *mesh = PlutoGE::scene::AddMeshComponent(
                *created,
                m_engine.GetAssetManager().LoadMeshAsset(std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference)),
                m_engine.GetAssetManager().LoadMaterialAsset(std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference)));
            if (mesh)
            {
                mesh->SetSourceMeshPath(std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference));
                mesh->SetMaterialAssetForMaterialSlot(0, std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference));
            }
        }
        else if (presetName == "Camera")
        {
            created->SetPosition({0.0f, 2.0f, 5.0f});
            PlutoGE::scene::AddComponentByTypeName(*created, "CameraComponent");
        }
        else if (presetName == "Directional Light" || presetName == "Point Light")
        {
            auto *light = static_cast<PlutoGE::scene::LightComponent *>(PlutoGE::scene::AddComponentByTypeName(*created, "LightComponent"));
            light->SetLightType(presetName == "Directional Light" ? PlutoGE::scene::LightType::Directional : PlutoGE::scene::LightType::Point);
            light->SetIntensity(presetName == "Directional Light" ? 4.0f : 8.0f);
            if (presetName == "Point Light") created->SetPosition({0.0f, 2.0f, 0.0f});
        }
        else if (presetName == "Sky")
        {
            PlutoGE::scene::AddComponentByTypeName(*created, "PhysicalSkyComponent");
            PlutoGE::scene::AddComponentByTypeName(*created, "VolumetricCloudComponent");
        }
        else if (presetName == "Ocean") PlutoGE::scene::AddComponentByTypeName(*created, "OceanComponent");
        else if (presetName == "Terrain") PlutoGE::scene::AddComponentByTypeName(*created, "TerrainComponent");
        else if (presetName == "Cloth") PlutoGE::scene::AddComponentByTypeName(*created, "ClothComponent");
        else if (presetName == "Particle System") PlutoGE::scene::AddComponentByTypeName(*created, "ParticleSystemComponent");
        else if (presetName == "IBL Capture") PlutoGE::scene::AddComponentByTypeName(*created, "IblCaptureComponent");
        m_selectedEntityId = created->GetID();
    }
    else if (command == "import_model_assets")
    {
        if (!m_project)
        {
            errorMessage = "Open a project before importing models.";
            return false;
        }
        std::string encodedReference;
        bool importedAny = false;
        while (input >> encodedReference)
        {
            const std::string reference = Decode(encodedReference);
            if (!PlutoGE::ui::ImportModelAssetThroughPipeline(*m_project, reference, &errorMessage))
                return false;
            importedAny = true;
        }
        if (!importedAny)
        {
            errorMessage = "No source models were supplied to the asset pipeline.";
            return false;
        }
        m_project->RefreshAssetRegistry();
    }
    else if (command == "instantiate_asset")
    {
        std::string encodedReference;
        input >> encodedReference;
        const std::string reference = Decode(encodedReference);
        if (!m_project || PlutoGE::assets::Project::GetAssetTypeForReference(reference) != PlutoGE::assets::ProjectAssetType::Model)
        {
            errorMessage = "Only imported project model assets can be placed in the scene.";
            return false;
        }
        const auto sourcePath = m_project->ResolveAssetReference(reference);
        const auto manifestPath = m_project->GetAssetDirectoryPath() / "Imported" / sourcePath.stem() /
                                  (sourcePath.stem().string() + ".plutomodel");
        PlutoGE::assets::ModelAsset modelAsset;
        if (!PlutoGE::assets::LoadModelAsset(manifestPath.string(), modelAsset, &errorMessage))
        {
            if (errorMessage.empty()) errorMessage = "Import the model through the asset pipeline before placing it in the scene.";
            return false;
        }
        const auto meshObject = std::find_if(modelAsset.objects.begin(), modelAsset.objects.end(),
                                             [](const PlutoGE::assets::ModelSubAsset &object)
                                             { return object.type == PlutoGE::assets::ProjectAssetType::Mesh; });
        if (meshObject == modelAsset.objects.end())
        {
            errorMessage = "The imported model has no generated mesh object.";
            return false;
        }
        auto *renderMesh = m_engine.GetAssetManager().LoadMeshAsset(meshObject->reference);
        if (!renderMesh)
        {
            errorMessage = "The generated mesh asset could not be loaded: " + meshObject->reference;
            return false;
        }
        auto entity = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = sourcePath.stem().string()});
        auto *created = m_scene->AddEntity(std::move(entity));
        auto *mesh = PlutoGE::scene::AddMeshComponent(*created, renderMesh, nullptr);
        mesh->SetSourceMeshPath(meshObject->reference);
        mesh->SetModelObjectIdentity(modelAsset.sourceAssetId, meshObject->localId);
        const auto &materialReferences = m_engine.GetAssetManager().GetMeshAssetMaterialReferences(meshObject->reference);
        std::vector<PlutoGE::render::Material *> materials;
        for (const auto &materialReference : materialReferences)
        {
            materials.push_back(m_engine.GetAssetManager().LoadMaterialAsset(materialReference));
        }
        mesh->SetMaterials(materials);
        for (std::size_t index = 0; index < materialReferences.size(); ++index)
            mesh->SetMaterialAssetForMaterialSlot(index, materialReferences[index]);
        const auto animationObject = std::find_if(modelAsset.objects.begin(), modelAsset.objects.end(),
                                                  [](const PlutoGE::assets::ModelSubAsset &object)
                                                  { return object.type == PlutoGE::assets::ProjectAssetType::Animation; });
        if (animationObject != modelAsset.objects.end())
        {
            auto *animation = created->CreateComponent<PlutoGE::scene::AnimationComponent>();
            animation->SetAnimationAssetReference(animationObject->reference);
        }
        m_selectedEntityId = created->GetID();
    }
    else if (command == "delete")
    {
        std::uint32_t id = 0;
        input >> id;
        if (m_scene->DestroyEntity(id) && m_selectedEntityId == id) m_selectedEntityId = 0;
    }
    else if (command == "duplicate")
    {
        std::uint32_t id = 0;
        input >> id;
        auto *source = FindEntity(id);
        auto *duplicate = source ? PlutoGE::scene::Prefab::DuplicateEntity(*m_scene, *source, source->GetParent(), true) : nullptr;
        if (!duplicate)
        {
            errorMessage = "Could not duplicate the selected entity.";
            return false;
        }
        duplicate->SetName(source->GetName() + " Copy");
        m_selectedEntityId = duplicate->GetID();
    }
    else if (command == "copy")
    {
        std::uint32_t id = 0;
        input >> id;
        if (!FindEntity(id)) { errorMessage = "Select an entity to copy."; return false; }
        m_copiedEntityId = id;
        return true;
    }
    else if (command == "paste")
    {
        std::uint32_t parentId = 0;
        input >> parentId;
        auto *source = FindEntity(m_copiedEntityId);
        if (!source) { errorMessage = "Copy an entity before pasting."; return false; }
        auto *duplicate = PlutoGE::scene::Prefab::DuplicateEntity(*m_scene, *source, FindEntity(parentId), true);
        if (!duplicate) { errorMessage = "Could not paste the copied entity."; return false; }
        duplicate->SetName(source->GetName() + " Copy");
        m_selectedEntityId = duplicate->GetID();
    }
    else if (command == "save_prefab")
    {
        std::uint32_t id = 0;
        input >> id;
        auto *entity = FindEntity(id);
        if (!entity || !m_project) { errorMessage = "Open a project and select an entity before saving a prefab."; return false; }
        std::string name = entity->GetName();
        for (auto &character : name) if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_') character = '_';
        if (name.empty()) name = "Prefab";
        const auto path = m_project->GetAssetDirectoryPath() / "Prefabs" / (name + ".plutoprefab");
        std::filesystem::create_directories(path.parent_path());
        if (!PlutoGE::scene::Prefab::SaveFromEntity(*entity, path, &errorMessage)) return false;
        m_project->RefreshAssetRegistry();
        return m_project->Save(&errorMessage);
    }
    else if (command == "skeleton_attachments")
    {
        std::uint32_t id = 0;
        input >> id;
        auto *entity = FindEntity(id);
        auto *mesh = entity ? entity->GetComponent<PlutoGE::scene::MeshComponent>() : nullptr;
        if (!mesh || !mesh->GetMesh() || !mesh->GetMesh()->HasSkeleton())
        {
            errorMessage = "The selected entity has no skeletal mesh.";
            return false;
        }
        mesh->CreateSkeletonAttachmentEntities();
    }
    else if (command == "set_name")
    {
        std::uint32_t id = 0;
        std::string value;
        input >> id >> value;
        if (auto *entity = FindEntity(id)) entity->SetName(Decode(value));
    }
    else if (command == "set_active")
    {
        std::uint32_t id = 0;
        int active = 1;
        input >> id >> active;
        if (auto *entity = FindEntity(id)) entity->SetActive(active != 0);
    }
    else if (command == "set_transform")
    {
        std::uint32_t id = 0;
        glm::vec3 position{}, rotation{}, scale{1.0f};
        input >> id >> position.x >> position.y >> position.z >> rotation.x >> rotation.y >> rotation.z >> scale.x >> scale.y >> scale.z;
        if (auto *entity = FindEntity(id)) { entity->SetPosition(position); entity->SetRotation(rotation); entity->SetScale(scale); }
    }
    else if (command == "reparent")
    {
        std::uint32_t id = 0, parentId = 0;
        input >> id >> parentId;
        if (auto *entity = FindEntity(id)) entity->SetParent(FindEntity(parentId));
    }
    else if (command == "component_enabled")
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0;
        int enabled = 1;
        input >> id >> componentIndex >> enabled;
        if (auto *entity = FindEntity(id)) if (auto *component = ComponentAt(*entity, componentIndex)) component->SetEnabled(enabled != 0);
    }
    else if (command == "set_property")
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0, propertyIndex = 0;
        std::string value;
        input >> id >> componentIndex >> propertyIndex >> value;
        if (auto *entity = FindEntity(id))
        {
            if (auto *component = ComponentAt(*entity, componentIndex))
            {
                auto properties = component->Serialize();
                if (propertyIndex < properties.size()) { properties[propertyIndex].value = Decode(value); component->Deserialize(properties); }
            }
        }
    }
    else if (command == "add_component")
    {
        std::uint32_t id = 0;
        std::string type;
        input >> id >> type;
        if (auto *entity = FindEntity(id)) PlutoGE::scene::AddComponentByTypeName(*entity, Decode(type));
    }
    else if (command == "remove_component")
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0;
        input >> id >> componentIndex;
        if (auto *entity = FindEntity(id)) if (auto *component = ComponentAt(*entity, componentIndex)) entity->RemoveComponent(component);
    }
    else if (command == "component_action")
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0;
        std::string encodedAction;
        int index = -1;
        input >> id >> componentIndex >> encodedAction >> index;
        auto *entity = FindEntity(id);
        auto *component = entity ? ComponentAt(*entity, componentIndex) : nullptr;
        const auto action = Decode(encodedAction);
        if (!component) { errorMessage = "The component no longer exists."; return false; }
        if (auto *navigation = dynamic_cast<PlutoGE::scene::NavigationMeshComponent *>(component))
        {
            if (action == "bake") navigation->Bake();
            else if (action == "clear") navigation->Clear();
            else { errorMessage = "Unknown navigation action."; return false; }
        }
        else if (auto *capture = dynamic_cast<PlutoGE::scene::IblCaptureComponent *>(component))
        {
            if (action == "capture")
            {
                capture->DiscardCaptureResult();
                m_scene->ClearIblCaptureVolumes();
                auto *captureTexture = capture->EnsureCaptureTexture();
                if (!captureTexture)
                {
                    errorMessage = "IBL capture failed: could not create the cubemap.";
                    return false;
                }
                if (!m_engine.GetRenderer().CaptureSceneCubemap(entity->GetWorldPosition(), capture->GetResolution(),
                                                                 capture->GetFarPlane(), captureTexture,
                                                                 m_scene->GetLights(), m_scene.get()))
                {
                    errorMessage = "IBL scene capture failed.";
                    return false;
                }
                if (!capture->StoreCapturePixelsFromTexture())
                {
                    errorMessage = "IBL capture completed, but its pixels could not be stored.";
                    return false;
                }
                capture->ClearDirty();
                m_scene->AddIblCaptureVolume(capture->BuildCaptureVolume());
            }
            else if (action == "mark-dirty") capture->MarkDirty();
            else if (action == "discard") capture->DiscardCaptureResult();
            else { errorMessage = "Unknown IBL capture action."; return false; }
        }
        else if (auto *spline = dynamic_cast<PlutoGE::scene::SplineComponent *>(component))
        {
            if (action == "add-point") spline->AddPoint(spline->GetPoints().empty() ? glm::vec3(0.0f) : spline->GetPoints().back().position + glm::vec3(8.0f, 0.0f, 0.0f));
            else if (action == "remove-point" && index >= 0 && static_cast<std::size_t>(index) < spline->GetPoints().size()) spline->RemovePoint(static_cast<std::size_t>(index));
            else { errorMessage = "Unknown spline action."; return false; }
        }
        else if (auto *ocean = dynamic_cast<PlutoGE::scene::OceanComponent *>(component))
        {
            if (action == "add-area") ocean->AddArea({{-10.0f, -10.0f}, {10.0f, -10.0f}, {10.0f, 10.0f}, {-10.0f, 10.0f}});
            else if (action == "remove-area" && index >= 0 && static_cast<std::size_t>(index) < ocean->GetAreas().size()) ocean->RemoveArea(static_cast<std::size_t>(index));
            else { errorMessage = "Unknown ocean action."; return false; }
        }
        else { errorMessage = "This component does not support that action."; return false; }
    }
    else if (command.rfind("camera_effect_", 0) == 0)
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0;
        input >> id >> componentIndex;
        auto *entity = FindEntity(id);
        auto *camera = entity ? dynamic_cast<PlutoGE::scene::CameraComponent *>(ComponentAt(*entity, componentIndex)) : nullptr;
        if (!camera)
        {
            errorMessage = "The selected component is not a camera.";
            return false;
        }

        if (command == "camera_effect_add")
        {
            std::string type;
            input >> type;
            camera->AddPostProcessEffectByType(Decode(type));
        }
        else if (command == "camera_effect_remove")
        {
            std::size_t index = 0;
            input >> index;
            camera->RemovePostProcessEffect(index);
        }
        else if (command == "camera_effect_move")
        {
            std::size_t from = 0, to = 0;
            input >> from >> to;
            camera->MovePostProcessEffect(from, to);
        }
        else if (command == "camera_effect_enabled")
        {
            std::size_t index = 0;
            int enabled = 1;
            input >> index >> enabled;
            if (auto *effect = camera->GetPostProcessEffect(index)) effect->SetEnabled(enabled != 0);
        }
        else if (command == "camera_effect_parameter")
        {
            std::size_t effectIndex = 0, parameterIndex = 0;
            std::string value;
            input >> effectIndex >> parameterIndex >> value;
            if (auto *effect = camera->GetPostProcessEffect(effectIndex))
            {
                auto parameters = effect->GetParameters();
                if (parameterIndex < parameters.size())
                {
                    parameters[parameterIndex].value = Decode(value);
                    effect->SetParameters(parameters);
                }
            }
        }
        else if (command == "camera_effect_preset")
        {
            std::string reference;
            input >> reference;
            if (!camera->SetPostProcessPresetAssetReference(Decode(reference)))
            {
                errorMessage = "Could not load camera post-process preset.";
                return false;
            }
        }
        else if (command == "camera_effect_save_preset")
        {
            const auto &reference = camera->GetPostProcessPresetAssetReference();
            if (reference.empty())
            {
                errorMessage = "Set a camera post-process preset reference before saving.";
                return false;
            }
            const auto preset = PlutoGE::assets::CapturePostProcessPreset(camera->GetPostProcessEffects());
            if (!m_engine.GetAssetManager().SavePostProcessPresetAsset(reference, preset, &errorMessage)) return false;
        }
        else if (command == "camera_effect_save_preset_as")
        {
            std::string encodedReference;
            input >> encodedReference;
            const std::string reference = Decode(encodedReference);
            if (!m_project || PlutoGE::assets::Project::GetAssetTypeForReference(reference) != PlutoGE::assets::ProjectAssetType::PostProcessPreset)
            {
                errorMessage = "Choose a project .plutopostprocess asset reference.";
                return false;
            }
            const auto path = m_project->ResolveAssetReference(reference);
            if (std::filesystem::exists(path))
            {
                errorMessage = "A post-process preset already exists at that path.";
                return false;
            }
            const auto preset = PlutoGE::assets::CapturePostProcessPreset(camera->GetPostProcessEffects());
            if (!m_engine.GetAssetManager().SavePostProcessPresetAsset(reference, preset, &errorMessage)) return false;
            if (!camera->SetPostProcessPresetAssetReference(reference))
            {
                errorMessage = "The new post-process preset could not be assigned to the camera.";
                return false;
            }
            m_project->RefreshAssetRegistry();
        }
        else
        {
            errorMessage = "Unknown camera post-process command.";
            return false;
        }
    }
    else
    {
        errorMessage = "Unknown editor command: " + command;
        return false;
    }

    CommitEdit(before);
    return true;
}

std::string EditorSession::BuildSnapshotEvent() const
{
    int renderableMeshComponents = 0;
    const int registeredMeshComponents = m_scene ? static_cast<int>(m_scene->GetMeshComponents().size()) : 0;
    if (m_scene)
    {
        for (const auto *mesh : m_scene->GetMeshComponents())
        {
            if (mesh && mesh->IsEnabled() && mesh->IsVisible() && mesh->GetMesh() && mesh->GetMaterial()) ++renderableMeshComponents;
        }
    }
    std::ostringstream output;
    output << "{\"type\":\"editor-state\",\"projectPath\":\"" << JsonEscape(m_projectPath)
           << "\",\"projectName\":\"" << JsonEscape(m_project ? m_project->GetManifest().name : std::string{})
           << "\",\"assetDirectoryPath\":\"" << JsonEscape(m_project ? m_project->GetAssetDirectoryPath().string() : std::string{})
           << "\",\"projectSettings\":{";
    if (m_project)
    {
        const auto &manifest = m_project->GetManifest();
        output << "\"name\":\"" << JsonEscape(manifest.name)
               << "\",\"startupScene\":\"" << JsonEscape(manifest.startupScene)
               << "\",\"scriptAssembly\":\"" << JsonEscape(manifest.scriptAssembly)
               << "\",\"windowTitle\":\"" << JsonEscape(manifest.windowTitle)
               << "\",\"windowWidth\":" << manifest.windowWidth
               << ",\"windowHeight\":" << manifest.windowHeight
               << ",\"vSyncEnabled\":" << (manifest.vSyncEnabled ? "true" : "false");
    }
    else
    {
        output << "\"name\":\"\",\"startupScene\":\"\",\"scriptAssembly\":\"\",\"windowTitle\":\"\","
               << "\"windowWidth\":1280,\"windowHeight\":720,\"vSyncEnabled\":true";
    }
    output << '}'
           << ",\"assets\":[";
    if (m_project)
    {
        const auto &assets = m_project->GetManifest().assetEntries;
        for (std::size_t assetIndex = 0; assetIndex < assets.size(); ++assetIndex)
        {
            if (assetIndex > 0) output << ',';
            output << "{\"reference\":\"" << JsonEscape(assets[assetIndex].reference)
                   << "\",\"size\":" << assets[assetIndex].size
                   << ",\"type\":\"" << PlutoGE::assets::Project::GetAssetTypeName(assets[assetIndex].type) << "\"}";
        }
    }
    output << ']'
           << ",\"scenePath\":\"" << JsonEscape(m_scenePath)
           << "\",\"dirty\":" << (m_dirty ? "true" : "false")
           << ",\"environmentPath\":\"" << JsonEscape(m_scene ? m_scene->GetEnvironmentMapPath() : std::string{})
           << "\",\"environmentIntensity\":" << (m_scene ? m_scene->GetEnvironmentIntensity() : 1.0f)
           << ",\"bakeRunning\":" << (m_bakeTask ? "true" : "false")
           << ",\"bakeStatus\":\"" << JsonEscape(m_bakeStatus) << "\""
           << ",\"scriptClassNames\":[";
    const auto scriptClassNames = m_engine.GetScriptEngine().GetClassNames();
    for (std::size_t classIndex = 0; classIndex < scriptClassNames.size(); ++classIndex)
    {
        if (classIndex > 0) output << ',';
        output << '"' << JsonEscape(scriptClassNames[classIndex]) << '"';
    }
    output << "],\"scriptableObjectClassNames\":[";
    const auto scriptableObjectClassNames = m_engine.GetScriptEngine().GetScriptableObjectClassNames();
    for (std::size_t classIndex = 0; classIndex < scriptableObjectClassNames.size(); ++classIndex)
    {
        if (classIndex > 0) output << ',';
        output << '"' << JsonEscape(scriptableObjectClassNames[classIndex]) << '"';
    }
    output << ']'
           << ",\"postProcessEffectTypes\":[";
    const auto &registeredEffectTypes = PlutoGE::render::GetRegisteredPostProcessEffectTypes();
    for (std::size_t typeIndex = 0; typeIndex < registeredEffectTypes.size(); ++typeIndex)
    {
        if (typeIndex > 0) output << ',';
        output << '"' << JsonEscape(registeredEffectTypes[typeIndex]) << '"';
    }
    output << ']'
           << ",\"running\":" << (m_engine.IsRuntimeRunning() ? "true" : "false")
           << ",\"gizmoOperation\":\"" << (m_gizmoOperation == GizmoOperation::Rotate ? "rotate" : m_gizmoOperation == GizmoOperation::Scale ? "scale" : "translate") << '"'
           << ",\"gizmoSpace\":\"" << (m_gizmoSpace == GizmoSpace::World ? "world" : "local") << '"'
           << ",\"selectedEntityId\":" << m_selectedEntityId
           << ",\"canUndo\":" << (!m_undo.empty() ? "true" : "false")
           << ",\"canRedo\":" << (!m_redo.empty() ? "true" : "false")
           << ",\"editorCamera\":{\"position\":[" << m_editorCamera.position.x << ',' << m_editorCamera.position.y << ',' << m_editorCamera.position.z << ']'
           << ",\"yawDegrees\":" << m_editorCamera.yawDegrees
           << ",\"pitchDegrees\":" << m_editorCamera.pitchDegrees
           << ",\"fovY\":" << m_editorCamera.fovY
           << ",\"nearPlane\":" << m_editorCamera.nearPlane
           << ",\"farPlane\":" << m_editorCamera.farPlane
           << ",\"moveSpeed\":" << m_editorCamera.moveSpeed
           << ",\"speedAdjustment\":" << m_editorCamera.speedAdjustment
           << ",\"gridVisible\":" << (m_editorCamera.gridVisible ? "true" : "false")
           << ",\"postProcessEffectCount\":" << m_editorPostProcessEffects.size()
           << ",\"postProcessPresetReference\":\"" << JsonEscape(m_editorPostProcessPresetReference)
           << "\",\"postProcessEffects\":";
    WritePostProcessEffectsJson(output, m_editorPostProcessEffects);
    output << '}'
           << ",\"viewportStats\":{\"submittedRenderCommands\":" << m_submittedRenderCommands
           << ",\"visibleRenderCommands\":" << m_visibleRenderCommands
           << ",\"registeredMeshComponents\":" << registeredMeshComponents
           << ",\"renderableMeshComponents\":" << renderableMeshComponents << '}'
           << ",\"viewportSettings\":{\"debugView\":" << static_cast<int>(m_engine.GetRenderer().GetPostProcessDebugView())
           << ",\"debugShapes\":" << (m_debugShapes ? "true" : "false")
           << ",\"snapEnabled\":" << (m_snapEnabled ? "true" : "false")
           << ",\"translateSnap\":" << m_translateSnap
           << ",\"rotateSnap\":" << m_rotateSnap
           << ",\"scaleSnap\":" << m_scaleSnap << '}'
           << ",\"entities\":[";
    std::vector<const PlutoGE::scene::Entity *> entities;
    if (m_scene)
    {
        for (const auto *root : m_scene->GetRootEntities()) if (root) CollectEntities(*root, entities);
    }
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        if (index > 0) output << ',';
        WriteEntityJson(output, *entities[index]);
    }
    output << "]}";
    return output.str();
}
