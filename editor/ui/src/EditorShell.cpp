#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ProfilerPanel.h"
#include "PlutoGE/ui/panels/ConsolePanel.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include "PlutoGE/ui/panels/MaterialEditorPanel.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <iostream>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <memory>
#include <array>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

namespace PlutoGE::ui
{
    void EditorShell::RequestIblCapture(scene::IblCaptureComponent *captureComponent)
    {
        auto *owner = captureComponent ? captureComponent->GetOwner() : nullptr;
        if (!owner)
        {
            return;
        }

        const auto entityId = owner->GetID();
        if (std::find(m_pendingIblCaptureEntities.begin(), m_pendingIblCaptureEntities.end(), entityId) == m_pendingIblCaptureEntities.end())
        {
            captureComponent->MarkDirty();
            m_pendingIblCaptureEntities.push_back(entityId);
            m_statusMessage = "IBL capture queued.";
        }
    }

    namespace
    {
        constexpr float kEditorCameraMoveSpeed = 6.0f;
        constexpr float kEditorCameraBoostMultiplier = 2.5f;
        constexpr float kEditorCameraMouseSensitivity = 0.12f;
        constexpr float kEditorCameraPitchLimitDegrees = 89.0f;
        constexpr const char *kSceneFileFilter = "PlutoGE Scene\0*.plutoscene\0All Files\0*.*\0";
        constexpr const char *kProjectFileFilter = "PlutoGE Project\0*.plutoproject\0All Files\0*.*\0";
        constexpr const char *kExecutableFileFilter = "Executable\0*.exe\0All Files\0*.*\0";
        constexpr const char *kDefaultProjectFileName = "UntitledProject.plutoproject";
        constexpr const char *kDefaultProjectSceneRelativePath = "Scenes/Main.plutoscene";
        constexpr std::string_view kDefaultProjectScriptDirectory = "Scripts";
        constexpr std::string_view kDefaultProjectManagedDirectory = "Managed";
        constexpr int kScriptCoreSearchAncestorLimit = 8;

        std::string QuoteShellArgument(const std::filesystem::path &path)
        {
            return '"' + path.string() + '"';
        }

        std::filesystem::path FindCMakeBuildDirectory(const std::filesystem::path &runtimeExecutablePath)
        {
            auto candidate = runtimeExecutablePath.parent_path();
            for (int depth = 0; depth < 8 && !candidate.empty(); ++depth)
            {
                if (std::filesystem::exists(candidate / "CMakeCache.txt"))
                {
                    return candidate;
                }

                const auto parent = candidate.parent_path();
                if (parent.empty() || parent == candidate)
                {
                    break;
                }

                candidate = parent;
            }

            return {};
        }

        std::string DetectCMakeBuildConfig(const std::filesystem::path &runtimeExecutablePath)
        {
            const auto configDirectory = runtimeExecutablePath.parent_path().filename().string();
            if (configDirectory == "Debug" ||
                configDirectory == "Release" ||
                configDirectory == "RelWithDebInfo" ||
                configDirectory == "MinSizeRel")
            {
                return configDirectory;
            }

            return {};
        }

        bool RebuildStandaloneRuntime(const std::filesystem::path &runtimeExecutablePath,
                                      std::string *errorMessage)
        {
            const auto buildDirectory = FindCMakeBuildDirectory(runtimeExecutablePath);
            if (buildDirectory.empty())
            {
                if (errorMessage)
                {
                    *errorMessage = "Could not determine the CMake build directory for PlutoGERuntime.";
                }
                return false;
            }

            std::string command = "cmake --build " + QuoteShellArgument(buildDirectory) + " --target PlutoGERuntime";
            if (const auto config = DetectCMakeBuildConfig(runtimeExecutablePath); !config.empty())
            {
                command += " --config " + config;
            }

            if (std::system(command.c_str()) != 0)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to rebuild PlutoGERuntime before exporting the project.";
                }
                return false;
            }

            return true;
        }

        bool LaunchExecutable(const std::filesystem::path &executablePath,
                              std::string *errorMessage)
        {
#ifdef _WIN32
            const auto operationResult = reinterpret_cast<std::intptr_t>(ShellExecuteA(nullptr,
                                                                                       "open",
                                                                                       executablePath.string().c_str(),
                                                                                       nullptr,
                                                                                       executablePath.parent_path().string().c_str(),
                                                                                       SW_SHOWNORMAL));
            if (operationResult <= 32)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to launch built project executable.";
                }
                return false;
            }

            return true;
#else
            const std::string command = QuoteShellArgument(executablePath);
            if (std::system(command.c_str()) != 0)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to launch built project executable.";
                }
                return false;
            }

            return true;
