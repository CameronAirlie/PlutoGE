#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ProfilerPanel.h"
#include "PlutoGE/ui/panels/ConsolePanel.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include "PlutoGE/ui/panels/AnimationGraphEditorPanel.h"
#include "PlutoGE/ui/panels/AnimationClipEditorPanel.h"
#include "PlutoGE/ui/panels/MaterialEditorPanel.h"
#include "PlutoGE/ui/panels/ParticleSystemEditorPanel.h"
#include "PlutoGE/ui/panels/InputMappingEditorPanel.h"
#include "PlutoGE/ui/panels/MeshEditorPanel.h"
#include "PlutoGE/ui/panels/ShaderGraphEditorPanel.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/CanvasEditorPanel.h"
#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptLogging.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Prefab.h"
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
#ifndef _WIN32
#include <cstdio>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

namespace PlutoGE::ui
{
    bool EditorShell::EditorViewportCamera::SetPostProcessPresetAssetReference(std::string assetReference)
    {
        auto &window = core::Engine::GetInstance().GetWindow();
        if (window.IsOpen() && !window.EnsureOpenGLContextCurrent(true))
        {
            return false;
        }

        if (assetReference.empty())
        {
            postProcessPresetAssetReference.clear();
            postProcessEffects.clear();
            return true;
        }
        bool loaded = false;
        const auto preset = core::Engine::GetInstance().GetAssetManager().LoadPostProcessPresetAsset(assetReference, &loaded);
        if (!loaded)
            return false;
        postProcessEffects = assets::InstantiatePostProcessPreset(preset);
        postProcessPresetAssetReference = std::move(assetReference);
        return true;
    }

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
        constexpr float kEditorCameraBoostMultiplier = 2.5f;
        constexpr float kEditorCameraScrollStepFactor = 1.2f;
        constexpr float kEditorCameraMinMoveSpeed = 0.1f;
        constexpr float kEditorCameraMaxMoveSpeed = 1000.0f;
        constexpr float kEditorCameraMinSpeedAdjustment = 0.1f;
        constexpr float kEditorCameraMaxSpeedAdjustment = 10.0f;
        constexpr float kEditorCameraMouseSensitivity = 0.12f;
        constexpr float kEditorCameraPitchLimitDegrees = 89.0f;
        constexpr const char *kSceneFileFilter = "PlutoGE Scene\0*.plutoscene\0All Files\0*.*\0";
        constexpr const char *kProjectFileFilter = "PlutoGE Project\0*.plutoproject\0All Files\0*.*\0";
#ifdef _WIN32
        constexpr const char *kExecutableFileFilter = "Executable\0*.exe\0All Files\0*.*\0";
#else
        constexpr const char *kExecutableFileFilter = "Executable\0*\0All Files\0*\0";
#endif
        constexpr const char *kRuntimeExecutableName =
#ifdef _WIN32
            "PlutoGERuntime.exe";
#else
            "PlutoGERuntime";
#endif
        constexpr const char *kDefaultProjectFileName = "UntitledProject.plutoproject";
        constexpr const char *kDefaultProjectSceneRelativePath = "Scenes/Main.plutoscene";
        constexpr std::string_view kDefaultProjectScriptDirectory = "Scripts";
        constexpr std::string_view kDefaultProjectManagedDirectory = "Managed";
        constexpr int kScriptCoreSearchAncestorLimit = 8;
        constexpr std::size_t kMaxRecentProjects = 10;

        std::filesystem::path GetEditorSettingsDirectory()
        {
#ifdef _WIN32
            if (const char *appData = std::getenv("APPDATA"); appData != nullptr && appData[0] != '\0')
            {
                return std::filesystem::path(appData) / "PlutoGE";
            }
#else
            if (const char *configHome = std::getenv("XDG_CONFIG_HOME"); configHome != nullptr && configHome[0] != '\0')
            {
                return std::filesystem::path(configHome) / "PlutoGE";
            }
            if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
            {
                return std::filesystem::path(home) / ".config" / "PlutoGE";
            }
#endif
            return std::filesystem::current_path();
        }