#endif
        }

        std::filesystem::path GetProcessDirectory()
        {
#ifdef _WIN32
            std::array<char, MAX_PATH> modulePath{};
            const DWORD modulePathLength = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
            if (modulePathLength > 0 && modulePathLength < modulePath.size())
            {
                return std::filesystem::path(modulePath.data()).parent_path().lexically_normal();
            }
#endif

            return std::filesystem::current_path();
        }

        std::string SanitizeIdentifier(std::string_view text)
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
                }
                else if (!identifier.empty() && identifier.back() != '_')
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

        std::filesystem::path FindScriptCoreProjectPath(const std::filesystem::path &searchRoot)
        {
            auto candidateRoot = searchRoot.lexically_normal();
            for (int depth = 0; depth < kScriptCoreSearchAncestorLimit && !candidateRoot.empty(); ++depth)
            {
                const auto candidate = (candidateRoot / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "PlutoGE.ScriptCore.csproj").lexically_normal();
                if (std::filesystem::exists(candidate))
                {
                    return candidate;
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

        std::string MakeRelativeOrAbsoluteGenericPath(const std::filesystem::path &targetPath, const std::filesystem::path &basePath)
        {
            std::error_code errorCode;
            const auto relativePath = std::filesystem::relative(targetPath, basePath, errorCode);
            if (!errorCode && !relativePath.empty())
            {
                return relativePath.generic_string();
            }

            return std::filesystem::absolute(targetPath).lexically_normal().generic_string();
        }

        bool WriteTextFile(const std::filesystem::path &filePath, std::string_view content, std::string *errorMessage)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(filePath.parent_path(), errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to create directory: " + filePath.parent_path().string();
                }
                return false;
            }

            std::ofstream output(filePath, std::ios::out | std::ios::trunc);
            if (!output.is_open())
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to open file for writing: " + filePath.string();
                }
                return false;
            }

            output << content;
            output.close();
            if (!output)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to write file: " + filePath.string();
                }
                return false;
            }

            return true;
        }

        bool IsCameraActiveInScene(scene::Scene *scene, scene::CameraComponent *cameraComponent)
        {
            if (!scene || !cameraComponent)
            {
                return false;
            }

            auto *owner = cameraComponent->GetOwner();
            if (!owner || !cameraComponent->GetCamera() || !owner->IsActive())
            {
                return false;
            }

            return scene->FindEntityByID(owner->GetID()) == owner;
        }

        glm::mat4 GetEditorCameraTransform(const EditorShell::EditorViewportCamera &camera)
        {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), camera.position);
            transform = glm::rotate(transform, glm::radians(camera.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::rotate(transform, glm::radians(camera.pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            return transform;
        }

        glm::vec3 GetTransformForward(const glm::mat4 &transform)
        {
            return glm::normalize(-glm::vec3(transform[2]));
        }

        glm::vec3 GetTransformRight(const glm::mat4 &transform)
        {
            return glm::normalize(glm::vec3(transform[0]));
        }

        assets::ProjectEditorCameraSettings BuildProjectEditorCameraSettings(const EditorShell::EditorViewportCamera &camera)
        {
            return assets::ProjectEditorCameraSettings{
                .positionX = camera.position.x,
                .positionY = camera.position.y,
                .positionZ = camera.position.z,
                .yawDegrees = camera.yawDegrees,
                .pitchDegrees = camera.pitchDegrees,
                .fovY = camera.camera.GetFOV(),
                .nearPlane = camera.camera.GetNearPlane(),
                .farPlane = camera.camera.GetFarPlane(),
            };
        }

        std::vector<assets::ProjectPostProcessEffect> BuildProjectEditorPostProcessEffects(const EditorShell::EditorViewportCamera &camera)
        {
            std::vector<assets::ProjectPostProcessEffect> effects;
            effects.reserve(camera.GetPostProcessEffects().size());

            for (const auto &effect : camera.GetPostProcessEffects())
            {
                if (!effect)
                {
                    continue;
                }

                assets::ProjectPostProcessEffect serializedEffect;
                serializedEffect.typeName = effect->GetTypeName();
                serializedEffect.enabled = effect->IsEnabled();

                const auto parameters = effect->GetParameters();
                serializedEffect.parameters.reserve(parameters.size());
                for (const auto &parameter : parameters)
                {
                    serializedEffect.parameters.push_back(assets::ProjectPostProcessParameter{
                        .name = parameter.name,
                        .type = static_cast<int>(parameter.type),
                        .value = parameter.value,
                    });
                }

                effects.push_back(std::move(serializedEffect));
            }

            return effects;
        }

        void ApplyProjectEditorCameraSettings(const assets::ProjectEditorCameraSettings &settings,
                                              EditorShell::EditorViewportCamera &camera)
        {
            camera.position = glm::vec3(settings.positionX, settings.positionY, settings.positionZ);
            camera.yawDegrees = settings.yawDegrees;
            camera.pitchDegrees = settings.pitchDegrees;
            camera.camera.SetFOV(settings.fovY);
            camera.camera.SetNearPlane(settings.nearPlane);
            camera.camera.SetFarPlane(settings.farPlane);
        }

        void ApplyProjectEditorPostProcessEffects(const std::vector<assets::ProjectPostProcessEffect> &serializedEffects,
                                                  EditorShell::EditorViewportCamera &camera)
        {
            if (serializedEffects.empty())
            {
                return;
            }

            camera.postProcessEffects.clear();
            for (const auto &serializedEffect : serializedEffects)
            {
                if (!camera.AddPostProcessEffectByType(serializedEffect.typeName))
                {
                    continue;
                }

                auto *effect = camera.GetPostProcessEffect(camera.GetPostProcessEffects().size() - 1);
                if (!effect)
                {
                    continue;
                }

                effect->SetEnabled(serializedEffect.enabled);
                auto parameters = effect->GetParameters();
                for (const auto &serializedParameter : serializedEffect.parameters)
                {
                    auto parameterIt = std::find_if(parameters.begin(), parameters.end(),
                                                    [&serializedParameter](const render::PostProcessParameter &parameter)
                                                    {
                                                        return parameter.name == serializedParameter.name;
                                                    });
                    if (parameterIt == parameters.end())
                    {
                        continue;
                    }

                    parameterIt->type = static_cast<render::PostProcessParameterType>(serializedParameter.type);
                    parameterIt->value = serializedParameter.value;
                }

                effect->SetParameters(parameters);
            }
        }

        void SetCursorCapture(GLFWwindow *windowHandle, bool captured)
        {
            if (!windowHandle)
            {
                return;
            }

            glfwSetInputMode(windowHandle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }

        void UpdateEditorCamera(EditorShell::EditorViewportCamera &camera,
                                GLFWwindow *windowHandle,
                                bool canActivate,
                                float deltaTime,
                                bool &isLookActive,
                                double &lastCursorX,
                                double &lastCursorY)
        {
            if (!windowHandle)
            {
                if (isLookActive)
                {
                    SetCursorCapture(windowHandle, false);
                    isLookActive = false;
                }
                return;
            }

            const bool isRightMouseDown = glfwGetMouseButton(windowHandle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            if (!isRightMouseDown)
            {
                if (isLookActive)
                {
                    SetCursorCapture(windowHandle, false);
                    isLookActive = false;
                }
                return;
            }

            if (!isLookActive)
            {
                if (!canActivate)
                {
                    return;
                }

                isLookActive = true;
                SetCursorCapture(windowHandle, true);
                glfwGetCursorPos(windowHandle, &lastCursorX, &lastCursorY);
            }

            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(windowHandle, &cursorX, &cursorY);

            const float deltaX = static_cast<float>(cursorX - lastCursorX);
            const float deltaY = static_cast<float>(cursorY - lastCursorY);
            lastCursorX = cursorX;
            lastCursorY = cursorY;

            camera.yawDegrees -= deltaX * kEditorCameraMouseSensitivity;
            camera.pitchDegrees = glm::clamp(camera.pitchDegrees - deltaY * kEditorCameraMouseSensitivity,
                                             -kEditorCameraPitchLimitDegrees,
                                             kEditorCameraPitchLimitDegrees);

            const glm::mat4 transform = GetEditorCameraTransform(camera);

            glm::vec3 movement(0.0f);
            const glm::vec3 forward = GetTransformForward(transform);
            const glm::vec3 right = GetTransformRight(transform);
            static constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);

            if (glfwGetKey(windowHandle, GLFW_KEY_W) == GLFW_PRESS)
            {
                movement += forward;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_S) == GLFW_PRESS)
            {
                movement -= forward;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_D) == GLFW_PRESS)
            {
                movement += right;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_A) == GLFW_PRESS)
            {
                movement -= right;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_E) == GLFW_PRESS)
            {
                movement += kWorldUp;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_Q) == GLFW_PRESS)
            {
                movement -= kWorldUp;
            }

            if (glm::dot(movement, movement) <= 0.0f)
            {
                return;
            }

            float moveSpeed = kEditorCameraMoveSpeed;
            if (glfwGetKey(windowHandle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            {
                moveSpeed *= kEditorCameraBoostMultiplier;
            }

            camera.position += glm::normalize(movement) * moveSpeed * deltaTime;
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

        scene::CameraComponent *FindFirstSceneCamera(scene::Scene *scene)
        {
            if (!scene)
            {
                return nullptr;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            for (auto *entity : entities)
            {
                if (!entity || !entity->IsActive())
                {
                    continue;
                }

                if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>())
                {
                    if (cameraComponent->GetCamera())
                    {
                        return cameraComponent;
                    }
                }
            }

            return nullptr;
        }

        std::unique_ptr<scene::Scene> CreateEmptyScene()
        {
            return std::make_unique<scene::Scene>();
        }

#ifdef _WIN32
        std::string ShowOpenFileDialog(const char *filter)
        {
            OPENFILENAMEA openFileName{};
            char fileName[MAX_PATH] = "";
            openFileName.lStructSize = sizeof(openFileName);
            openFileName.hwndOwner = nullptr;
            openFileName.lpstrFilter = filter;
            openFileName.lpstrFile = fileName;
            openFileName.nMaxFile = MAX_PATH;
            openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileNameA(&openFileName))
            {
                return {};
            }

            return std::filesystem::path(fileName).lexically_normal().string();
        }

        std::string ShowSaveFileDialog(const char *filter, const std::string &initialPath, const char *defaultExtension)
        {
            OPENFILENAMEA openFileName{};
            char fileName[MAX_PATH] = "";
            if (!initialPath.empty())
            {
                strncpy_s(fileName, sizeof(fileName), initialPath.c_str(), _TRUNCATE);
            }
            openFileName.lStructSize = sizeof(openFileName);
            openFileName.hwndOwner = nullptr;
            openFileName.lpstrFilter = filter;
            openFileName.lpstrFile = fileName;
            openFileName.nMaxFile = MAX_PATH;
            openFileName.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            openFileName.lpstrDefExt = defaultExtension;
            if (!GetSaveFileNameA(&openFileName))
            {
                return {};
            }

            return std::filesystem::path(fileName).lexically_normal().string();
        }
#else
        std::string ShowOpenFileDialog(const char *)
        {
            return {};
        }

        std::string ShowSaveFileDialog(const char *, const std::string &, const char *)
        {
            return {};
        }
#endif
    }

    EditorShell::~EditorShell() = default;

    void EditorShell::InitializeEditorCamera()
    {
        m_editorCamera = EditorViewportCamera{};
        m_editorCamera.AddPostProcessEffectByType("RSM");
        m_editorCamera.AddPostProcessEffectByType("VolumetricFog");
        m_editorCamera.AddPostProcessEffectByType("LSAO");
        m_editorCamera.AddPostProcessEffectByType("TAA");
        if (m_editorCamera.AddPostProcessEffectByType("MotionBlur"))
        {
            m_editorCamera.GetPostProcessEffect(m_editorCamera.GetPostProcessEffects().size() - 1)->SetEnabled(false);
        }
        if (m_editorCamera.AddPostProcessEffectByType("DepthOfField"))
        {
            m_editorCamera.GetPostProcessEffect(m_editorCamera.GetPostProcessEffects().size() - 1)->SetEnabled(false);
        }
        m_editorCamera.AddPostProcessEffectByType("ToneMapping");
        m_editorCamera.AddPostProcessEffectByType("ColorGrading");
        m_editorCamera.AddPostProcessEffectByType("SceneComposite");
        // m_editorCamera.AddPostProcessEffectByType("GammaCorrection");
    }

    void EditorShell::ApplyProjectContext()
    {
        auto &assetManager = m_engine.GetAssetManager();
        if (m_project)
        {
            assetManager.SetProjectContext(m_project->GetRootDirectory().string(), m_project->GetManifest().assetDirectory);
            return;
        }

        assetManager.ClearProjectContext();
    }

    std::filesystem::path EditorShell::ResolveProjectScriptAssemblyPath() const
    {
        if (!m_project || m_project->GetManifest().scriptAssembly.empty())
        {
            return {};
        }

        return std::filesystem::path(m_engine.GetAssetManager().ResolveAssetPath(m_project->GetManifest().scriptAssembly)).lexically_normal();
    }

    std::filesystem::path EditorShell::GetProjectScriptSourceDirectory() const
    {
        if (!m_project)
        {
            return {};
        }

        return (m_project->GetAssetDirectoryPath() / std::filesystem::path(kDefaultProjectScriptDirectory)).lexically_normal();
    }

    std::filesystem::path EditorShell::GetProjectScriptProjectPath() const
    {
        if (!m_project)
        {
            return {};
        }

        std::string baseName = SanitizeIdentifier(m_project->GetManifest().name);
        if (baseName.empty())
        {
            baseName = SanitizeIdentifier(m_project->GetManifestPath().stem().string());
        }
        if (baseName.empty())
        {
            baseName = "PlutoGEProject";
        }

        return (m_project->GetRootDirectory() / (baseName + ".Scripts.csproj")).lexically_normal();
    }

    std::filesystem::path EditorShell::GetProjectScriptAssemblyOutputPath() const
    {
        if (!m_project)
        {
            return {};
        }

        const auto &manifest = m_project->GetManifest();
        if (!manifest.scriptAssembly.empty() && !assets::Project::IsEngineAssetReference(manifest.scriptAssembly))
        {
            return m_project->ResolveAssetReference(manifest.scriptAssembly).lexically_normal();
        }

        std::string baseName = SanitizeIdentifier(manifest.name);
        if (baseName.empty())
        {
            baseName = "PlutoGEProject";
        }

        return (m_project->GetAssetDirectoryPath() / std::filesystem::path(kDefaultProjectManagedDirectory) / (baseName + ".Scripts.dll")).lexically_normal();
    }

    bool EditorShell::IsRuntimeExportProject() const
    {
        if (!m_project)
        {
            return false;
        }

        auto runtimeExecutablePath = m_project->GetManifestPath();
        runtimeExecutablePath.replace_extension(".exe");
        return std::filesystem::exists(runtimeExecutablePath);
    }

    bool EditorShell::EnsureProjectScriptBuildScaffold(std::string *errorMessage)
    {
        if (!m_project)
        {
            if (errorMessage)
            {
                *errorMessage = "No project loaded.";
            }
            return false;
        }

        if (IsRuntimeExportProject())
        {
            if (errorMessage)
            {
                *errorMessage = "Script authoring is disabled for exported runtime bundles. Open the source project to edit or build scripts.";
            }
            return false;
        }

        const auto scriptCoreProjectPath = FindScriptCoreProjectPath(GetProcessDirectory());
        if (scriptCoreProjectPath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Could not locate PlutoGE.ScriptCore.csproj for script compilation.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(GetProjectScriptSourceDirectory(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create project script source directory.";
            }
            return false;
        }

        std::filesystem::create_directories(GetProjectScriptAssemblyOutputPath().parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create project script output directory.";
            }
            return false;
        }

        const auto scriptProjectPath = GetProjectScriptProjectPath();
        const auto sourcePattern = std::filesystem::relative(GetProjectScriptSourceDirectory(), scriptProjectPath.parent_path(), errorCode).generic_string() + "/**/*.cs";
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create script project source path.";
            }
            return false;
        }

        const auto scriptCoreReference = MakeRelativeOrAbsoluteGenericPath(scriptCoreProjectPath,
                                                                           scriptProjectPath.parent_path());
        if (scriptCoreReference.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create script project reference path.";
            }
            return false;
        }

        const auto scriptCoreDirectory = MakeRelativeOrAbsoluteGenericPath(scriptCoreProjectPath.parent_path(),
                                                                           scriptProjectPath.parent_path());
        if (scriptCoreDirectory.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create script runtime copy path.";
            }
            return false;
        }

        const auto managedOutputPath = std::filesystem::relative(GetProjectScriptAssemblyOutputPath().parent_path(), scriptProjectPath.parent_path(), errorCode).generic_string();
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create script project output path.";
            }
            return false;
        }

        std::string rootNamespace = SanitizeIdentifier(m_project->GetManifest().name);
        if (rootNamespace.empty())
        {
            rootNamespace = "PlutoGEProject";
        }

        std::string scriptProjectContent;
        scriptProjectContent += "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
        scriptProjectContent += "  <PropertyGroup>\n";
        scriptProjectContent += "    <TargetFramework>net8.0</TargetFramework>\n";
        scriptProjectContent += "    <ImplicitUsings>enable</ImplicitUsings>\n";
        scriptProjectContent += "    <Nullable>enable</Nullable>\n";
        scriptProjectContent += "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
        scriptProjectContent += "    <CopyLocalLockFileAssemblies>true</CopyLocalLockFileAssemblies>\n";
        scriptProjectContent += "    <AssemblyName>" + rootNamespace + ".Scripts</AssemblyName>\n";
        scriptProjectContent += "    <RootNamespace>" + rootNamespace + ".Scripts</RootNamespace>\n";
        scriptProjectContent += "    <OutputPath>" + managedOutputPath + "/</OutputPath>\n";
        scriptProjectContent += "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n";
        scriptProjectContent += "  </PropertyGroup>\n";
        scriptProjectContent += "  <ItemGroup>\n";
        scriptProjectContent += "    <Compile Include=\"" + sourcePattern + "\" />\n";
        scriptProjectContent += "  </ItemGroup>\n";
        scriptProjectContent += "  <ItemGroup>\n";
        scriptProjectContent += "    <ProjectReference Include=\"" + scriptCoreReference + "\" />\n";
        scriptProjectContent += "  </ItemGroup>\n";
        scriptProjectContent += "  <Target Name=\"CopyScriptCoreRuntimeFiles\" AfterTargets=\"Build\">\n";
        scriptProjectContent += "    <ItemGroup>\n";
        scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"" + scriptCoreDirectory + "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.runtimeconfig.json\" />\n";
        scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"" + scriptCoreDirectory + "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.deps.json\" Condition=\"Exists('" + scriptCoreDirectory + "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.deps.json')\" />\n";
        scriptProjectContent += "    </ItemGroup>\n";
        scriptProjectContent += "    <Copy SourceFiles=\"@(ScriptCoreRuntimeFiles)\" DestinationFolder=\"$(OutputPath)\" SkipUnchangedFiles=\"true\" Condition=\"@(ScriptCoreRuntimeFiles) != ''\" />\n";
        scriptProjectContent += "  </Target>\n";
        scriptProjectContent += "</Project>\n";

        if (!WriteTextFile(scriptProjectPath, scriptProjectContent, errorMessage))
        {
            return false;
        }

        auto &manifest = m_project->GetManifest();
        if (manifest.scriptAssembly.empty() || assets::Project::IsEngineAssetReference(manifest.scriptAssembly))
        {
            manifest.scriptAssembly = m_project->MakeAssetReference(GetProjectScriptAssemblyOutputPath());
        }

        return true;
    }

    bool EditorShell::SaveProjectManifest(std::string *errorMessage)
    {
        if (!m_project)
        {
            if (errorMessage)
            {
                *errorMessage = "No project loaded.";
            }
            return false;
        }

        m_project->RefreshAssetRegistry();
        return m_project->Save(errorMessage);
    }

    bool EditorShell::CreateScriptAsset(std::string_view requestedName,
                                        std::string *createdClassName,
                                        std::string *errorMessage)
    {
        if (!EnsureProjectScriptBuildScaffold(errorMessage))
        {
            return false;
        }

        const std::string className = SanitizeIdentifier(requestedName);
        if (className.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Enter a valid script name.";
            }
            return false;
        }

        const auto scriptPath = GetProjectScriptSourceDirectory() / (className + ".cs");
        if (std::filesystem::exists(scriptPath))
        {
            if (errorMessage)
            {
                *errorMessage = "A script with that name already exists.";
            }
            return false;
        }

        std::string scriptSource;
        scriptSource += "using PlutoGE.ScriptCore;\n\n";
        scriptSource += "public sealed class " + className + " : ScriptBehaviour\n";
        scriptSource += "{\n";
        scriptSource += "    public override void OnCreate()\n";
        scriptSource += "    {\n";
        scriptSource += "    }\n\n";
        scriptSource += "    public override void OnUpdate(float deltaTime)\n";
        scriptSource += "    {\n";
        scriptSource += "    }\n";
        scriptSource += "}\n";

        if (!WriteTextFile(scriptPath, scriptSource, errorMessage))
        {
            return false;
        }

        if (!SaveProjectManifest(errorMessage))
        {
            return false;
        }

        if (createdClassName)
        {
            *createdClassName = className;
        }

        m_statusMessage = "Created script: " + scriptPath.filename().string();
        return true;
    }

    bool EditorShell::BuildProjectScripts()
    {
        std::string errorMessage;
        if (!EnsureProjectScriptBuildScaffold(&errorMessage))
        {
            m_statusMessage = errorMessage;
            return false;
        }

        if (!SaveProjectManifest(&errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Failed to save project manifest before building scripts." : errorMessage;
            return false;
        }

        auto &scriptEngine = m_engine.GetScriptEngine();
        const bool wasRuntimeRunning = m_engine.IsRuntimeRunning();
        if (wasRuntimeRunning)
        {
            m_engine.StopRuntime();
        }

        // Release the currently loaded assembly before invoking MSBuild so the
        // output DLL in Assets/Managed is not locked by the editor process.
        scriptEngine.Shutdown();

        auto buildConfig = scripting::ScriptBuildConfig{};
        buildConfig.projectPath = GetProjectScriptProjectPath();
        buildConfig.configuration = "Debug";
        buildConfig.framework = "net8.0";

        const auto buildResult = scriptEngine.BuildProject(buildConfig);
        if (!buildResult.succeeded)
        {
            m_statusMessage = "Failed to build scripts (exit code " + std::to_string(buildResult.exitCode) + ").";

            std::string restoreErrorMessage;
            if (ReloadProjectScriptAssembly(&restoreErrorMessage) && wasRuntimeRunning)
            {
                m_engine.StartRuntime();
            }

            return false;
        }

        if (!SaveProjectManifest(&errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Built scripts but failed to refresh project assets." : errorMessage;
            return false;
        }

        std::string reloadErrorMessage;
        if (!ReloadProjectScriptAssembly(&reloadErrorMessage))
        {
            m_statusMessage = reloadErrorMessage.empty() ? "Built scripts but failed to reload the script assembly." : reloadErrorMessage;
            return false;
        }

        if (wasRuntimeRunning)
        {
            m_engine.StartRuntime();
        }

        m_statusMessage = "Built and reloaded scripts: " + GetProjectScriptAssemblyOutputPath().filename().string();
        return true;
    }

    bool EditorShell::ReloadProjectScriptAssembly(std::string *errorMessage)
    {
        auto &scriptEngine = m_engine.GetScriptEngine();
        const bool wasRuntimeRunning = m_engine.IsRuntimeRunning();
        if (wasRuntimeRunning)
        {
            m_engine.StopRuntime();
        }

        scriptEngine.Shutdown();
        scriptEngine.Initialize();

        const auto assemblyPath = ResolveProjectScriptAssemblyPath();
        if (!m_project || assemblyPath.empty())
        {
            if (wasRuntimeRunning)
            {
                m_engine.StartRuntime();
            }

            return true;
        }

        if (!std::filesystem::exists(assemblyPath))
        {
            if (errorMessage)
            {
                *errorMessage = "Project script assembly was not found: " + assemblyPath.string();
            }
            return false;
        }

        if (!scriptEngine.LoadAssembly(assemblyPath))
        {
            if (errorMessage)
            {
                const auto &runtimeError = scriptEngine.GetLastError();
                *errorMessage = runtimeError.empty()
                                    ? ("Failed to load project script assembly: " + assemblyPath.string())
                                    : ("Failed to load project script assembly: " + assemblyPath.string() + " (" + runtimeError + ")");
            }
            return false;
        }

        if (wasRuntimeRunning)
        {
            m_engine.StartRuntime();
        }

        return true;
    }

    void EditorShell::Log(ConsoleSeverity severity, std::string message)
    {
        if (message.empty())
        {
            return;
        }

        m_consoleMessages.push_back(ConsoleMessage{.severity = severity, .text = std::move(message)});
        constexpr std::size_t kMaxConsoleMessages = 1000;
        if (m_consoleMessages.size() > kMaxConsoleMessages)
        {
            m_consoleMessages.erase(m_consoleMessages.begin(), m_consoleMessages.begin() + static_cast<std::ptrdiff_t>(m_consoleMessages.size() - kMaxConsoleMessages));
        }
    }

    namespace
    {
        render::RenderBackend ToRenderBackend(assets::ProjectGraphicsApi graphicsApi)
        {
            switch (graphicsApi)
            {
            case assets::ProjectGraphicsApi::D3D12:
                return render::RenderBackend::NvrhiD3D12;
            case assets::ProjectGraphicsApi::Vulkan:
                return render::RenderBackend::NvrhiVulkan;
            case assets::ProjectGraphicsApi::OpenGL:
            default:
                return render::RenderBackend::OpenGL;
            }
        }
    }

    void EditorShell::MarkSceneDirty()
    {
        if (!m_sceneDirty)
        {
            m_sceneDirty = true;
            UpdateWindowTitle();
        }
    }

    void EditorShell::MarkProjectDirty()
    {
        if (!m_projectDirty)
        {
            m_projectDirty = true;
            UpdateWindowTitle();
        }
    }

    void EditorShell::MarkSceneClean()
    {
        if (m_sceneDirty)
        {
            m_sceneDirty = false;
            UpdateWindowTitle();
        }
    }

    void EditorShell::MarkProjectClean()
    {
        if (m_projectDirty)
        {
            m_projectDirty = false;
            UpdateWindowTitle();
        }
    }

    bool EditorShell::CaptureSceneState(std::string &state, std::string *errorMessage) const
    {
        if (!m_scene)
        {
            if (errorMessage)
            {
                *errorMessage = "No scene is loaded.";
            }
            return false;
        }

        return scene::SceneSerializer::SaveToString(*m_scene, state, errorMessage);
    }

    bool EditorShell::RestoreSceneState(const std::string &state, std::string *errorMessage)
    {
        auto restoredScene = scene::SceneSerializer::LoadFromString(state, errorMessage);
        if (!restoredScene)
        {
            return false;
        }

        const std::string previousPath = m_scene ? m_scene->GetFilePath() : std::string{};
        restoredScene->SetFilePath(previousPath);
        SetScene(std::move(restoredScene));
        MarkSceneDirty();
        return true;
    }

    void EditorShell::ExecuteSceneEdit(std::string label, const std::function<void()> &edit)
    {
        if (!edit)
        {
            return;
        }

        std::string beforeState;
        std::string errorMessage;
        const bool capturedBefore = CaptureSceneState(beforeState, &errorMessage);
        edit();

        if (!capturedBefore)
        {
            MarkSceneDirty();
            Log(ConsoleSeverity::Warning, errorMessage.empty() ? "Edited scene without undo snapshot." : errorMessage);
            return;
        }

        std::string afterState;
        if (!CaptureSceneState(afterState, &errorMessage) || beforeState == afterState)
        {
            return;
        }

        m_undoStack.push_back(SceneHistoryEntry{.label = std::move(label), .beforeState = std::move(beforeState), .afterState = std::move(afterState)});
        constexpr std::size_t kMaxUndoEntries = 80;
        if (m_undoStack.size() > kMaxUndoEntries)
        {
            m_undoStack.erase(m_undoStack.begin());
        }
        m_redoStack.clear();
        MarkSceneDirty();
    }

    bool EditorShell::Undo()
    {
        if (m_undoStack.empty())
        {
            return false;
        }

        auto entry = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        std::string errorMessage;
        if (!RestoreSceneState(entry.beforeState, &errorMessage))
        {
            Log(ConsoleSeverity::Error, errorMessage.empty() ? "Undo failed." : errorMessage);
            return false;
        }

        Log(ConsoleSeverity::Info, "Undo: " + entry.label);
        m_redoStack.push_back(std::move(entry));
        return true;
    }

    bool EditorShell::Redo()
    {
        if (m_redoStack.empty())
        {
            return false;
        }

        auto entry = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        std::string errorMessage;
        if (!RestoreSceneState(entry.afterState, &errorMessage))
        {
            Log(ConsoleSeverity::Error, errorMessage.empty() ? "Redo failed." : errorMessage);
            return false;
        }

        Log(ConsoleSeverity::Info, "Redo: " + entry.label);
        m_undoStack.push_back(std::move(entry));
        return true;
    }

    bool EditorShell::ConfirmContinueWithUnsavedChanges()
    {
        if (!m_sceneDirty && !m_projectDirty)
        {
            return true;
        }

#ifdef _WIN32
        const int result = MessageBoxA(nullptr,
                                       "There are unsaved editor changes. Continue and discard them?",
                                       "Unsaved Changes",
                                       MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        return result == IDYES;
#else
        return true;
#endif
    }

    void EditorShell::UpdateWindowTitle()
    {
        std::string windowTitle = "PlutoGE Editor";
        if (m_project && !m_project->GetManifest().name.empty())
        {
            windowTitle += " - ";
            windowTitle += m_project->GetManifest().name;
        }
        if (m_sceneDirty || m_projectDirty)
        {
            windowTitle += " *";
        }

        m_engine.GetWindow().SetTitle(windowTitle);
    }

    void EditorShell::ResetSelection()
    {
        m_selectedEntity = nullptr;
        m_isEditorCameraSelected = false;
    }

    void EditorShell::SetScene(std::unique_ptr<scene::Scene> scene)
    {
        if (m_activeBakeTask)
        {
            m_activeBakeTask->Cancel();
            m_activeBakeTask.reset();
        }

        if (!scene)
        {
            scene = CreateEmptyScene();
        }

        m_scene = std::move(scene);
        m_engine.SetScene(m_scene.get());
        ResetSelection();
    }

    std::filesystem::path EditorShell::GetDefaultProjectScenePath() const
    {
        if (!m_project)
        {
            return {};
        }

        if (m_scene && !m_scene->GetFilePath().empty())
        {
            const auto currentScenePath = std::filesystem::path(m_scene->GetFilePath()).lexically_normal();
            if (m_project->IsInAssetDirectory(currentScenePath))
            {
                return currentScenePath;
            }
        }

        return (m_project->GetAssetDirectoryPath() / std::filesystem::path(kDefaultProjectSceneRelativePath)).lexically_normal();
    }

    std::filesystem::path EditorShell::GetDefaultExportExecutablePath() const
    {
        if (!m_project)
        {
            return {};
        }

        std::string projectName = m_project->GetManifest().name.empty() ? "PlutoGEProject" : m_project->GetManifest().name;
        std::filesystem::path executablePath = m_project->GetRootDirectory() / "Build" / projectName;
#ifdef _WIN32
        executablePath.replace_extension(".exe");
#endif
        return executablePath.lexically_normal();
    }

    bool EditorShell::SaveSceneToPath(const std::filesystem::path &scenePath)
    {
        if (!m_scene)
        {
            m_statusMessage = "No scene to save.";
            return false;
        }

        const auto normalizedScenePath = std::filesystem::absolute(scenePath).lexically_normal();
        std::error_code errorCode;
        std::filesystem::create_directories(normalizedScenePath.parent_path(), errorCode);
        if (errorCode)
        {
            m_statusMessage = "Failed to create scene directory: " + normalizedScenePath.parent_path().string();
            return false;
        }

        std::string errorMessage;
        if (!scene::SceneSerializer::Save(*m_scene, normalizedScenePath.string(), &errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Failed to save scene" : errorMessage;
            return false;
        }

        m_scene->SetFilePath(normalizedScenePath.string());
        m_statusMessage = "Saved scene: " + normalizedScenePath.filename().string();
        MarkSceneClean();
        Log(ConsoleSeverity::Info, m_statusMessage);
        return true;
    }

    bool EditorShell::SaveActiveSceneIntoProject()
    {
        if (!m_project)
        {
            return true;
        }

        if (!m_scene)
        {
            SetScene(CreateEmptyScene());
        }

        const auto projectScenePath = GetDefaultProjectScenePath();
        if (projectScenePath.empty())
        {
            m_statusMessage = "Project scene path could not be determined.";
            return false;
        }

        if (!SaveSceneToPath(projectScenePath))
        {
            return false;
        }

        m_project->GetManifest().startupScene = m_project->MakeAssetReference(projectScenePath);
        return true;
    }

    bool EditorShell::OpenSceneFromPath(const std::filesystem::path &scenePath)
    {
        if (!ConfirmContinueWithUnsavedChanges())
        {
            return false;
        }

        std::string errorMessage;
        auto loadedScene = scene::SceneSerializer::Load(scenePath.string(), &errorMessage);
        if (!loadedScene)
        {
            m_statusMessage = errorMessage.empty() ? "Failed to open scene." : errorMessage;
            Log(ConsoleSeverity::Error, m_statusMessage);
            return false;
        }

        SetScene(std::move(loadedScene));
        m_undoStack.clear();
        m_redoStack.clear();
        MarkSceneClean();
        m_statusMessage = "Opened scene: " + scenePath.filename().string();
        Log(ConsoleSeverity::Info, m_statusMessage);
        return true;
    }

    void EditorShell::OpenMaterialAsset(std::string materialAssetReference)
    {
        if (materialAssetReference.empty())
        {
            return;
        }

        m_activeMaterialAssetReference = std::move(materialAssetReference);
        m_openMaterialEditorRequested = true;
        Log(ConsoleSeverity::Info, "Opened material: " + m_activeMaterialAssetReference);
    }

    bool EditorShell::LoadProjectFromPath(const std::filesystem::path &manifestPath)
    {
        constexpr std::string_view kMissingScriptAssemblyPrefix = "Project script assembly was not found: ";

        std::string errorMessage;
        auto loadedProject = assets::Project::Load(manifestPath, &errorMessage);
        if (!loadedProject)
        {
            m_statusMessage = errorMessage.empty() ? "Failed to open project." : errorMessage;
            return false;
        }

        m_project = std::move(loadedProject);
        ApplyProjectContext();
        m_project->RefreshAssetRegistry();

        const auto &manifest = m_project->GetManifest();
        ApplyProjectEditorCameraSettings(manifest.editorCamera, m_editorCamera);
        ApplyProjectEditorPostProcessEffects(manifest.editorCameraPostProcessEffects, m_editorCamera);
        m_engine.GetRenderer().SetVSyncEnabled(manifest.vSyncEnabled);

        std::string scriptErrorMessage;
        bool scriptScaffoldReady = true;
        if (!IsRuntimeExportProject())
        {
            scriptScaffoldReady = EnsureProjectScriptBuildScaffold(&scriptErrorMessage);
        }

        if (scriptScaffoldReady)
        {
            ReloadProjectScriptAssembly(&scriptErrorMessage);
        }

        std::unique_ptr<scene::Scene> loadedScene;
        if (!manifest.startupScene.empty())
        {
            const std::string startupScenePath = m_engine.GetAssetManager().ResolveAssetPath(manifest.startupScene);
            if (!startupScenePath.empty() && std::filesystem::exists(startupScenePath))
            {
                loadedScene = scene::SceneSerializer::Load(startupScenePath, &errorMessage);
            }
        }

        if (loadedScene)
        {
            SetScene(std::move(loadedScene));
            m_statusMessage = "Opened project: " + manifest.name;
        }
        else
        {
            SetScene(CreateEmptyScene());
            if (!errorMessage.empty())
            {
                m_statusMessage = errorMessage;
            }
            else
            {
                m_statusMessage = "Opened project without a startup scene.";
            }
        }

        if (!scriptErrorMessage.empty())
        {
            const bool isMissingScriptAssembly = scriptErrorMessage.rfind(kMissingScriptAssemblyPrefix.data(), 0) == 0;
            if (!isMissingScriptAssembly && !m_statusMessage.empty())
            {
                m_statusMessage += " ";
            }
            if (!isMissingScriptAssembly)
            {
                m_statusMessage += scriptErrorMessage;
            }
        }

        UpdateWindowTitle();
        return true;
    }

    bool EditorShell::CreateProjectAtPath(const std::filesystem::path &manifestPath)
    {
        std::string errorMessage;
        std::string projectName = manifestPath.stem().string();
        if (projectName.empty())
        {
            projectName = "UntitledProject";
        }

        auto createdProject = assets::Project::Create(manifestPath, projectName, &errorMessage);
        if (!createdProject)
        {
            m_statusMessage = errorMessage.empty() ? "Failed to create project." : errorMessage;
            return false;
        }

        m_project = std::move(createdProject);
        ApplyProjectContext();
        SetScene(CreateEmptyScene());

        std::string scriptErrorMessage;
        if (!EnsureProjectScriptBuildScaffold(&scriptErrorMessage))
        {
            m_statusMessage = scriptErrorMessage.empty() ? "Failed to prepare project scripting." : scriptErrorMessage;
            return false;
        }

        ReloadProjectScriptAssembly();

        if (!SaveProjectToDisk())
        {
            return false;
        }

        m_statusMessage = "Created project: " + m_project->GetManifest().name;
        m_undoStack.clear();
        m_redoStack.clear();
        MarkSceneClean();
        MarkProjectClean();
        Log(ConsoleSeverity::Info, m_statusMessage);
        UpdateWindowTitle();
        return true;
    }

    bool EditorShell::SaveProjectToDisk()
    {
        if (!m_project)
        {
            m_statusMessage = "No project loaded.";
            return false;
        }

        ApplyProjectContext();

        if (!IsRuntimeExportProject())
        {
            std::string scriptErrorMessage;
            if (!EnsureProjectScriptBuildScaffold(&scriptErrorMessage))
            {
                m_statusMessage = scriptErrorMessage.empty() ? "Failed to prepare project scripting." : scriptErrorMessage;
                return false;
            }
        }

        auto &manifest = m_project->GetManifest();
        manifest.editorCamera = BuildProjectEditorCameraSettings(m_editorCamera);
        manifest.editorCameraPostProcessEffects = BuildProjectEditorPostProcessEffects(m_editorCamera);
        if (manifest.windowTitle.empty())
        {
            manifest.windowTitle = manifest.name;
        }

        if (!SaveActiveSceneIntoProject())
        {
            return false;
        }

        m_project->RefreshAssetRegistry();

        std::string errorMessage;
        if (!m_project->Save(&errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Failed to save project." : errorMessage;
            return false;
        }

        m_statusMessage = "Saved project: " + m_project->GetManifestPath().filename().string();
        m_undoStack.clear();
        m_redoStack.clear();
        MarkSceneClean();
        MarkProjectClean();
        Log(ConsoleSeverity::Info, m_statusMessage);
        UpdateWindowTitle();
        return true;
    }

    bool EditorShell::BuildProjectToPath(const std::filesystem::path &destinationExecutablePath)
    {
        if (!m_project)
        {
            m_statusMessage = "No project loaded.";
            return false;
        }

        if (!SaveProjectToDisk())
        {
            return false;
        }

        const auto runtimeExecutablePath = assets::FindRuntimeExecutable(GetProcessDirectory());
        if (runtimeExecutablePath.empty())
        {
            m_statusMessage = "Could not find PlutoGERuntime executable to export.";
            return false;
        }

        std::string errorMessage;
        if (!RebuildStandaloneRuntime(runtimeExecutablePath, &errorMessage))
        {
            m_statusMessage = errorMessage;
            return false;
        }

        if (!assets::ExportStandaloneProject(*m_project, destinationExecutablePath, runtimeExecutablePath, &errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Failed to build project." : errorMessage;
            return false;
        }

        m_statusMessage = "Built project: " + std::filesystem::path(destinationExecutablePath).filename().string();
        return true;
    }

    bool EditorShell::BuildAndRunProjectToPath(const std::filesystem::path &destinationExecutablePath)
    {
        if (!BuildProjectToPath(destinationExecutablePath))
        {
            return false;
        }

        std::string errorMessage;
        if (!LaunchExecutable(destinationExecutablePath, &errorMessage))
        {
            m_statusMessage = errorMessage;
            return false;
        }

        m_statusMessage = "Built and launched project: " + std::filesystem::path(destinationExecutablePath).filename().string();
        return true;
    }

    void EditorShell::Initialize()
    {
        auto config = core::EngineConfig{
            platform::WindowConfig{
                .title = "PlutoGE Editor",
                .width = 1280,
                .height = 720,
                .resizable = true,
                .visible = true,
                .fullscreen = false,
            }};
        if (!m_engine.Initialize(config))
        {
            std::cerr << "Failed to initialize Engine in EditorShell" << std::endl;
        }

        InitializeEditorCamera();
        ApplyProjectContext();
        SetScene(CreateEmptyScene());
        m_statusMessage = "Ready";
        UpdateWindowTitle();

        m_panelManager.InitializeImGui(&m_engine.GetWindow());
    }

    glm::vec3 randomColour()
    {
        // maximum value for each color channel is 255, so we divide by 255 to get a value between 0 and 1
        return glm::vec3(
            static_cast<float>(rand()) / static_cast<float>(RAND_MAX),
            static_cast<float>(rand()) / static_cast<float>(RAND_MAX),
            static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
    }

    void EditorShell::Render()
    {
        auto &window = m_engine.GetWindow();
        auto &renderer = m_engine.GetRenderer();
        auto deltaTime = std::chrono::duration<float>::zero();
        auto lastTime = std::chrono::high_resolution_clock::now();

        ViewportPanelConfig viewportConfig;
        viewportConfig.name = "Editor Viewport";
        viewportConfig.clearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);
        viewportConfig.initialRenderScale = 1.0f;
        viewportConfig.editorViewport = true;
        auto *viewportPanel = new ViewportPanel(viewportConfig);
        viewportPanel->Initialize();

        auto *sceneHierarchyPanel = new SceneHierarchyPanel(PanelConfig{"Scene Hierarchy"});
        sceneHierarchyPanel->Initialize();

        m_panelManager.AddPanel(viewportPanel);
        m_panelManager.AddPanel(sceneHierarchyPanel);

        auto *renderTarget = viewportPanel->GetRenderTarget();

        ViewportPanelConfig viewportConfig2;
        viewportConfig2.name = "Game Viewport";
        viewportConfig2.openByDefault = true;
        viewportConfig2.clearColor = glm::vec4(0.15f, 0.1f, 0.1f, 1.0f);
        viewportConfig2.initialRenderScale = 1.0f;
        auto viewportPanel2 = new ViewportPanel(viewportConfig2);
        viewportPanel2->Initialize();
        m_panelManager.AddPanel(viewportPanel2);

        auto inspectorPanel = new InspectorPanel(PanelConfig{"Inspector"});
        inspectorPanel->Initialize();
        m_panelManager.AddPanel(inspectorPanel);

        auto contentBrowserPanel = new ContentBrowserPanel(PanelConfig{"Content Browser"});
        contentBrowserPanel->Initialize();
        m_panelManager.AddPanel(contentBrowserPanel);

        auto consolePanel = new ConsolePanel(PanelConfig{"Console"});
        consolePanel->Initialize();
        m_panelManager.AddPanel(consolePanel);

        auto materialEditorPanel = new MaterialEditorPanel(PanelConfig{"Material Editor", false});
        materialEditorPanel->Initialize();
        m_panelManager.AddPanel(materialEditorPanel);

        auto profilerPanel = new ProfilerPanel(PanelConfig{"Profiler"}, &m_profiler, &m_panelManager, &renderer);
        profilerPanel->Initialize();
        m_panelManager.AddPanel(profilerPanel);

        auto *renderTarget2 = viewportPanel2->GetRenderTarget();
        auto *windowHandle = static_cast<GLFWwindow *>(window.GetWindow());
        bool isEditorCameraLookActive = false;
        double lastEditorCameraCursorX = 0.0;
        double lastEditorCameraCursorY = 0.0;
        std::array<char, 256> projectNameBuffer{};
        std::array<char, 256> projectWindowTitleBuffer{};
        std::array<char, 512> projectScriptAssemblyBuffer{};
        int projectWindowWidth = 1280;
        int projectWindowHeight = 720;
        bool projectVSyncEnabled = true;
        int projectGraphicsApiIndex = 0;
        bool shouldOpenProjectSettingsPopup = false;

        auto loadProjectSettingsDraft = [&]()
        {
            if (!m_project)
            {
                return;
            }

            const auto &manifest = m_project->GetManifest();
            std::memset(projectNameBuffer.data(), 0, projectNameBuffer.size());
            std::memset(projectWindowTitleBuffer.data(), 0, projectWindowTitleBuffer.size());
            std::memset(projectScriptAssemblyBuffer.data(), 0, projectScriptAssemblyBuffer.size());

            const std::size_t projectNameLength = (std::min)(manifest.name.size(), projectNameBuffer.size() - 1);
            std::memcpy(projectNameBuffer.data(), manifest.name.c_str(), projectNameLength);

            const std::size_t windowTitleLength = (std::min)(manifest.windowTitle.size(), projectWindowTitleBuffer.size() - 1);
            std::memcpy(projectWindowTitleBuffer.data(), manifest.windowTitle.c_str(), windowTitleLength);

            const std::size_t scriptAssemblyLength = (std::min)(manifest.scriptAssembly.size(), projectScriptAssemblyBuffer.size() - 1);
            std::memcpy(projectScriptAssemblyBuffer.data(), manifest.scriptAssembly.c_str(), scriptAssemblyLength);

            projectWindowWidth = manifest.windowWidth;
            projectWindowHeight = manifest.windowHeight;
            projectVSyncEnabled = manifest.vSyncEnabled;
            switch (manifest.graphicsApi)
            {
            case assets::ProjectGraphicsApi::D3D12:
                projectGraphicsApiIndex = 1;
                break;
            case assets::ProjectGraphicsApi::Vulkan:
                projectGraphicsApiIndex = 2;
                break;
            case assets::ProjectGraphicsApi::OpenGL:
            default:
                projectGraphicsApiIndex = 0;
                break;
            }
        };

        bool editorVSyncEnabled = m_project ? m_project->GetManifest().vSyncEnabled : false;
        bool appliedEditorVSyncEnabled = editorVSyncEnabled;
        renderer.SetVSyncEnabled(appliedEditorVSyncEnabled);

        while (!window.ShouldClose())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            deltaTime = currentTime - lastTime;
            const float deltaSeconds = deltaTime.count();
            EditorFrameTimingStats frameTimingStats{};
            renderer.BeginProfilingFrame();

            const bool isRuntimeRunning = m_engine.IsRuntimeRunning();
            window.SetScriptInputEnabled(isRuntimeRunning && viewportPanel2->IsViewportFocused());
            if (!isRuntimeRunning)
            {
                window.SetCursorLocked(false);
            }

            viewportPanel->SetPanelControlsEnabled(!isRuntimeRunning);
            viewportPanel2->SetPanelControlsEnabled(!isRuntimeRunning);

            const auto renderTargetWidth = renderTarget->GetWidth();
            const auto renderTargetHeight = renderTarget->GetHeight();
            const auto renderTarget2Width = renderTarget2->GetWidth();
            const auto renderTarget2Height = renderTarget2->GetHeight();

            const bool bakeTaskFinished = m_activeBakeTask && m_activeBakeTask->IsFinished();
            if (bakeTaskFinished && m_scene)
            {
                const auto bakeResult = m_activeBakeTask->Finalize(*m_scene);
                m_statusMessage = bakeResult.message;
                std::cout << bakeResult.message << std::endl;

                if (bakeResult.succeeded && !m_scene->GetFilePath().empty())
                {
                    std::string errorMessage;
                    if (scene::SceneSerializer::Save(*m_scene, m_scene->GetFilePath(), &errorMessage))
                    {
                        m_statusMessage += " Saved scene.";
                    }
                    else if (!errorMessage.empty())
                    {
                        m_statusMessage += " " + errorMessage;
                    }
                }

                m_activeBakeTask.reset();
            }

            const bool isBakeRunning = m_activeBakeTask && m_activeBakeTask->IsRunning();
            if (isBakeRunning)
            {
                m_statusMessage = m_activeBakeTask->GetStatusMessage();
            }

            // Scene update

            const auto sceneUpdateStart = std::chrono::high_resolution_clock::now();
            if (!isBakeRunning)
            {
                m_engine.UpdateAsyncMeshImports();
                if (m_scene)
                {
                    m_scene->Update(deltaTime.count());
                }
            }
            const auto sceneUpdateEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.sceneUpdateMs = std::chrono::duration<float, std::milli>(sceneUpdateEnd - sceneUpdateStart).count();

            if (!isBakeRunning && m_scene && !m_pendingIblCaptureEntities.empty())
            {
                auto pendingCaptures = std::move(m_pendingIblCaptureEntities);
                m_pendingIblCaptureEntities.clear();
                for (const auto entityId : pendingCaptures)
                {
                    auto *entity = m_scene->FindEntityByID(entityId);
                    auto *iblCaptureComponent = entity ? entity->GetComponent<scene::IblCaptureComponent>() : nullptr;
                    if (!entity || !entity->IsActive() || !iblCaptureComponent || !iblCaptureComponent->IsEnabled())
                    {
                        continue;
                    }

                    auto *captureTexture = iblCaptureComponent->EnsureCaptureTexture();
                    if (!captureTexture)
                    {
                        m_statusMessage = "IBL capture failed: could not create cubemap.";
                        continue;
                    }

                    const bool captured = renderer.CaptureSceneCubemap(
                        entity->GetWorldPosition(),
                        iblCaptureComponent->GetResolution(),
                        iblCaptureComponent->GetFarPlane(),
                        captureTexture,
                        m_scene->GetLights(),
                        m_scene.get());
                    if (!captured)
                    {
                        m_statusMessage = "IBL capture failed.";
                        continue;
                    }

                    iblCaptureComponent->ClearDirty();
                    m_scene->AddIblCaptureVolume(iblCaptureComponent->BuildCaptureVolume());
                    m_statusMessage = "IBL capture complete.";
                }
            }

            if (isRuntimeRunning)
            {
                if (isEditorCameraLookActive)
                {
                    SetCursorCapture(windowHandle, false);
                    isEditorCameraLookActive = false;
                }
            }
            else
            {
                UpdateEditorCamera(m_editorCamera,
                                   windowHandle,
                                   viewportPanel->IsViewportHovered() || viewportPanel->IsViewportFocused(),
                                   deltaSeconds,
                                   isEditorCameraLookActive,
                                   lastEditorCameraCursorX,
                                   lastEditorCameraCursorY);
            }

            auto *cameraComponent2 = FindFirstSceneCamera(m_scene.get());
            const bool shouldRenderViewport1 = viewportPanel->ShouldRenderFrame();
            const bool shouldRenderViewport2 = viewportPanel2->ShouldRenderFrame() && IsCameraActiveInScene(m_scene.get(), cameraComponent2);

            const auto viewportRenderStart = std::chrono::high_resolution_clock::now();
            if (shouldRenderViewport1)
            {
                ++frameTimingStats.renderedViewportCount;
                const glm::mat4 editorCameraTransform = GetEditorCameraTransform(m_editorCamera);
                const auto editorCameraData = m_editorCamera.camera.GetCameraDataForTransform(editorCameraTransform,
                                                                                              renderTarget->GetWidth(),
                                                                                              renderTarget->GetHeight());
                std::vector<render::IPostProcessEffect *> editorPostProcessEffects;
                editorPostProcessEffects.reserve(m_editorCamera.GetPostProcessEffects().size());
                for (const auto &effect : m_editorCamera.GetPostProcessEffects())
                {
                    editorPostProcessEffects.push_back(effect.get());
                }

                renderer.RenderFrame(editorCameraData,
                                     renderTarget,
                                     m_scene ? m_scene->GetLights() : std::vector<scene::Light *>{},
                                     &editorPostProcessEffects,
                                     m_scene.get(),
                                     viewportPanel->IsGridVisible());
            }
            else
            {
                viewportPanel->ClearFrame();
            }

            if (shouldRenderViewport2)
            {
                ++frameTimingStats.renderedViewportCount;
                viewportPanel2->RenderFrame(*cameraComponent2);
            }
            else
            {
                viewportPanel2->ClearFrame();
            }
            const auto viewportRenderEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.viewportRenderMs = std::chrono::duration<float, std::milli>(viewportRenderEnd - viewportRenderStart).count();

            renderer.ClearRenderCommands();

            // UI

            const auto beginFrameStart = std::chrono::high_resolution_clock::now();
            renderer.BeginFrame();
            const auto beginFrameEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.rendererBeginFrameMs = std::chrono::duration<float, std::milli>(beginFrameEnd - beginFrameStart).count();

            m_panelManager.BeginPanelUpdate();

            if (!isRuntimeRunning && ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput)
            {
                SetSelectedEntity(nullptr);
            }

            const ImGuiIO &io = ImGui::GetIO();
            if (!isRuntimeRunning && !io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
            {
                Undo();
            }
            if (!isRuntimeRunning && !io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
            {
                Redo();
            }

            auto sanitizeBakeSettings = [](scene::SceneBakeSettings &settings)
            {
                settings.lightmapResolution = (std::max)(settings.lightmapResolution, 4);
                settings.lightmapTileSize = (std::max)(settings.lightmapTileSize, 1);
                settings.indirectBounceSampleCount = (std::max)(settings.indirectBounceSampleCount, 0);
                settings.probeDirectionCount = (std::max)(settings.probeDirectionCount, 0);
                settings.lightmapBounceStrength = (std::max)(settings.lightmapBounceStrength, 0.0f);
                settings.probeBounceStrength = (std::max)(settings.probeBounceStrength, 0.0f);
            };

            auto runBake = [&](const scene::SceneBakeSettings &requestedSettings)
            {
                if (!m_scene || m_activeBakeTask)
                {
                    return;
                }

                scene::SceneBakeSettings bakeSettings = requestedSettings;
                sanitizeBakeSettings(bakeSettings);

                scene::SceneBaker baker;
                scene::SceneBakeResult immediateResult;
                auto bakeTask = baker.BeginBake(*m_scene, bakeSettings, &immediateResult);
                if (!bakeTask)
                {
                    m_statusMessage = immediateResult.message;
                    std::cout << immediateResult.message << std::endl;
                    return;
                }

                m_activeBakeTask = std::move(bakeTask);
                m_statusMessage = m_activeBakeTask->GetStatusMessage();
                std::cout << m_statusMessage << std::endl;
            };

            // Toolbar menu
            if (ImGui::BeginMainMenuBar())
            {
                if (ConsumeMaterialEditorOpenRequest())
                {
                    materialEditorPanel->SetOpen(true);
                }

                if (ImGui::BeginMenu("File"))
                {
                    if (isBakeRunning && ImGui::MenuItem("Cancel Bake"))
                    {
                        m_activeBakeTask->Cancel();
                        m_statusMessage = "Cancelling bake...";
                    }
                    if (isBakeRunning)
                    {
                        ImGui::Separator();
                    }

                    ImGui::BeginDisabled(isBakeRunning);
                    if (ImGui::MenuItem("New Project..."))
                    {
                        if (ConfirmContinueWithUnsavedChanges())
                        {
                            const std::string projectPath = ShowSaveFileDialog(kProjectFileFilter, kDefaultProjectFileName, "plutoproject");
                            if (!projectPath.empty())
                            {
                                CreateProjectAtPath(projectPath);
                            }
                        }
                    }
                    if (ImGui::MenuItem("Open Project..."))
                    {
                        if (ConfirmContinueWithUnsavedChanges())
                        {
                            const std::string projectPath = ShowOpenFileDialog(kProjectFileFilter);
                            if (!projectPath.empty())
                            {
                                LoadProjectFromPath(projectPath);
                            }
                        }
                    }
                    if (ImGui::MenuItem("Save Project", nullptr, false, m_project != nullptr))
                    {
                        SaveProjectToDisk();
                    }
                    if (ImGui::MenuItem("Project Settings...", nullptr, false, m_project != nullptr))
                    {
                        loadProjectSettingsDraft();
                        shouldOpenProjectSettingsPopup = true;
                    }
                    if (ImGui::MenuItem("Build Project...", nullptr, false, m_project != nullptr))
                    {
                        const std::string suggestedPath = GetDefaultExportExecutablePath().empty()
                                                              ? std::string("PlutoGERuntime.exe")
                                                              : GetDefaultExportExecutablePath().string();
                        const std::string exportPath = ShowSaveFileDialog(kExecutableFileFilter, suggestedPath, "exe");
                        if (!exportPath.empty())
                        {
                            BuildProjectToPath(exportPath);
                        }
                    }
                    if (ImGui::MenuItem("Build and Run Project...", nullptr, false, m_project != nullptr))
                    {
                        const std::string suggestedPath = GetDefaultExportExecutablePath().empty()
                                                              ? std::string("PlutoGERuntime.exe")
                                                              : GetDefaultExportExecutablePath().string();
                        const std::string exportPath = ShowSaveFileDialog(kExecutableFileFilter, suggestedPath, "exe");
                        if (!exportPath.empty())
                        {
                            BuildAndRunProjectToPath(exportPath);
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("New Scene"))
                    {
                        if (ConfirmContinueWithUnsavedChanges())
                        {
                            SetScene(CreateEmptyScene());
                            m_undoStack.clear();
                            m_redoStack.clear();
                            MarkSceneDirty();
                            m_statusMessage = "Created new scene";
                            Log(ConsoleSeverity::Info, m_statusMessage);
                        }
                    }
                    if (ImGui::MenuItem("Open Scene..."))
                    {
                        const std::string filePath = ShowOpenFileDialog(kSceneFileFilter);
                        if (!filePath.empty())
                        {
                            OpenSceneFromPath(filePath);
                        }
                    }
                    if (ImGui::MenuItem("Save Scene"))
                    {
                        if (m_scene)
                        {
                            std::string savePath = m_scene->GetFilePath();
                            if (savePath.empty())
                            {
                                savePath = ShowSaveFileDialog(kSceneFileFilter,
                                                              m_project ? GetDefaultProjectScenePath().string() : std::string("scene.plutoscene"),
                                                              "plutoscene");
                            }

                            if (!savePath.empty())
                            {
                                SaveSceneToPath(savePath);
                            }
                        }
                    }
                    if (ImGui::MenuItem("Save Scene As..."))
                    {
                        if (m_scene)
                        {
                            const std::string suggestedPath = m_scene->GetFilePath().empty() ? "scene.plutoscene" : m_scene->GetFilePath();
                            const std::string savePath = ShowSaveFileDialog(kSceneFileFilter, suggestedPath, "plutoscene");
                            if (!savePath.empty())
                            {
                                SaveSceneToPath(savePath);
                            }
                        }
                    }
                    if (ImGui::MenuItem("Bake Scene"))
                    {
                        runBake(scene::SceneBakeSettings::BalancedPreview());
                    }
                    if (ImGui::MenuItem("Bake Scene Fast"))
                    {
                        runBake(scene::SceneBakeSettings::FastPreview());
                    }
                    if (ImGui::MenuItem("Bake Scene Final"))
                    {
                        runBake(scene::SceneBakeSettings::Final());
                    }
                    if (ImGui::MenuItem("Bake Scene Custom..."))
                    {
                        ImGui::OpenPopup("Bake Scene Custom");
                    }
                    ImGui::EndDisabled();

                    if (ImGui::MenuItem("Exit"))
                    {
                        if (ConfirmContinueWithUnsavedChanges())
                        {
                            window.Close();
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    ImGui::BeginDisabled(!CanUndo());
                    if (ImGui::MenuItem("Undo", "Ctrl+Z"))
                    {
                        Undo();
                    }
                    ImGui::EndDisabled();

                    ImGui::BeginDisabled(!CanRedo());
                    if (ImGui::MenuItem("Redo", "Ctrl+Y"))
                    {
                        Redo();
                    }
                    ImGui::EndDisabled();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View"))
                {
                    if (ImGui::MenuItem("Editor Viewport", NULL, viewportPanel->IsOpen()))
                    {
                        viewportPanel->SetOpen(!viewportPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Game Viewport", NULL, viewportPanel2->IsOpen()))
                    {
                        viewportPanel2->SetOpen(!viewportPanel2->IsOpen());
                    }
                    if (ImGui::MenuItem("Scene Hierarchy", NULL, sceneHierarchyPanel->IsOpen()))
                    {
                        sceneHierarchyPanel->SetOpen(!sceneHierarchyPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Inspector", NULL, inspectorPanel->IsOpen()))
                    {
                        inspectorPanel->SetOpen(!inspectorPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Content Browser", NULL, contentBrowserPanel->IsOpen()))
                    {
                        contentBrowserPanel->SetOpen(!contentBrowserPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Console", NULL, consolePanel->IsOpen()))
                    {
                        consolePanel->SetOpen(!consolePanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Material Editor", NULL, materialEditorPanel->IsOpen()))
                    {
                        materialEditorPanel->SetOpen(!materialEditorPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Profiler", NULL, profilerPanel->IsOpen()))
                    {
                        profilerPanel->SetOpen(!profilerPanel->IsOpen());
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Runtime"))
                {
                    const bool canRunRuntime = m_scene != nullptr;
                    ImGui::BeginDisabled(!canRunRuntime || m_engine.IsRuntimeRunning());
                    if (ImGui::MenuItem("Play"))
                    {
                        m_engine.StartRuntime();
                        window.SetScriptInputEnabled(viewportPanel2->IsViewportFocused());
                        m_statusMessage = "Runtime started.";
                    }
                    ImGui::EndDisabled();

                    ImGui::BeginDisabled(!m_engine.IsRuntimeRunning());
                    if (ImGui::MenuItem("Stop"))
                    {
                        m_engine.StopRuntime();
                        window.SetScriptInputEnabled(false);
                        window.SetCursorLocked(false);
                        m_statusMessage = "Runtime stopped.";
                    }
                    ImGui::EndDisabled();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Scripts"))
                {
                    ImGui::BeginDisabled(m_project == nullptr || IsRuntimeExportProject());
                    if (ImGui::MenuItem("Build Scripts"))
                    {
                        BuildProjectScripts();
                    }
                    if (ImGui::MenuItem("Reload Script Assembly"))
                    {
                        std::string scriptErrorMessage;
                        if (ReloadProjectScriptAssembly(&scriptErrorMessage))
                        {
                            m_statusMessage = "Reloaded script assembly.";
                        }
                        else
                        {
                            m_statusMessage = scriptErrorMessage.empty() ? "Failed to reload script assembly." : scriptErrorMessage;
                        }
                    }
                    ImGui::EndDisabled();
                    if (m_project != nullptr && IsRuntimeExportProject())
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("Open the source project to edit or build scripts.");
                    }
                    ImGui::EndMenu();
                }
                if (!m_statusMessage.empty())
                {
                    ImGui::Separator();
                    ImGui::TextUnformatted(m_statusMessage.c_str());
                }
                ImGui::EndMainMenuBar();
            }

            if (shouldOpenProjectSettingsPopup)
            {
                ImGui::OpenPopup("Project Settings");
                shouldOpenProjectSettingsPopup = false;
            }

            if (ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (!m_project)
                {
                    ImGui::TextUnformatted("No project loaded.");
                    if (ImGui::Button("Close"))
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                else
                {
                    auto &manifest = m_project->GetManifest();
                    projectWindowWidth = (std::max)(projectWindowWidth, 64);
                    projectWindowHeight = (std::max)(projectWindowHeight, 64);

                    ImGui::InputText("Project Name", projectNameBuffer.data(), projectNameBuffer.size());
                    ImGui::InputText("Window Title", projectWindowTitleBuffer.data(), projectWindowTitleBuffer.size());
                    ImGui::InputInt("Window Width", &projectWindowWidth);
                    ImGui::InputInt("Window Height", &projectWindowHeight);
                    ImGui::Combo("Graphics API", &projectGraphicsApiIndex, "OpenGL\0DirectX 12\0Vulkan\0");
                    ImGui::Checkbox("VSync", &projectVSyncEnabled);
                    ImGui::InputText("Script Assembly", projectScriptAssemblyBuffer.data(), projectScriptAssemblyBuffer.size());
                    ImGui::SameLine();
                    if (ImGui::Button("...##ScriptAssembly"))
                    {
#ifdef _WIN32
                        OPENFILENAMEA ofn = {};
                        char fileName[MAX_PATH] = "";
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = nullptr;
                        ofn.lpstrFilter = "Managed Assemblies\0*.dll\0All Files\0*.*\0";
                        ofn.lpstrFile = fileName;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                        if (GetOpenFileNameA(&ofn))
                        {
                            strncpy_s(projectScriptAssemblyBuffer.data(), projectScriptAssemblyBuffer.size(), fileName, _TRUNCATE);
                        }
#endif
                    }

                    ImGui::Separator();
                    ImGui::Text("Manifest: %s", m_project->GetManifestPath().string().c_str());
                    ImGui::Text("Asset Directory: %s", manifest.assetDirectory.c_str());
                    ImGui::Text("Startup Scene: %s", manifest.startupScene.empty() ? "<none>" : manifest.startupScene.c_str());

                    if (ImGui::Button("Save"))
                    {
                        manifest.name = projectNameBuffer.data();
                        manifest.windowTitle = projectWindowTitleBuffer.data();
                        manifest.windowWidth = (std::max)(projectWindowWidth, 64);
                        manifest.windowHeight = (std::max)(projectWindowHeight, 64);
                        manifest.vSyncEnabled = projectVSyncEnabled;
                        switch (projectGraphicsApiIndex)
                        {
                        case 1:
                            manifest.graphicsApi = assets::ProjectGraphicsApi::D3D12;
                            break;
                        case 2:
                            manifest.graphicsApi = assets::ProjectGraphicsApi::Vulkan;
                            break;
                        case 0:
                        default:
                            manifest.graphicsApi = assets::ProjectGraphicsApi::OpenGL;
                            break;
                        }
                        manifest.scriptAssembly = projectScriptAssemblyBuffer[0] == '\0'
                                                      ? std::string{}
                                                      : m_project->MakeAssetReference(projectScriptAssemblyBuffer.data());

                        if (SaveProjectToDisk())
                        {
                            std::string scriptErrorMessage;
                            ReloadProjectScriptAssembly(&scriptErrorMessage);
                            if (!scriptErrorMessage.empty())
                            {
                                m_statusMessage = scriptErrorMessage;
                            }
                            editorVSyncEnabled = manifest.vSyncEnabled;
                            appliedEditorVSyncEnabled = editorVSyncEnabled;
                            renderer.SetVSyncEnabled(appliedEditorVSyncEnabled);
                            if (ToRenderBackend(manifest.graphicsApi) != renderer.GetBackend())
                            {
                                m_statusMessage += " Graphics API changes apply when the renderer is recreated.";
                            }
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
            }

            if (ImGui::BeginPopupModal("Bake Scene Custom", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                sanitizeBakeSettings(m_customBakeSettings);

                if (ImGui::Button("Fast Preset"))
                {
                    m_customBakeSettings = scene::SceneBakeSettings::FastPreview();
                }
                ImGui::SameLine();
                if (ImGui::Button("Balanced Preset"))
                {
                    m_customBakeSettings = scene::SceneBakeSettings::BalancedPreview();
                }
                ImGui::SameLine();
                if (ImGui::Button("Final Preset"))
                {
                    m_customBakeSettings = scene::SceneBakeSettings::Final();
                }

                ImGui::Separator();
                ImGui::InputInt("Lightmap Resolution", &m_customBakeSettings.lightmapResolution);
                ImGui::InputInt("Lightmap Tile Size", &m_customBakeSettings.lightmapTileSize);
                ImGui::Checkbox("Bake Indirect Bounce", &m_customBakeSettings.bakeIndirectBounce);
                ImGui::InputInt("Indirect Samples", &m_customBakeSettings.indirectBounceSampleCount);
                ImGui::SliderFloat("Lightmap Bounce Strength", &m_customBakeSettings.lightmapBounceStrength, 0.0f, 4.0f, "%.2f");
                ImGui::Checkbox("Bake Probe Volume", &m_customBakeSettings.bakeProbeVolume);
                ImGui::InputInt("Probe Directions", &m_customBakeSettings.probeDirectionCount);
                ImGui::SliderFloat("Probe Bounce Strength", &m_customBakeSettings.probeBounceStrength, 0.0f, 4.0f, "%.2f");

                sanitizeBakeSettings(m_customBakeSettings);

                ImGui::BeginDisabled(isBakeRunning);
                if (ImGui::Button("Bake"))
                {
                    runBake(m_customBakeSettings);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Close"))
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            if (isBakeRunning)
            {
                ImGui::BeginDisabled();
            }

            const bool playModePanelsDisabled = m_engine.IsRuntimeRunning();
            viewportPanel->SetInteractionEnabled(true);
            viewportPanel->SetVisualAlpha(playModePanelsDisabled ? 0.35f : 1.0f);
            sceneHierarchyPanel->SetInteractionEnabled(true);
            sceneHierarchyPanel->SetVisualAlpha(playModePanelsDisabled ? 0.35f : 1.0f);
            inspectorPanel->SetInteractionEnabled(true);
            inspectorPanel->SetVisualAlpha(playModePanelsDisabled ? 0.35f : 1.0f);
            profilerPanel->SetInteractionEnabled(true);
            profilerPanel->SetVisualAlpha(playModePanelsDisabled ? 0.35f : 1.0f);
            viewportPanel2->SetInteractionEnabled(true);
            viewportPanel2->SetVisualAlpha(1.0f);

            m_panelManager.UpdatePanels();

            window.SetScriptInputEnabled(m_engine.IsRuntimeRunning() && viewportPanel2->IsViewportFocused());
            if (isBakeRunning)
            {
                ImGui::EndDisabled();
            }

            m_panelManager.EndPanelUpdate();

            const bool interactiveEdit =
                !isRuntimeRunning &&
                (isEditorCameraLookActive ||
                 viewportPanel->IsTransformGizmoUsing() ||
                 viewportPanel2->IsTransformGizmoUsing());
            const bool desiredEditorVSyncEnabled = editorVSyncEnabled && !interactiveEdit;
            if (desiredEditorVSyncEnabled != appliedEditorVSyncEnabled)
            {
                appliedEditorVSyncEnabled = desiredEditorVSyncEnabled;
                renderer.SetVSyncEnabled(appliedEditorVSyncEnabled);
            }

            const auto presentStart = std::chrono::high_resolution_clock::now();
            renderer.EndFrame();
            const auto presentEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.presentMs = std::chrono::duration<float, std::milli>(presentEnd - presentStart).count();

            const auto pollEventsStart = std::chrono::high_resolution_clock::now();
            window.PollEvents();
            const auto pollEventsEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.eventPollingMs = std::chrono::duration<float, std::milli>(pollEventsEnd - pollEventsStart).count();

            const auto frameEndTime = std::chrono::high_resolution_clock::now();
            m_profiler.SetLatestFrameTimingStats(frameTimingStats);
            m_profiler.AddFrameSample(std::chrono::duration<float, std::milli>(frameEndTime - currentTime).count());

            lastTime = currentTime;
        }

        if (isEditorCameraLookActive)
        {
            SetCursorCapture(windowHandle, false);
        }
    }

    void EditorShell::Shutdown()
    {
        if (m_activeBakeTask)
        {
            m_activeBakeTask->Cancel();
            m_activeBakeTask.reset();
        }
        m_selectedEntity = nullptr;
        m_project.reset();
        m_scene.reset();
        m_engine.GetAssetManager().ClearProjectContext();
        m_engine.SetScene(nullptr);
        m_panelManager.ShutdownPanels();
        m_engine.Shutdown();
    }
}