        std::filesystem::path GetRecentProjectsPath()
        {
            return GetEditorSettingsDirectory() / "recent-projects.txt";
        }

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
                // Installed distributions ship a prebuilt runtime and intentionally
                // have no CMake build tree. Rebuild only developer-tree runtimes.
                return true;
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
            const pid_t child = fork();
            if (child < 0)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to launch built project executable.";
                }
                return false;
            }
            if (child == 0)
            {
                const auto workingDirectory = executablePath.parent_path().string();
                if (!workingDirectory.empty())
                {
                    (void)chdir(workingDirectory.c_str());
                }
                const auto executable = executablePath.string();
                execl(executable.c_str(), executable.c_str(), static_cast<char *>(nullptr));
                _exit(127);
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
#else
            std::array<char, 4096> modulePath{};
            const auto modulePathLength = readlink("/proc/self/exe", modulePath.data(), modulePath.size() - 1);
            if (modulePathLength > 0)
            {
                modulePath[static_cast<std::size_t>(modulePathLength)] = '\0';
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

        std::filesystem::path FindScriptCoreAssemblyPath(const std::filesystem::path &searchRoot)
        {
            const auto scriptCoreProjectPath = FindScriptCoreProjectPath(searchRoot);
            if (scriptCoreProjectPath.empty())
            {
                return {};
            }

            const auto scriptCoreDirectory = scriptCoreProjectPath.parent_path();
            const std::array<std::filesystem::path, 2> candidates{
                scriptCoreDirectory / "bin" / "Release" / "net8.0" / "PlutoGE.ScriptCore.dll",
                scriptCoreDirectory / "bin" / "Debug" / "net8.0" / "PlutoGE.ScriptCore.dll",
            };

            for (const auto &candidate : candidates)
            {
                if (std::filesystem::exists(candidate))
                {
                    return candidate.lexically_normal();
                }
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

        bool CopyFileIfExists(const std::filesystem::path &sourcePath,
                              const std::filesystem::path &destinationPath,
                              std::string *errorMessage)
        {
            if (!std::filesystem::exists(sourcePath))
            {
                return true;
            }

            std::error_code errorCode;
            std::filesystem::create_directories(destinationPath.parent_path(), errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to create SDK directory: " + destinationPath.parent_path().string();
                }
                return false;
            }

            std::filesystem::copy_file(sourcePath,
                                       destinationPath,
                                       std::filesystem::copy_options::overwrite_existing,
                                       errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to copy SDK file: " + sourcePath.string();
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
                .moveSpeed = camera.moveSpeed,
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
            camera.moveSpeed = glm::clamp(settings.moveSpeed, kEditorCameraMinMoveSpeed, kEditorCameraMaxMoveSpeed);
            camera.speedAdjustment = 1.0f;
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

            auto &window = core::Engine::GetInstance().GetWindow();
            if (window.IsOpen())
            {
                window.EnsureOpenGLContextCurrent(true);
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

                effect->ApplyParameters(parameters);
            }
        }

        void UpdateEditorCamera(EditorShell::EditorViewportCamera &camera,
                                platform::Window &window,
                                GLFWwindow *windowHandle,
                                bool canActivate,
                                float deltaTime,
                                bool &isLookActive,
                                double &lastCursorX,
                                double &lastCursorY,
                                double &restoreCursorX,
                                double &restoreCursorY)
        {
            static bool isPanActive = false;
            static double lastPanCursorX = 0.0;
            static double lastPanCursorY = 0.0;

            if (!windowHandle)
            {
                if (isLookActive)
                {
                    window.SetEditorCursorLocked(false);
                    isLookActive = false;
                }
                isPanActive = false;
                return;
            }

            const bool isRightMouseDown = glfwGetMouseButton(windowHandle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            const bool isMiddleMouseDown = glfwGetMouseButton(windowHandle, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
            const bool isLeftMouseDown = glfwGetMouseButton(windowHandle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool isAltDown = glfwGetKey(windowHandle, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                                   glfwGetKey(windowHandle, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            const bool isShiftDown = glfwGetKey(windowHandle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                     glfwGetKey(windowHandle, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            const bool isTrackpadPanDown = isLeftMouseDown && isAltDown && isShiftDown;
            const bool isTrackpadLookDown = isLeftMouseDown && isAltDown && !isShiftDown;
            const bool isPanDown = isMiddleMouseDown || isTrackpadPanDown;
            const bool isLookDown = isRightMouseDown || isTrackpadLookDown;
            const double scrollDeltaY = window.GetInputState().mouseState.scrollDeltaY;
            if (camera.orthographic && canActivate && scrollDeltaY != 0.0)
            {
                camera.orthographicSize = glm::clamp(camera.orthographicSize *
                                                         std::pow(0.85f, static_cast<float>(scrollDeltaY)),
                                                     0.05f,
                                                     100000.0f);
            }
            if (isPanDown)
            {
                if (isLookActive)
                {
                    window.SetEditorCursorLocked(false);
                    glfwSetCursorPos(windowHandle, restoreCursorX, restoreCursorY);
                    isLookActive = false;
                }

                double cursorX = 0.0;
                double cursorY = 0.0;
                glfwGetCursorPos(windowHandle, &cursorX, &cursorY);
                if (!isPanActive)
                {
                    if (!canActivate)
                    {
                        return;
                    }
                    isPanActive = true;
                    lastPanCursorX = cursorX;
                    lastPanCursorY = cursorY;
                }

                const float deltaX = static_cast<float>(cursorX - lastPanCursorX);
                const float deltaY = static_cast<float>(cursorY - lastPanCursorY);
                lastPanCursorX = cursorX;
                lastPanCursorY = cursorY;

                const glm::mat4 transform = GetEditorCameraTransform(camera);
                const glm::vec3 right = GetTransformRight(transform);
                const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));
                const float panScale = camera.orthographic
                                           ? (2.0f * camera.orthographicSize / 720.0f)
                                           : (camera.moveSpeed * camera.speedAdjustment / 500.0f);
                camera.position += (-right * deltaX + up * deltaY) * panScale;
                return;
            }
            isPanActive = false;

            if (!isLookDown)
            {
                if (isLookActive)
                {
                    window.SetEditorCursorLocked(false);
                    glfwSetCursorPos(windowHandle, restoreCursorX, restoreCursorY);
                    lastCursorX = restoreCursorX;
                    lastCursorY = restoreCursorY;
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
                glfwGetCursorPos(windowHandle, &restoreCursorX, &restoreCursorY);
                window.SetEditorCursorLocked(true);
                lastCursorX = restoreCursorX;
                lastCursorY = restoreCursorY;
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
            if (deltaX != 0.0f || deltaY != 0.0f)
            {
                if (camera.orthographic && camera.hasPerspectivePosition)
                {
                    camera.position = camera.perspectivePosition;
                    camera.hasPerspectivePosition = false;
                }
                camera.orthographic = false;
            }

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

            if (scrollDeltaY != 0.0)
            {
                camera.speedAdjustment = glm::clamp(camera.speedAdjustment * std::pow(kEditorCameraScrollStepFactor, static_cast<float>(scrollDeltaY)),
                                                    kEditorCameraMinSpeedAdjustment,
                                                    kEditorCameraMaxSpeedAdjustment);
            }

            float moveSpeed = glm::clamp(camera.moveSpeed, kEditorCameraMinMoveSpeed, kEditorCameraMaxMoveSpeed) * camera.speedAdjustment;
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
        std::string RunZenityFileDialog(const std::string &arguments)
        {
            std::array<char, 4096> output{};
            std::string result;
            FILE *pipe = popen(("zenity --file-selection " + arguments + " 2>/dev/null").c_str(), "r");
            if (!pipe)
            {
                return {};
            }
            while (fgets(output.data(), static_cast<int>(output.size()), pipe))
            {
                result += output.data();
            }
            if (pclose(pipe) != 0)
            {
                return {};
            }
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            {
                result.pop_back();
            }
            return result;
        }

        std::string ShowOpenFileDialog(const char *)
        {
            return RunZenityFileDialog("");
        }

        std::string ShowSaveFileDialog(const char *, const std::string &initialPath, const char *)
        {
            std::string arguments = "--save --confirm-overwrite";
            if (!initialPath.empty() && initialPath.find('\'') == std::string::npos)
            {
                arguments += " --filename='" + initialPath + "'";
            }
            return RunZenityFileDialog(arguments);
        }
#endif
    }

    EditorShell::~EditorShell() = default;

    void EditorShell::InitializeEditorCamera()
    {
        m_editorCamera = EditorViewportCamera{};
        m_editorCamera.AddPostProcessEffectByType("AutoExposure");
        m_editorCamera.AddPostProcessEffectByType("ToneMapping");
        m_editorCamera.AddPostProcessEffectByType("ColorGrading");
        m_editorCamera.AddPostProcessEffectByType("SceneComposite");
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

    void EditorShell::LoadRecentProjects()
    {
        m_recentProjects.clear();
        std::ifstream input(GetRecentProjectsPath());
        std::string pathText;
        while (m_recentProjects.size() < kMaxRecentProjects && std::getline(input, pathText))
        {
            if (pathText.empty())
                continue;
            std::filesystem::path path(pathText);
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error))
                m_recentProjects.push_back(std::move(path));
        }
    }

    void EditorShell::SaveRecentProjects() const
    {
        const auto settingsDirectory = GetEditorSettingsDirectory();
        std::error_code error;
        std::filesystem::create_directories(settingsDirectory, error);
        if (error)
            return;

        std::ofstream output(GetRecentProjectsPath(), std::ios::out | std::ios::trunc);
        for (const auto &path : m_recentProjects)
            output << path.string() << '\n';
    }

    void EditorShell::AddRecentProject(const std::filesystem::path &manifestPath)
    {
        std::error_code error;
        auto normalizedPath = std::filesystem::absolute(manifestPath, error).lexically_normal();
        if (error)
            normalizedPath = manifestPath.lexically_normal();

        m_recentProjects.erase(
            std::remove(m_recentProjects.begin(), m_recentProjects.end(), normalizedPath),
            m_recentProjects.end());
        m_recentProjects.insert(m_recentProjects.begin(), std::move(normalizedPath));
        if (m_recentProjects.size() > kMaxRecentProjects)
            m_recentProjects.resize(kMaxRecentProjects);
        SaveRecentProjects();
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
            const auto configuredPath = m_project->ResolveAssetReference(manifest.scriptAssembly).lexically_normal();
            const bool hasCollapsedProjectScheme =
                manifest.scriptAssembly.find("project:/") != std::string::npos &&
                !assets::Project::IsProjectAssetReference(manifest.scriptAssembly);
            if (!hasCollapsedProjectScheme && configuredPath.extension() == ".dll")
            {
                return configuredPath;
            }
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
#ifdef _WIN32
        runtimeExecutablePath.replace_extension(".exe");
#else
        runtimeExecutablePath.replace_extension();
#endif
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

        const auto scriptCoreAssemblyPath = FindScriptCoreAssemblyPath(GetProcessDirectory());
        const auto scriptCoreReferencePath = scriptCoreAssemblyPath.empty() ? scriptCoreProjectPath : scriptCoreAssemblyPath;
        const auto scriptCoreReference = MakeRelativeOrAbsoluteGenericPath(scriptCoreReferencePath,
                                                                           scriptProjectPath.parent_path());
        if (scriptCoreReference.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create script project reference path.";
            }
            return false;
        }

        const auto scriptCoreRuntimeDirectoryPath =
            scriptCoreAssemblyPath.empty() ? scriptCoreProjectPath.parent_path() : scriptCoreAssemblyPath.parent_path();
        const auto scriptCoreDirectory = MakeRelativeOrAbsoluteGenericPath(scriptCoreRuntimeDirectoryPath,
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
        scriptProjectContent += "    <EnableDefaultItems>false</EnableDefaultItems>\n";
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
        if (scriptCoreAssemblyPath.empty())
        {
            scriptProjectContent += "    <ProjectReference Include=\"" + scriptCoreReference + "\" />\n";
        }
        else
        {
            // Installed editor SDKs normally live under Program Files and are not
            // writable by the current user. Referencing the packaged assembly keeps
            // MSBuild from trying to create bin/obj folders inside the installation.
            scriptProjectContent += "    <Reference Include=\"PlutoGE.ScriptCore\">\n";
            scriptProjectContent += "      <HintPath>" + scriptCoreReference + "</HintPath>\n";
            scriptProjectContent += "      <Private>true</Private>\n";
            scriptProjectContent += "    </Reference>\n";
        }
        scriptProjectContent += "  </ItemGroup>\n";
        scriptProjectContent += "  <Target Name=\"CopyScriptCoreRuntimeFiles\" AfterTargets=\"Build\">\n";
        scriptProjectContent += "    <ItemGroup>\n";
        if (scriptCoreAssemblyPath.empty())
        {
            scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"" + scriptCoreDirectory + "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.runtimeconfig.json\" />\n";
            scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"" + scriptCoreDirectory + "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.deps.json\" Condition=\"Exists('" + scriptCoreDirectory + "/bin/$(Configuration)/$(TargetFramework)/PlutoGE.ScriptCore.deps.json')\" />\n";
        }
        else
        {
            scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"" + scriptCoreDirectory + "/PlutoGE.ScriptCore.runtimeconfig.json\" />\n";
            scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"" + scriptCoreDirectory + "/PlutoGE.ScriptCore.deps.json\" Condition=\"Exists('" + scriptCoreDirectory + "/PlutoGE.ScriptCore.deps.json')\" />\n";
        }
        scriptProjectContent += "    </ItemGroup>\n";
        scriptProjectContent += "    <Copy SourceFiles=\"@(ScriptCoreRuntimeFiles)\" DestinationFolder=\"$(OutputPath)\" SkipUnchangedFiles=\"true\" Condition=\"@(ScriptCoreRuntimeFiles) != ''\" />\n";
        scriptProjectContent += "  </Target>\n";
        scriptProjectContent += "</Project>\n";

        if (!WriteTextFile(scriptProjectPath, scriptProjectContent, errorMessage))
        {
            return false;
        }

        auto &manifest = m_project->GetManifest();
        // Always canonicalize this value. In particular, passing a project://
        // reference through std::filesystem::path on POSIX collapses it to
        // project:/ and turns it into an invalid relative filesystem path.
        manifest.scriptAssembly = m_project->MakeAssetReference(GetProjectScriptAssemblyOutputPath());

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
            const auto scriptCoreAssemblyPath = FindScriptCoreAssemblyPath(GetProcessDirectory());
            if (!scriptCoreAssemblyPath.empty())
            {
                (void)scriptEngine.LoadAssembly(scriptCoreAssemblyPath);
            }

            if (wasRuntimeRunning)
            {
                m_engine.StartRuntime();
            }

            return true;
        }

        if (!std::filesystem::exists(assemblyPath))
        {
            const auto scriptCoreAssemblyPath = FindScriptCoreAssemblyPath(GetProcessDirectory());
            if (!scriptCoreAssemblyPath.empty() && scriptEngine.LoadAssembly(scriptCoreAssemblyPath))
            {
                if (errorMessage)
                {
                    errorMessage->clear();
                }

                if (wasRuntimeRunning)
                {
                    m_engine.StartRuntime();
                }

                return true;
            }

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

        std::lock_guard lock(m_consoleMessagesMutex);
        m_consoleMessages.push_back(ConsoleMessage{.severity = severity, .text = std::move(message)});
        constexpr std::size_t kMaxConsoleMessages = 1000;
        if (m_consoleMessages.size() > kMaxConsoleMessages)
        {
            m_consoleMessages.erase(m_consoleMessages.begin(), m_consoleMessages.begin() + static_cast<std::ptrdiff_t>(m_consoleMessages.size() - kMaxConsoleMessages));
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

    bool EditorShell::RestoreSceneState(const std::string &state, std::string *errorMessage, bool markDirty)
    {
        auto restoredScene = scene::SceneSerializer::LoadFromString(state, errorMessage);
        if (!restoredScene)
        {
            return false;
        }

        const std::string previousPath = m_scene ? m_scene->GetFilePath() : std::string{};
        restoredScene->SetFilePath(previousPath);
        SetScene(std::move(restoredScene));
        if (markDirty)
        {
            MarkSceneDirty();
        }
        return true;
    }

    bool EditorShell::StartEditorRuntime()
    {
        if (m_engine.IsRuntimeRunning())
        {
            return true;
        }

        std::string errorMessage;
        if (!CaptureSceneState(m_runtimeSceneSnapshot, &errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Failed to snapshot scene before Play." : errorMessage;
            Log(ConsoleSeverity::Error, m_statusMessage);
            return false;
        }

        m_runtimeSceneWasDirty = m_sceneDirty;
        m_runtimeSceneSnapshotPath = m_scene ? m_scene->GetFilePath() : std::string{};
        m_engine.GetWindow().SetCursorLockOverride(false);
        m_engine.StartRuntime();
        m_statusMessage = "Runtime started.";
        return true;
    }

    bool EditorShell::StopEditorRuntime()
    {
        if (!m_engine.IsRuntimeRunning())
        {
            return true;
        }

        m_engine.StopRuntime();
        render::RmlUiRuntime::Get().ResetRuntimeState();
        auto &window = m_engine.GetWindow();
        window.SetCursorLockOverride(false);
        window.SetScriptInputEnabled(false);
        window.SetCursorLocked(false);

        if (!m_runtimeSceneSnapshot.empty())
        {
            std::string errorMessage;
            if (!RestoreSceneState(m_runtimeSceneSnapshot, &errorMessage, false))
            {
                m_statusMessage = errorMessage.empty() ? "Runtime stopped, but failed to restore pre-Play scene state." : errorMessage;
                Log(ConsoleSeverity::Error, m_statusMessage);
                return false;
            }

            m_runtimeSceneSnapshot.clear();
            if (m_scene)
            {
                m_scene->SetFilePath(m_runtimeSceneSnapshotPath);
            }
        }

        m_runtimeSceneSnapshotPath.clear();

        m_sceneDirty = m_runtimeSceneWasDirty;
        UpdateWindowTitle();
        m_statusMessage = "Runtime stopped. Restored pre-Play scene state.";
        return true;
    }

    void EditorShell::HandleRuntimeSceneLoadRequest()
    {
        const auto request = m_engine.ConsumeSceneLoadRequest();
        if (!request)
            return;
        if (!m_project)
        {
            Log(ConsoleSeverity::Error, "Cannot load a scene from script without an open project.");
            return;
        }

        const std::string reference = m_project->FindSceneAssetReference(*request);
        const std::string path = reference.empty() ? std::string{} : m_engine.GetAssetManager().ResolveAssetPath(reference);
        std::string errorMessage;
        auto loadedScene = path.empty() ? nullptr : scene::SceneSerializer::Load(path, &errorMessage);
        if (!loadedScene)
        {
            const std::string detail = errorMessage.empty() ? "scene asset was not found" : errorMessage;
            Log(ConsoleSeverity::Error, "Failed to load scene '" + *request + "': " + detail);
            return;
        }

        SetScene(std::move(loadedScene));
        m_statusMessage = "Runtime loaded scene: " + std::filesystem::path(path).filename().string();
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

        PushSceneHistoryEntry(SceneHistoryEntry{.label = std::move(label), .beforeState = std::move(beforeState), .afterState = std::move(afterState)});
        m_redoStack.clear();
        MarkSceneDirty();
    }

    void EditorShell::PushSceneHistoryEntry(SceneHistoryEntry entry)
    {
        m_undoStack.push_back(std::move(entry));

        constexpr std::size_t kMaxUndoEntries = 80;
        constexpr std::size_t kMaxUndoBytes = 256ull * 1024ull * 1024ull;
        std::size_t retainedBytes = 0;
        for (const auto &historyEntry : m_undoStack)
        {
            retainedBytes += historyEntry.label.size();
            retainedBytes += historyEntry.beforeState.size();
            retainedBytes += historyEntry.afterState.size();
            retainedBytes += historyEntry.retainedBytes;
        }

        while (m_undoStack.size() > 1 &&
               (m_undoStack.size() > kMaxUndoEntries || retainedBytes > kMaxUndoBytes))
        {
            const auto &oldest = m_undoStack.front();
            retainedBytes -= oldest.label.size();
            retainedBytes -= oldest.beforeState.size();
            retainedBytes -= oldest.afterState.size();
            retainedBytes -= oldest.retainedBytes;
            m_undoStack.erase(m_undoStack.begin());
        }
    }

    void EditorShell::PushSceneEditCommand(std::string label,
                                           std::function<bool()> undo,
                                           std::function<bool()> redo,
                                           std::size_t retainedBytes)
    {
        if (!undo || !redo)
            return;
        PushSceneHistoryEntry(SceneHistoryEntry{.label = std::move(label),
                                                .undo = std::move(undo),
                                                .redo = std::move(redo),
                                                .retainedBytes = retainedBytes});
        m_redoStack.clear();
        MarkSceneDirty();
    }

    bool EditorShell::BeginSceneEdit(std::string label)
    {
        if (m_sceneEditInProgress)
        {
            return false;
        }

        std::string errorMessage;
        if (!CaptureSceneState(m_sceneEditBeforeState, &errorMessage))
        {
            m_sceneEditBeforeState.clear();
            Log(ConsoleSeverity::Warning, errorMessage.empty() ? "Started scene edit without undo snapshot." : errorMessage);
            return false;
        }

        m_sceneEditLabel = std::move(label);
        m_sceneEditInProgress = true;
        return true;
    }

    bool EditorShell::EndSceneEdit()
    {
        if (!m_sceneEditInProgress)
        {
            return false;
        }

        const std::string label = std::move(m_sceneEditLabel);
        const std::string beforeState = std::move(m_sceneEditBeforeState);
        m_sceneEditInProgress = false;
        m_sceneEditLabel.clear();
        m_sceneEditBeforeState.clear();

        std::string afterState;
        std::string errorMessage;
        if (beforeState.empty() || !CaptureSceneState(afterState, &errorMessage))
        {
            MarkSceneDirty();
            return false;
        }

        if (beforeState == afterState)
        {
            return false;
        }

        PushSceneHistoryEntry(SceneHistoryEntry{.label = label.empty() ? "Scene Edit" : label, .beforeState = beforeState, .afterState = std::move(afterState)});
        m_redoStack.clear();
        MarkSceneDirty();
        return true;
    }

    void EditorShell::CancelSceneEdit()
    {
        m_sceneEditInProgress = false;
        m_sceneEditLabel.clear();
        m_sceneEditBeforeState.clear();
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
        const bool restored = entry.undo ? entry.undo() : RestoreSceneState(entry.beforeState, &errorMessage);
        if (!restored)
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
        const bool restored = entry.redo ? entry.redo() : RestoreSceneState(entry.afterState, &errorMessage);
        if (!restored)
        {
            Log(ConsoleSeverity::Error, errorMessage.empty() ? "Redo failed." : errorMessage);
            return false;
        }

        Log(ConsoleSeverity::Info, "Redo: " + entry.label);
        m_undoStack.push_back(std::move(entry));
        return true;
    }

    bool EditorShell::CopySelectedEntity()
    {
        if (!m_selectedEntity)
        {
            return false;
        }

        auto clipboardScene = std::make_unique<scene::Scene>();
        auto *copiedRoot = scene::Prefab::DuplicateEntity(*clipboardScene, *m_selectedEntity, nullptr, true);
        if (!copiedRoot)
        {
            return false;
        }

        m_entityClipboardRootId = copiedRoot->GetID();
        m_entityClipboardScene = std::move(clipboardScene);
        Log(ConsoleSeverity::Info, "Copied entity: " + m_selectedEntity->GetName());
        return true;
    }

    bool EditorShell::PasteCopiedEntity()
    {
        if (!m_scene || !HasCopiedEntity())
        {
            return false;
        }

        auto *sourceRoot = m_entityClipboardScene->FindEntityByID(m_entityClipboardRootId);
        if (!sourceRoot)
        {
            return false;
        }

        scene::Entity *createdEntity = nullptr;
        scene::Entity *parent = m_selectedEntity ? m_selectedEntity->GetParent() : nullptr;
        ExecuteSceneEdit("Paste Entity",
                         [&]()
                         {
                             createdEntity = scene::Prefab::DuplicateEntity(*m_scene, *sourceRoot, parent, true);
                             if (createdEntity)
                             {
                                 createdEntity->SetPosition(createdEntity->GetPosition() + glm::vec3(0.0f, 0.0f, 0.0f));
                             }
                         });

        if (createdEntity)
        {
            SetSelectedEntity(createdEntity);
            return true;
        }

        return false;
    }

    bool EditorShell::DuplicateSelectedEntity()
    {
        if (!m_scene || !m_selectedEntity)
        {
            return false;
        }

        auto *sourceEntity = m_selectedEntity;
        auto *parent = sourceEntity->GetParent();
        scene::Entity *createdEntity = nullptr;
        ExecuteSceneEdit("Duplicate Entity",
                         [&]()
                         {
                             createdEntity = scene::Prefab::DuplicateEntity(*m_scene, *sourceEntity, parent, true);
                             if (createdEntity)
                             {
                                 createdEntity->SetName(sourceEntity->GetName() + " Copy");
                                 createdEntity->SetPosition(sourceEntity->GetPosition() + glm::vec3(0.0f, 0.0f, 0.0f));
                             }
                         });

        if (createdEntity)
        {
            SetSelectedEntity(createdEntity);
            return true;
        }

        return false;
    }

    bool EditorShell::DeleteSelectedEntity()
    {
        if (!m_scene || !m_selectedEntity)
        {
            return false;
        }

        auto *entity = m_selectedEntity;
        ExecuteSceneEdit("Delete Entity",
                         [&]()
                         {
                             m_scene->RemoveEntity(entity);
                         });
        SetSelectedEntity(nullptr);
        return true;
    }

    void EditorShell::HandleEditorShortcuts(bool isRuntimeRunning, ProfilerPanel *profilerPanel)
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (io.WantTextInput)
        {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        {
            if (isRuntimeRunning && io.KeyShift)
                StopEditorRuntime();
            else if (!isRuntimeRunning && !io.KeyShift)
                StartEditorRuntime();
            return;
        }
        const bool command = io.KeyCtrl || io.KeySuper;
        if (command && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O))
        {
            if (!m_activeBakeTask && ConfirmContinueWithUnsavedChanges())
            {
                const std::string projectPath = ShowOpenFileDialog(kProjectFileFilter);
                if (!projectPath.empty())
                    LoadProjectFromPath(projectPath);
            }
            return;
        }
        if (command && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            if (profilerPanel)
            {
                profilerPanel->CopyMetricsToClipboard();
            }
            return;
        }
        if (isRuntimeRunning)
        {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            SetSelectedEntity(nullptr);
        }
        if (command && ImGui::IsKeyPressed(ImGuiKey_Z))
        {
            Undo();
        }
        if ((command && ImGui::IsKeyPressed(ImGuiKey_Y)) ||
            (command && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))
        {
            Redo();
        }
        if (command && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            CopySelectedEntity();
        }
        if (command && ImGui::IsKeyPressed(ImGuiKey_V))
        {
            PasteCopiedEntity();
        }
        if (command && ImGui::IsKeyPressed(ImGuiKey_D))
        {
            DuplicateSelectedEntity();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
        {
            DeleteSelectedEntity();
        }
    }

    std::vector<EditorShell::ConsoleMessage> EditorShell::GetConsoleMessages() const
    {
        std::lock_guard lock(m_consoleMessagesMutex);
        return m_consoleMessages;
    }

    void EditorShell::ClearConsoleMessages()
    {
        std::lock_guard lock(m_consoleMessagesMutex);
        m_consoleMessages.clear();
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

    scene::Entity *EditorShell::GetSelectedEntity()
    {
        if (m_selectedEntity && (!m_scene || !m_scene->ContainsEntity(m_selectedEntity)))
        {
            m_selectedEntity = nullptr;
            m_isEditorCameraSelected = false;
        }
        return m_selectedEntity;
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

        m_pendingIblCaptureEntities.clear();
        m_engine.GetWindow().EnsureOpenGLContextCurrent(true);

        // Keep the previous scene alive until Engine::SetScene has stopped its
        // runtime and switched the non-owning scene pointer.
        auto previousScene = std::move(m_scene);
        m_scene = std::move(scene);
        std::string prefabErrorMessage;
        const int updatedPrefabCount = scene::Prefab::UpdateInstances(*m_scene, {}, &prefabErrorMessage);
        if (updatedPrefabCount > 0)
        {
            Log(ConsoleSeverity::Info, "Updated " + std::to_string(updatedPrefabCount) + " prefab instance(s).");
        }
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
        if (!errorMessage.empty())
        {
            m_statusMessage = errorMessage;
            Log(ConsoleSeverity::Warning, errorMessage);
        }
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

    void EditorShell::OpenMeshAsset(std::string meshAssetReference)
    {
        if (meshAssetReference.empty())
        {
            return;
        }

        m_activeMeshAssetReference = std::move(meshAssetReference);
        m_openMeshEditorRequested = true;
        Log(ConsoleSeverity::Info, "Opened mesh: " + m_activeMeshAssetReference);
    }

    void EditorShell::OpenShaderGraphAsset(std::string shaderGraphAssetReference)
    {
        if (shaderGraphAssetReference.empty())
        {
            return;
        }

        m_activeShaderGraphAssetReference = std::move(shaderGraphAssetReference);
        m_openShaderGraphEditorRequested = true;
        Log(ConsoleSeverity::Info, "Opened shader graph: " + m_activeShaderGraphAssetReference);
    }

    void EditorShell::OpenAnimationGraphAsset(std::string animationGraphAssetReference)
    {
        if (animationGraphAssetReference.empty())
        {
            return;
        }

        m_activeAnimationGraphAssetReference = std::move(animationGraphAssetReference);
        m_openAnimationGraphEditorRequested = true;
        Log(ConsoleSeverity::Info, "Opened animation graph: " + m_activeAnimationGraphAssetReference);
    }

    void EditorShell::OpenAnimationClipAsset(std::string animationClipAssetReference)
    {
        if (animationClipAssetReference.empty())
            return;
        m_activeAnimationClipAssetReference = std::move(animationClipAssetReference);
        m_openAnimationClipEditorRequested = true;
        Log(ConsoleSeverity::Info, "Opened animation clip: " + m_activeAnimationClipAssetReference);
    }

    void EditorShell::OpenParticleSystemAsset(std::string particleSystemAssetReference)
    {
        if (particleSystemAssetReference.empty())
        {
            return;
        }

        m_activeParticleSystemAssetReference = std::move(particleSystemAssetReference);
        m_openParticleSystemEditorRequested = true;
        Log(ConsoleSeverity::Info, "Opened particle system: " + m_activeParticleSystemAssetReference);
    }

    void EditorShell::OpenInputMappingAsset(std::string reference)
    {
        if (reference.empty()) return;
        m_activeInputMappingAssetReference = std::move(reference);
        m_openInputMappingEditorRequested = true;
        Log(ConsoleSeverity::Info, "Opened input mapping: " + m_activeInputMappingAssetReference);
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
        m_panelManager.SetEditorFontSize(manifest.editorFontSize);
        m_panelManager.SetEditorFont(manifest.editorFont);
        ApplyProjectEditorCameraSettings(manifest.editorCamera, m_editorCamera);
        if (manifest.editorCameraPostProcessPreset.empty() ||
            !m_editorCamera.SetPostProcessPresetAssetReference(manifest.editorCameraPostProcessPreset))
        {
            ApplyProjectEditorPostProcessEffects(manifest.editorCameraPostProcessEffects, m_editorCamera);
        }
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
        AddRecentProject(m_project->GetManifestPath());
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
        m_project->GetManifest().editorFontSize = m_panelManager.GetEditorFontSize();
        m_project->GetManifest().editorFont = m_panelManager.GetEditorFont();
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
        AddRecentProject(m_project->GetManifestPath());
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
        manifest.editorCameraPostProcessPreset = m_editorCamera.GetPostProcessPresetAssetReference();
        manifest.editorCameraPostProcessEffects = manifest.editorCameraPostProcessPreset.empty()
                                                      ? BuildProjectEditorPostProcessEffects(m_editorCamera)
                                                      : std::vector<assets::ProjectPostProcessEffect>{};
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

        const auto &manifest = m_project->GetManifest();
        if (!manifest.scriptAssembly.empty())
        {
            const auto scriptProjectPath = GetProjectScriptProjectPath();
            const auto scriptSourceDirectory = GetProjectScriptSourceDirectory();
            if (std::filesystem::exists(scriptProjectPath) || std::filesystem::exists(scriptSourceDirectory))
            {
                if (!BuildProjectScripts())
                {
                    m_statusMessage = "Game export stopped because project scripts failed to build. " + m_statusMessage;
                    return false;
                }
            }

            const auto scriptAssemblyPath = ResolveProjectScriptAssemblyPath();
            if (scriptAssemblyPath.empty() || !std::filesystem::exists(scriptAssemblyPath))
            {
                m_statusMessage = "Game export stopped because the configured script assembly was not found: " +
                                  (scriptAssemblyPath.empty() ? manifest.scriptAssembly : scriptAssemblyPath.string());
                return false;
            }
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

        if (!ExportScriptAuthoringSdk(destinationExecutablePath, &errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Built project but failed to export the script SDK." : errorMessage;
            return false;
        }

        m_statusMessage = "Built project: " + std::filesystem::path(destinationExecutablePath).filename().string();
        return true;
    }

    bool EditorShell::ExportScriptAuthoringSdk(const std::filesystem::path &destinationExecutablePath, std::string *errorMessage) const
    {
        if (!m_project)
        {
            if (errorMessage)
            {
                *errorMessage = "No project loaded.";
            }
            return false;
        }

        const auto scriptCoreAssemblyPath = FindScriptCoreAssemblyPath(GetProcessDirectory());
        if (scriptCoreAssemblyPath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Could not locate PlutoGE.ScriptCore.dll for the exported script SDK.";
            }
            return false;
        }

        const auto scriptCoreDirectory = scriptCoreAssemblyPath.parent_path();
        const auto exportDirectory = std::filesystem::absolute(destinationExecutablePath).lexically_normal().parent_path();
        const auto sdkDirectory = exportDirectory / "Sdk";
        const auto sdkReferenceDirectory = sdkDirectory / "PlutoGE.ScriptCore";

        const std::array<std::filesystem::path, 5> sdkFiles{
            scriptCoreDirectory / "PlutoGE.ScriptCore.dll",
            scriptCoreDirectory / "PlutoGE.ScriptCore.xml",
            scriptCoreDirectory / "PlutoGE.ScriptCore.pdb",
            scriptCoreDirectory / "PlutoGE.ScriptCore.deps.json",
            scriptCoreDirectory / "PlutoGE.ScriptCore.runtimeconfig.json",
        };

        for (const auto &sdkFile : sdkFiles)
        {
            if (!CopyFileIfExists(sdkFile, sdkReferenceDirectory / sdkFile.filename(), errorMessage))
            {
                return false;
            }
        }

        const auto executableName = destinationExecutablePath.filename().generic_string();
        const auto manifestName = assets::GetRuntimeManifestPathForExecutable(destinationExecutablePath).filename().generic_string();
        const auto projectRoot = std::filesystem::absolute(m_project->GetRootDirectory()).lexically_normal();
        const auto scriptSourceDirectory = std::filesystem::absolute(GetProjectScriptSourceDirectory()).lexically_normal();
        const auto scriptOutputDirectory = exportDirectory / m_project->GetManifest().assetDirectory / std::filesystem::path(kDefaultProjectManagedDirectory);
        const std::string sourcePattern = MakeRelativeOrAbsoluteGenericPath(scriptSourceDirectory, sdkDirectory) + "/**/*.cs";
        const std::string outputPath = MakeRelativeOrAbsoluteGenericPath(scriptOutputDirectory, sdkDirectory);

        std::string rootNamespace = SanitizeIdentifier(m_project->GetManifest().name);
        if (rootNamespace.empty())
        {
            rootNamespace = "PlutoGEProject";
        }

        std::string propsContent;
        propsContent += "<Project>\n";
        propsContent += "  <ItemGroup>\n";
        propsContent += "    <Reference Include=\"PlutoGE.ScriptCore\">\n";
        propsContent += "      <HintPath>PlutoGE.ScriptCore/PlutoGE.ScriptCore.dll</HintPath>\n";
        propsContent += "      <Private>false</Private>\n";
        propsContent += "    </Reference>\n";
        propsContent += "  </ItemGroup>\n";
        propsContent += "</Project>\n";

        if (!WriteTextFile(sdkDirectory / "PlutoGE.ScriptCore.props", propsContent, errorMessage))
        {
            return false;
        }

        std::string scriptProjectContent;
        scriptProjectContent += "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
        scriptProjectContent += "  <Import Project=\"PlutoGE.ScriptCore.props\" />\n";
        scriptProjectContent += "  <PropertyGroup>\n";
        scriptProjectContent += "    <TargetFramework>net8.0</TargetFramework>\n";
        scriptProjectContent += "    <ImplicitUsings>enable</ImplicitUsings>\n";
        scriptProjectContent += "    <Nullable>enable</Nullable>\n";
        scriptProjectContent += "    <EnableDefaultItems>false</EnableDefaultItems>\n";
        scriptProjectContent += "    <CopyLocalLockFileAssemblies>true</CopyLocalLockFileAssemblies>\n";
        scriptProjectContent += "    <AssemblyName>" + rootNamespace + ".Scripts</AssemblyName>\n";
        scriptProjectContent += "    <RootNamespace>" + rootNamespace + ".Scripts</RootNamespace>\n";
        scriptProjectContent += "    <OutputPath>" + outputPath + "/</OutputPath>\n";
        scriptProjectContent += "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n";
        scriptProjectContent += "    <StartAction>Program</StartAction>\n";
        scriptProjectContent += "    <StartProgram>../" + executableName + "</StartProgram>\n";
        scriptProjectContent += "    <StartArguments>../" + manifestName + "</StartArguments>\n";
        scriptProjectContent += "    <StartWorkingDirectory>..</StartWorkingDirectory>\n";
        scriptProjectContent += "  </PropertyGroup>\n";
        scriptProjectContent += "  <ItemGroup>\n";
        scriptProjectContent += "    <Compile Include=\"" + sourcePattern + "\" />\n";
        scriptProjectContent += "  </ItemGroup>\n";
        scriptProjectContent += "  <Target Name=\"CopyScriptCoreRuntimeFiles\" AfterTargets=\"Build\">\n";
        scriptProjectContent += "    <ItemGroup>\n";
        scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"PlutoGE.ScriptCore/PlutoGE.ScriptCore.dll\" />\n";
        scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"PlutoGE.ScriptCore/PlutoGE.ScriptCore.runtimeconfig.json\" />\n";
        scriptProjectContent += "      <ScriptCoreRuntimeFiles Include=\"PlutoGE.ScriptCore/PlutoGE.ScriptCore.deps.json\" Condition=\"Exists('PlutoGE.ScriptCore/PlutoGE.ScriptCore.deps.json')\" />\n";
        scriptProjectContent += "    </ItemGroup>\n";
        scriptProjectContent += "    <Copy SourceFiles=\"@(ScriptCoreRuntimeFiles)\" DestinationFolder=\"$(OutputPath)\" SkipUnchangedFiles=\"true\" />\n";
        scriptProjectContent += "  </Target>\n";
        scriptProjectContent += "</Project>\n";

        if (!WriteTextFile(sdkDirectory / (rootNamespace + ".Scripts.csproj"), scriptProjectContent, errorMessage))
        {
            return false;
        }

        std::string readmeContent;
        readmeContent += "# PlutoGE Script SDK\n\n";
        readmeContent += "Open `" + rootNamespace + ".Scripts.csproj` in Visual Studio to edit project scripts against this exported engine build.\n";
        readmeContent += "The project builds into `../" + m_project->GetManifest().assetDirectory + "/" + std::string(kDefaultProjectManagedDirectory) + "` and launches `../" + executableName + "` for debugging.\n";
        readmeContent += "Use Visual Studio's Debug > Attach to Process command to attach to a running `" + executableName + "` process when you prefer manual attach.\n";
        readmeContent += "\nSource project root at export time: `" + projectRoot.generic_string() + "`\n";

        return WriteTextFile(sdkDirectory / "README.md", readmeContent, errorMessage);
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

    bool EditorShell::Initialize()
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
        config.isEditorHost = true;
        if (!m_engine.Initialize(config))
        {
            std::cerr << "Failed to initialize Engine in EditorShell" << std::endl;
            return false;
        }

        scripting::SetScriptLogSink(
            [this](scripting::ScriptLogSeverity severity, std::string_view message)
            {
                ConsoleSeverity consoleSeverity = ConsoleSeverity::Info;
                switch (severity)
                {
                case scripting::ScriptLogSeverity::Warning:
                    consoleSeverity = ConsoleSeverity::Warning;
                    break;
                case scripting::ScriptLogSeverity::Error:
                    consoleSeverity = ConsoleSeverity::Error;
                    break;
                case scripting::ScriptLogSeverity::Info:
                default:
                    consoleSeverity = ConsoleSeverity::Info;
                    break;
                }

                Log(consoleSeverity, std::string(message));
            });

        InitializeEditorCamera();
        LoadRecentProjects();
        ApplyProjectContext();
        SetScene(CreateEmptyScene());
        m_statusMessage = "Ready";
        UpdateWindowTitle();

        if (!m_panelManager.InitializeImGui(&m_engine.GetWindow()))
        {
            m_editorCamera.postProcessEffects.clear();
            m_scene.reset();
            m_engine.Shutdown();
            return false;
        }

        return true;
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
        viewportConfig.graphicsApi = m_project ? m_project->GetManifest().graphicsApi
                                               : render::rhi::GraphicsApi::OpenGL;
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
        if (m_project && m_project->GetManifest().runtimeUpscaler == assets::RuntimeUpscalerMode::Spatial)
        {
            viewportConfig2.initialRenderScale = m_project->GetManifest().runtimeRenderScale;
            viewportConfig2.initialUpscaleSharpness = m_project->GetManifest().runtimeUpscaleSharpness;
        }
        auto viewportPanel2 = new ViewportPanel(viewportConfig2);
        viewportPanel2->Initialize();
        m_panelManager.AddPanel(viewportPanel2);

        auto inspectorPanel = new InspectorPanel(PanelConfig{"Inspector"});
        inspectorPanel->Initialize();
        m_panelManager.AddPanel(inspectorPanel);

        auto canvasEditorPanel = new CanvasEditorPanel(PanelConfig{"Canvas Editor", true});
        canvasEditorPanel->Initialize();
        m_panelManager.AddPanel(canvasEditorPanel);

        auto contentBrowserPanel = new ContentBrowserPanel(PanelConfig{"Content Browser"});
        contentBrowserPanel->Initialize();
        m_panelManager.AddPanel(contentBrowserPanel);

        auto consolePanel = new ConsolePanel(PanelConfig{"Console"});
        consolePanel->Initialize();
        m_panelManager.AddPanel(consolePanel);

        auto materialEditorPanel = new MaterialEditorPanel(PanelConfig{"Material Editor", false});
        materialEditorPanel->Initialize();
        m_panelManager.AddPanel(materialEditorPanel);

        auto meshEditorPanel = new MeshEditorPanel(PanelConfig{"Mesh Editor", false});
        meshEditorPanel->Initialize();
        m_panelManager.AddPanel(meshEditorPanel);

        auto shaderGraphEditorPanel = new ShaderGraphEditorPanel(PanelConfig{"Shader Graph Editor", false});
        shaderGraphEditorPanel->Initialize();
        m_panelManager.AddPanel(shaderGraphEditorPanel);

        auto animationGraphEditorPanel = new AnimationGraphEditorPanel(PanelConfig{"Animation Graph Editor", false});
        animationGraphEditorPanel->Initialize();
        m_panelManager.AddPanel(animationGraphEditorPanel);

        auto animationClipEditorPanel = new AnimationClipEditorPanel(PanelConfig{"Animation Clip Editor", false});
        animationClipEditorPanel->Initialize();
        m_panelManager.AddPanel(animationClipEditorPanel);

        auto particleSystemEditorPanel = new ParticleSystemEditorPanel(PanelConfig{"Particle System Editor", false});
        particleSystemEditorPanel->Initialize();
        m_panelManager.AddPanel(particleSystemEditorPanel);

        auto inputMappingEditorPanel = new InputMappingEditorPanel(PanelConfig{"Input Mapping Editor", false});
        inputMappingEditorPanel->Initialize();
        m_panelManager.AddPanel(inputMappingEditorPanel);

        auto profilerPanel = new ProfilerPanel(PanelConfig{"Profiler"}, &m_profiler, &m_panelManager, &renderer);
        profilerPanel->Initialize();
        m_panelManager.AddPanel(profilerPanel);

        auto *renderTarget2 = viewportPanel2->GetRenderTarget();
        auto *windowHandle = static_cast<GLFWwindow *>(window.GetWindow());
        bool isEditorCameraLookActive = false;
        bool forceEditorCursorVisible = false;
        bool cursorOverrideShortcutWasDown = false;
        double lastEditorCameraCursorX = 0.0;
        double lastEditorCameraCursorY = 0.0;
        double restoreEditorCameraCursorX = 0.0;
        double restoreEditorCameraCursorY = 0.0;
        std::array<char, 256> projectNameBuffer{};
        std::array<char, 256> projectWindowTitleBuffer{};
        std::array<char, 512> projectScriptAssemblyBuffer{};
        int projectWindowWidth = 1280;
        int projectWindowHeight = 720;
        bool projectVSyncEnabled = true;
        assets::RuntimeUpscalerMode projectRuntimeUpscaler = assets::RuntimeUpscalerMode::None;
        float projectRuntimeRenderScale = 1.0f;
        float projectRuntimeUpscaleSharpness = 0.25f;
        float projectEditorFontSize = m_panelManager.GetEditorFontSize();
        std::string projectEditorFont = m_panelManager.GetEditorFont();
        bool shouldOpenProjectSettingsPopup = false;
        bool shouldOpenBakeSceneCustomPopup = false;

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
            projectRuntimeUpscaler = manifest.runtimeUpscaler;
            projectRuntimeRenderScale = manifest.runtimeRenderScale;
            projectRuntimeUpscaleSharpness = manifest.runtimeUpscaleSharpness;
            projectEditorFontSize = manifest.editorFontSize;
            projectEditorFont = manifest.editorFont;
        };

        bool editorVSyncEnabled = m_project ? m_project->GetManifest().vSyncEnabled : false;
        renderer.SetVSyncEnabled(editorVSyncEnabled);

        while (!window.ShouldClose())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            deltaTime = currentTime - lastTime;
            const float deltaSeconds = deltaTime.count();
            EditorFrameTimingStats frameTimingStats{};
            const auto profilingBeginStart = std::chrono::high_resolution_clock::now();
            renderer.BeginProfilingFrame();
            const auto profilingBeginEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.profilingBeginMs = std::chrono::duration<float, std::milli>(profilingBeginEnd - profilingBeginStart).count();

            const bool isRuntimeRunning = m_engine.IsRuntimeRunning();
            const bool cursorOverrideShortcutDown = isRuntimeRunning &&
                                                    (glfwGetKey(windowHandle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                                     glfwGetKey(windowHandle, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) &&
                                                    glfwGetKey(windowHandle, GLFW_KEY_F1) == GLFW_PRESS;
            if (cursorOverrideShortcutDown && !cursorOverrideShortcutWasDown)
            {
                forceEditorCursorVisible = true;
                m_statusMessage = "Cursor lock overridden. Disable it from Runtime > Force Show Cursor.";
            }
            cursorOverrideShortcutWasDown = cursorOverrideShortcutDown;
            if (!isRuntimeRunning)
            {
                forceEditorCursorVisible = false;
            }
            window.SetCursorLockOverride(forceEditorCursorVisible);
            if (!isRuntimeRunning)
            {
                window.SetCursorLocked(false);
            }

            viewportPanel->SetPanelControlsEnabled(true);
            viewportPanel2->SetPanelControlsEnabled(!isRuntimeRunning);

            const auto renderTargetWidth = renderTarget->GetWidth();
            const auto renderTargetHeight = renderTarget->GetHeight();
            const auto renderTarget2Width = renderTarget2->GetWidth();
            const auto renderTarget2Height = renderTarget2->GetHeight();
            UpdateEditorCamera(m_editorCamera,
                               window,
                               windowHandle,
                               viewportPanel->IsViewportHovered(),
                               deltaSeconds,
                               isEditorCameraLookActive,
                               lastEditorCameraCursorX,
                               lastEditorCameraCursorY,
                               restoreEditorCameraCursorX,
                               restoreEditorCameraCursorY);
            viewportPanel->SetEditorMovementEnabled(isEditorCameraLookActive);
            auto shouldEnableRuntimeInput = [&]()
            {
                return m_engine.IsRuntimeRunning() &&
                       !isEditorCameraLookActive &&
                       (window.IsCursorLocked() ||
                        window.IsCursorLockRequested() ||
                        viewportPanel2->IsViewportFocused() ||
                        viewportPanel2->IsViewportHovered());
            };

            window.SetScriptInputEnabled(shouldEnableRuntimeInput());

            auto *cameraComponent2 = FindFirstSceneCamera(m_scene.get());
            const bool shouldRenderViewport1 = viewportPanel->ShouldRenderFrame();
            bool shouldRenderViewport2 = viewportPanel2->ShouldRenderFrame() && IsCameraActiveInScene(m_scene.get(), cameraComponent2);
            render::CameraData editorCameraData{};
            bool hasEditorCameraData = false;
            render::CameraData gameCameraData{};
            bool hasGameCameraData = false;

            if (shouldRenderViewport1)
            {
                const glm::mat4 editorCameraTransform = GetEditorCameraTransform(m_editorCamera);
                editorCameraData = m_editorCamera.camera.GetCameraDataForTransform(editorCameraTransform,
                                                                                   renderTargetWidth,
                                                                                   renderTargetHeight);
                if (m_editorCamera.orthographic)
                {
                    const int safeRenderTargetHeight = renderTargetHeight > 1 ? renderTargetHeight : 1;
                    const float aspect = static_cast<float>(renderTargetWidth) / static_cast<float>(safeRenderTargetHeight);
                    const float halfHeight = m_editorCamera.orthographicSize > 0.01f ? m_editorCamera.orthographicSize : 0.01f;
                    editorCameraData.projection = glm::ortho(-halfHeight * aspect,
                                                             halfHeight * aspect,
                                                             -halfHeight,
                                                             halfHeight,
                                                             m_editorCamera.camera.GetFarPlane(),
                                                             m_editorCamera.camera.GetNearPlane());
                }
                hasEditorCameraData = true;
                viewportPanel->SetEditorCameraData(editorCameraData);
            }
            else
            {
                viewportPanel->ClearEditorCameraData();
            }

            if (shouldRenderViewport2 && cameraComponent2)
            {
                gameCameraData = cameraComponent2->GetCameraData(renderTarget2Width, renderTarget2Height);
                hasGameCameraData = true;
            }

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
            frameTimingStats.editorSetupMs = std::chrono::duration<float, std::milli>(sceneUpdateStart - profilingBeginEnd).count();
            if (!isBakeRunning)
            {
                m_engine.UpdateAsyncMeshImports();
                if (m_scene)
                {
                    if (!isRuntimeRunning)
                    {
                        std::vector<render::CameraData> submissionCullingCameras;
                        if (hasEditorCameraData)
                        {
                            submissionCullingCameras.push_back(editorCameraData);
                        }
                        if (hasGameCameraData)
                        {
                            submissionCullingCameras.push_back(gameCameraData);
                        }
                        renderer.SetSubmissionCullingCameras(submissionCullingCameras);
                    }
                    else
                    {
                        renderer.ClearSubmissionCullingCameras();

                        const glm::vec2 viewportMin = viewportPanel2->GetViewportMin();
                        const glm::vec2 viewportSize = viewportPanel2->GetViewportSize();
                        auto *gameRenderTarget = viewportPanel2->GetRenderTarget();
                        const bool hasViewport = gameRenderTarget && viewportSize.x > 0.0f && viewportSize.y > 0.0f;
                        const ImVec2 mouse = ImGui::GetIO().MousePos;
                        const glm::vec2 normalizedMouse = hasViewport
                                                              ? glm::vec2((mouse.x - viewportMin.x) / viewportSize.x,
                                                                          (mouse.y - viewportMin.y) / viewportSize.y)
                                                              : glm::vec2(-1.0f);
                        const bool pointerInside = hasViewport && viewportPanel2->IsViewportHovered() &&
                                                   normalizedMouse.x >= 0.0f && normalizedMouse.x <= 1.0f &&
                                                   normalizedMouse.y >= 0.0f && normalizedMouse.y <= 1.0f;
                        const glm::vec2 canvasSize = hasViewport
                                                         ? glm::vec2(static_cast<float>(gameRenderTarget->GetWidth()),
                                                                     static_cast<float>(gameRenderTarget->GetHeight()))
                                                         : glm::vec2(0.0f);
                        const glm::vec2 canvasMouse(normalizedMouse.x * canvasSize.x,
                                                    (1.0f - normalizedMouse.y) * canvasSize.y);
                        m_scene->SetRuntimeUIInputOverride(canvasSize, canvasMouse, pointerInside);
                    }
                    m_scene->Update(deltaTime.count());
                    const auto &sceneTimingStats = m_scene->GetUpdateTimingStats();
                    frameTimingStats.scenePreparationMs = sceneTimingStats.preparationMs;
                    frameTimingStats.sceneRuntimeUiMs = sceneTimingStats.runtimeUiMs;
                    frameTimingStats.sceneComponentsMs = sceneTimingStats.componentsMs;
                    frameTimingStats.sceneLateScriptsMs = sceneTimingStats.lateScriptsMs;
                    frameTimingStats.sceneAudioMs = sceneTimingStats.audioMs;
                    frameTimingStats.componentTimings = sceneTimingStats.componentTimings;
                    frameTimingStats.animationTimings = sceneTimingStats.animationTimings;
                    frameTimingStats.scriptUpdateTimings = sceneTimingStats.scriptUpdateTimings;
                    frameTimingStats.scriptLateUpdateTimings = sceneTimingStats.scriptLateUpdateTimings;
                    frameTimingStats.sceneRenderSubmissionMs = sceneTimingStats.renderSubmissionMs;
                    frameTimingStats.sceneMeshSubmissionMs = sceneTimingStats.meshSubmissionMs;
                    frameTimingStats.sceneTerrainSubmissionMs = sceneTimingStats.terrainSubmissionMs;
                    frameTimingStats.sceneFoliageSubmissionMs = sceneTimingStats.foliageSubmissionMs;
                    frameTimingStats.scenePhysicsMs = sceneTimingStats.physicsMs;
                }
                if (isRuntimeRunning)
                {
                    if (m_engine.ConsumeApplicationQuitRequest())
                    {
                        StopEditorRuntime();
                    }
                    else
                    {
                        HandleRuntimeSceneLoadRequest();
                    }
                }
            }
            const auto sceneUpdateEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.sceneUpdateMs = std::chrono::duration<float, std::milli>(sceneUpdateEnd - sceneUpdateStart).count();

            // Scripts may destroy the active camera or replace the entire scene
            // during Update. Any component pointer captured before Update is stale.
            cameraComponent2 = FindFirstSceneCamera(m_scene.get());
            shouldRenderViewport2 = viewportPanel2->ShouldRenderFrame() &&
                                    IsCameraActiveInScene(m_scene.get(), cameraComponent2);

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

                    iblCaptureComponent->DiscardCaptureResult();
                    m_scene->ClearIblCaptureVolumes();

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

                    const bool storedCapturePixels = iblCaptureComponent->StoreCapturePixelsFromTexture();
                    if (storedCapturePixels)
                    {
                        iblCaptureComponent->ClearDirty();
                    }
                    m_scene->AddIblCaptureVolume(iblCaptureComponent->BuildCaptureVolume());
                    m_statusMessage = storedCapturePixels ? "IBL capture complete." : "IBL capture complete, but could not store pixels.";
                }
            }

            const auto viewportRenderStart = std::chrono::high_resolution_clock::now();
            if (shouldRenderViewport1)
            {
                auto *sceneRenderTarget = viewportPanel->GetSceneRenderTarget();
                ++frameTimingStats.renderedViewportCount;
                frameTimingStats.editorViewportWidth = renderTargetWidth;
                frameTimingStats.editorViewportHeight = renderTargetHeight;
                frameTimingStats.renderedViewportPixels +=
                    static_cast<std::uint64_t>((std::max)(renderTargetWidth, 0)) *
                    static_cast<std::uint64_t>((std::max)(renderTargetHeight, 0));
                std::vector<render::IPostProcessEffect *> editorPostProcessEffects;
                editorPostProcessEffects.reserve(m_editorCamera.GetPostProcessEffects().size());
                for (const auto &effect : m_editorCamera.GetPostProcessEffects())
                {
                    editorPostProcessEffects.push_back(effect.get());
                }

                renderer.RenderFrame(editorCameraData,
                                     sceneRenderTarget,
                                     m_scene ? m_scene->GetLights() : std::vector<scene::Light *>{},
                                     &editorPostProcessEffects,
                                     m_scene.get(),
                                     viewportPanel->IsGridVisible(),
                                     true);
                viewportPanel->RenderRhiFrame(editorCameraData, renderer.GetVisibleRenderCommands());
                render::CameraData renderedEditorCameraData{};
                if (renderer.GetLastUnjitteredCameraData(sceneRenderTarget, renderedEditorCameraData))
                {
                    viewportPanel->SetEditorCameraData(renderedEditorCameraData);
                }
                viewportPanel->PresentSceneRenderTarget();
            }
            else
            {
                viewportPanel->ClearFrame();
                viewportPanel->ClearEditorCameraData();
            }

            if (shouldRenderViewport2 && cameraComponent2)
            {
                ++frameTimingStats.renderedViewportCount;
                frameTimingStats.gameViewportWidth = renderTarget2Width;
                frameTimingStats.gameViewportHeight = renderTarget2Height;
                frameTimingStats.renderedViewportPixels +=
                    static_cast<std::uint64_t>((std::max)(renderTarget2Width, 0)) *
                    static_cast<std::uint64_t>((std::max)(renderTarget2Height, 0));
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
            const auto editorUiStart = beginFrameEnd;

            m_panelManager.BeginPanelUpdate();
            const auto editorChromeStart = std::chrono::high_resolution_clock::now();

            HandleEditorShortcuts(isRuntimeRunning, profilerPanel);

            auto sanitizeBakeSettings = [](scene::SceneBakeSettings &settings)
            {
                settings.lightmapResolution = (std::max)(settings.lightmapResolution, 4);
                settings.lightmapTileSize = (std::max)(settings.lightmapTileSize, 1);
                settings.directShadowSampleCount = std::clamp(settings.directShadowSampleCount, 1, 32);
                settings.indirectBounceSampleCount = (std::max)(settings.indirectBounceSampleCount, 0);
                settings.indirectBounceCount = std::clamp(settings.indirectBounceCount, 1, 8);
                settings.indirectDenoisePassCount = std::clamp(settings.indirectDenoisePassCount, 0, 4);
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
                if (ConsumeMeshEditorOpenRequest())
                {
                    meshEditorPanel->SetOpen(true);
                }
                if (ConsumeShaderGraphEditorOpenRequest())
                {
                    shaderGraphEditorPanel->SetOpen(true);
                }
                if (ConsumeAnimationGraphEditorOpenRequest())
                {
                    animationGraphEditorPanel->SetOpen(true);
                }
                if (ConsumeAnimationClipEditorOpenRequest())
                    animationClipEditorPanel->SetOpen(true);
                if (ConsumeParticleSystemEditorOpenRequest())
                {
                    particleSystemEditorPanel->SetOpen(true);
                }
                if (ConsumeInputMappingEditorOpenRequest())
                    inputMappingEditorPanel->SetOpen(true);

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
                    if (ImGui::MenuItem("Open Project...", "Ctrl+O"))
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
                    std::filesystem::path recentProjectToOpen;
                    if (ImGui::BeginMenu("Open Recent", !m_recentProjects.empty()))
                    {
                        for (const auto &recentProject : m_recentProjects)
                        {
                            const std::string label = recentProject.filename().string() + "##" + recentProject.string();
                            if (ImGui::MenuItem(label.c_str()))
                                recentProjectToOpen = recentProject;
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", recentProject.string().c_str());
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Clear Recent Projects"))
                        {
                            m_recentProjects.clear();
                            SaveRecentProjects();
                        }
                        ImGui::EndMenu();
                    }
                    if (!recentProjectToOpen.empty() && ConfirmContinueWithUnsavedChanges())
                        LoadProjectFromPath(recentProjectToOpen);
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
                                                              ? std::string(kRuntimeExecutableName)
                                                              : GetDefaultExportExecutablePath().string();
                        const std::string exportPath = ShowSaveFileDialog(kExecutableFileFilter, suggestedPath,
#ifdef _WIN32
                                                                          "exe"
#else
                                                                          nullptr
#endif
                        );
                        if (!exportPath.empty())
                        {
                            BuildProjectToPath(exportPath);
                        }
                    }
                    if (ImGui::MenuItem("Build and Run Project...", nullptr, false, m_project != nullptr))
                    {
                        const std::string suggestedPath = GetDefaultExportExecutablePath().empty()
                                                              ? std::string(kRuntimeExecutableName)
                                                              : GetDefaultExportExecutablePath().string();
                        const std::string exportPath = ShowSaveFileDialog(kExecutableFileFilter, suggestedPath,
#ifdef _WIN32
                                                                          "exe"
#else
                                                                          nullptr
#endif
                        );
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
                    if (ImGui::MenuItem("Bake Scene High (512px)"))
                    {
                        runBake(scene::SceneBakeSettings::HighQuality());
                    }
                    if (ImGui::MenuItem("Bake Scene Ultra (1024px)"))
                    {
                        runBake(scene::SceneBakeSettings::Ultra());
                    }
                    if (ImGui::MenuItem("Bake Scene Custom..."))
                    {
                        shouldOpenBakeSceneCustomPopup = true;
                    }
                    if (ImGui::MenuItem("Clear Baked Lighting", nullptr, false, m_scene != nullptr))
                    {
                        const bool hadProbeVolume = m_scene->HasBakedProbeVolume();
                        const std::size_t clearedLightmaps = m_scene->ClearBakedLighting();
                        MarkSceneDirty();
                        m_statusMessage = "Cleared " + std::to_string(clearedLightmaps) +
                                          " baked lightmap(s)" +
                                          (hadProbeVolume ? " and the baked probe volume." : ".");
                        Log(ConsoleSeverity::Info, m_statusMessage);
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
                    if (ImGui::MenuItem("Mesh Editor", NULL, meshEditorPanel->IsOpen()))
                    {
                        meshEditorPanel->SetOpen(!meshEditorPanel->IsOpen());
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
                    if (ImGui::MenuItem("Play", "F5"))
                    {
                        if (StartEditorRuntime())
                        {
                            forceEditorCursorVisible = false;
                            window.SetCursorLockOverride(false);
                            window.SetScriptInputEnabled(shouldEnableRuntimeInput());
                        }
                    }
                    ImGui::EndDisabled();

                    if (ImGui::BeginMenu("Simulation Speed", canRunRuntime))
                    {
                        constexpr std::array<float, 6> timeScales{0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
                        constexpr std::array<const char *, 6> timeScaleLabels{"Paused", "0.25x", "0.5x", "1x", "2x", "4x"};
                        for (std::size_t index = 0; index < timeScales.size(); ++index)
                        {
                            const bool selected = std::abs(m_scene->GetTimeScale() - timeScales[index]) < 0.001f;
                            if (ImGui::MenuItem(timeScaleLabels[index], nullptr, selected))
                            {
                                m_scene->SetTimeScale(timeScales[index]);
                            }
                        }
                        ImGui::EndMenu();
                    }

                    ImGui::BeginDisabled(!m_engine.IsRuntimeRunning());
                    if (ImGui::MenuItem("Stop", "Shift+F5"))
                    {
                        StopEditorRuntime();
                        forceEditorCursorVisible = false;
                        window.SetCursorLockOverride(false);
                        window.SetScriptInputEnabled(false);
                        window.SetCursorLocked(false);
                    }
                    ImGui::EndDisabled();

                    ImGui::BeginDisabled(!m_engine.IsRuntimeRunning());
                    if (ImGui::MenuItem("Force Show Cursor", "Shift+F1", forceEditorCursorVisible))
                    {
                        forceEditorCursorVisible = !forceEditorCursorVisible;
                        window.SetCursorLockOverride(forceEditorCursorVisible);
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
                    ImGui::Checkbox("VSync", &projectVSyncEnabled);
                    const char *runtimeUpscalerLabel = projectRuntimeUpscaler == assets::RuntimeUpscalerMode::Spatial
                                                          ? "Spatial"
                                                          : "None";
                    if (ImGui::BeginCombo("Runtime Upscaler", runtimeUpscalerLabel))
                    {
                        constexpr std::array<const char *, 2> upscalerLabels = {"None", "Spatial"};
                        for (int modeIndex = 0; modeIndex < static_cast<int>(upscalerLabels.size()); ++modeIndex)
                        {
                            const auto mode = static_cast<assets::RuntimeUpscalerMode>(modeIndex);
                            const bool selected = projectRuntimeUpscaler == mode;
                            if (ImGui::Selectable(upscalerLabels[modeIndex], selected))
                                projectRuntimeUpscaler = mode;
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::BeginDisabled(projectRuntimeUpscaler == assets::RuntimeUpscalerMode::None);
                    ImGui::SliderFloat("Runtime Render Scale", &projectRuntimeRenderScale, 0.5f, 1.0f, "%.2fx");
                    ImGui::SliderFloat("Runtime Sharpness", &projectRuntimeUpscaleSharpness, 0.0f, 1.0f, "%.2f");
                    ImGui::EndDisabled();
                    if (ImGui::SliderFloat("Editor Font Size", &projectEditorFontSize, 10.0f, 24.0f, "%.1f px"))
                    {
                        m_panelManager.SetEditorFontSize(projectEditorFontSize);
                    }
                    constexpr std::array<const char *, 3> editorFonts = {"Martian Mono", "Georama", "ImGui Default"};
                    if (ImGui::BeginCombo("Editor Font", projectEditorFont.c_str()))
                    {
                        for (const char *fontName : editorFonts)
                        {
                            const bool selected = projectEditorFont == fontName;
                            if (ImGui::Selectable(fontName, selected))
                            {
                                projectEditorFont = fontName;
                                m_panelManager.SetEditorFont(projectEditorFont);
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
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
                        manifest.runtimeUpscaler = projectRuntimeUpscaler;
                        manifest.runtimeRenderScale = std::clamp(projectRuntimeRenderScale, 0.5f, 1.0f);
                        manifest.runtimeUpscaleSharpness = std::clamp(projectRuntimeUpscaleSharpness, 0.0f, 1.0f);
                        manifest.editorFontSize = std::clamp(projectEditorFontSize, 10.0f, 24.0f);
                        manifest.editorFont = projectEditorFont;
                        const std::string configuredScriptAssembly = projectScriptAssemblyBuffer.data();
                        manifest.scriptAssembly = configuredScriptAssembly.empty()
                                                      ? std::string{}
                                                      : (assets::Project::IsProjectAssetReference(configuredScriptAssembly) ||
                                                                 assets::Project::IsEngineAssetReference(configuredScriptAssembly)
                                                             ? configuredScriptAssembly
                                                             : m_project->MakeAssetReference(configuredScriptAssembly));

                        if (SaveProjectToDisk())
                        {
                            std::string scriptErrorMessage;
                            ReloadProjectScriptAssembly(&scriptErrorMessage);
                            if (!scriptErrorMessage.empty())
                            {
                                m_statusMessage = scriptErrorMessage;
                            }
                            editorVSyncEnabled = manifest.vSyncEnabled;
                            renderer.SetVSyncEnabled(editorVSyncEnabled);
                            m_panelManager.SetEditorFontSize(manifest.editorFontSize);
                            m_panelManager.SetEditorFont(manifest.editorFont);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        m_panelManager.SetEditorFontSize(manifest.editorFontSize);
                        m_panelManager.SetEditorFont(manifest.editorFont);
                        projectEditorFontSize = manifest.editorFontSize;
                        projectEditorFont = manifest.editorFont;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
            }

            if (shouldOpenBakeSceneCustomPopup)
            {
                ImGui::OpenPopup("Bake Scene Custom");
                shouldOpenBakeSceneCustomPopup = false;
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
                ImGui::SameLine();
                if (ImGui::Button("High 512"))
                {
                    m_customBakeSettings = scene::SceneBakeSettings::HighQuality();
                }
                ImGui::SameLine();
                if (ImGui::Button("Ultra 1024"))
                {
                    m_customBakeSettings = scene::SceneBakeSettings::Ultra();
                }

                ImGui::Separator();
                ImGui::InputInt("Lightmap Resolution", &m_customBakeSettings.lightmapResolution);
                ImGui::InputInt("Lightmap Tile Size", &m_customBakeSettings.lightmapTileSize);
                ImGui::InputInt("Direct Shadow Samples", &m_customBakeSettings.directShadowSampleCount);
                ImGui::Checkbox("Bake Indirect Bounce", &m_customBakeSettings.bakeIndirectBounce);
                ImGui::InputInt("Indirect Samples", &m_customBakeSettings.indirectBounceSampleCount);
                ImGui::InputInt("GI Bounce Count", &m_customBakeSettings.indirectBounceCount);
                ImGui::InputInt("GI Denoise Passes", &m_customBakeSettings.indirectDenoisePassCount);
                ImGui::SliderFloat("Lightmap Bounce Strength", &m_customBakeSettings.lightmapBounceStrength, 0.0f, 4.0f, "%.2f");
                ImGui::Checkbox("Use GPU Bake", &m_customBakeSettings.useGpu);
                if (m_customBakeSettings.useGpu)
                {
                    ImGui::TextDisabled("GPU traces direct light and GI for base-colour scenes; textured GI uses the accurate CPU fallback.");
                }
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

            // Keep the editor responsive while a bake runs. Prepared bake data
            // retains pointers to scene components until finalization, so block
            // panels that can mutate the scene without disabling the entire
            // ImGui interface (which made the application appear frozen).
            const bool allowSceneInteraction = !isBakeRunning;
            viewportPanel->SetInteractionEnabled(allowSceneInteraction);
            viewportPanel->SetVisualAlpha(allowSceneInteraction ? 1.0f : 0.65f);
            sceneHierarchyPanel->SetInteractionEnabled(allowSceneInteraction);
            sceneHierarchyPanel->SetVisualAlpha(allowSceneInteraction ? 1.0f : 0.65f);
            inspectorPanel->SetInteractionEnabled(allowSceneInteraction);
            inspectorPanel->SetVisualAlpha(allowSceneInteraction ? 1.0f : 0.65f);
            profilerPanel->SetInteractionEnabled(true);
            profilerPanel->SetVisualAlpha(1.0f);
            viewportPanel2->SetInteractionEnabled(allowSceneInteraction);
            viewportPanel2->SetVisualAlpha(allowSceneInteraction ? 1.0f : 0.65f);

            const auto editorChromeEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.editorChromeMs = std::chrono::duration<float, std::milli>(editorChromeEnd - editorChromeStart).count();
            m_panelManager.UpdatePanels();

            window.SetScriptInputEnabled(shouldEnableRuntimeInput());

            m_panelManager.EndPanelUpdate();
            const auto editorUiEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.editorUiMs = std::chrono::duration<float, std::milli>(editorUiEnd - editorUiStart).count();

            const auto presentStart = std::chrono::high_resolution_clock::now();
            frameTimingStats.vSyncEnabled = renderer.IsVSyncEnabled();
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
            window.SetEditorCursorLocked(false);
            glfwSetCursorPos(windowHandle, restoreEditorCameraCursorX, restoreEditorCameraCursorY);
        }
        window.SetCursorLockOverride(false);
    }

    void EditorShell::Shutdown()
    {
        if (m_activeBakeTask)
        {
            m_activeBakeTask->Cancel();
            m_activeBakeTask.reset();
        }
        m_selectedEntity = nullptr;
        m_engine.SetScene(nullptr);
        m_engine.GetWindow().EnsureOpenGLContextCurrent(true);
        m_scene.reset();
        m_project.reset();
        m_editorCamera.postProcessEffects.clear();
        m_engine.GetAssetManager().ClearProjectContext();
        scripting::ClearScriptLogSink();
        m_panelManager.ShutdownPanels();
        m_panelManager.ShutdownImGui();
        m_engine.Shutdown();
    }
}
