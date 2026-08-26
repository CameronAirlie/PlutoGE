#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/Graphics.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi_Platform_GLFW.h>
#include <RmlUi_Renderer_GL3.h>
#include <PlutoGE_RmlUi_Target.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::render
{
    namespace
    {
        constexpr char kEventSeparator = '\x1f';

        std::string ResolveDocumentPath(assets::AssetManager &assets, const std::string &reference)
        {
            if (reference.empty())
                return {};

            // Canvas document paths are authored relative to the project's asset
            // directory. AssetManager intentionally leaves ordinary relative
            // paths relative to the process working directory, so qualify them
            // here. Explicit schemes and absolute paths retain their normal
            // AssetManager behaviour.
            const std::filesystem::path referencePath(reference);
            if (!referencePath.is_absolute() && reference.find("://") == std::string::npos &&
                !assets.GetProjectRootDirectory().empty())
            {
                return (std::filesystem::path(assets.GetProjectRootDirectory()) /
                        assets.GetProjectAssetDirectory() / referencePath)
                    .lexically_normal().string();
            }

            return assets.ResolveAssetPath(reference);
        }

        std::filesystem::path NormalizeDocumentPath(assets::AssetManager &assets,
                                                    const std::string &reference)
        {
            const std::string resolved = ResolveDocumentPath(assets, reference);
            if (resolved.empty())
                return {};

            std::error_code error;
            auto normalized = std::filesystem::weakly_canonical(resolved, error);
            if (error)
                normalized = std::filesystem::path(resolved).lexically_normal();
            return normalized;
        }

        std::string EventKey(const std::string &document, const std::string &id, const std::string &event)
        {
            return document + kEventSeparator + id + kEventSeparator + event;
        }

        std::filesystem::file_time_type DocumentSourceWriteTime(const std::filesystem::path &document,
                                                                std::error_code &error)
        {
            auto newest = std::filesystem::last_write_time(document, error);
            if (error) return {};
            for (std::filesystem::directory_iterator it(document.parent_path(), error), end; !error && it != end; it.increment(error))
            {
                if (it->is_regular_file() && it->path().extension() == ".rcss")
                {
                    std::error_code timeError;
                    newest = std::max(newest, std::filesystem::last_write_time(it->path(), timeError));
                }
            }
            return newest;
        }

        std::string EscapeMarkup(std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const char character : value)
            {
                switch (character)
                {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '"': result += "&quot;"; break;
                default: result += character; break;
                }
            }
            return result;
        }

        std::string CssColor(const glm::vec4 &color)
        {
            const auto channel = [](float value) { return std::clamp(static_cast<int>(std::round(value * 255.0f)), 0, 255); };
            return "rgba(" + std::to_string(channel(color.r)) + "," +
                   std::to_string(channel(color.g)) + "," + std::to_string(channel(color.b)) + "," +
                   std::to_string(channel(color.a)) + ")";
        }

        std::string GeneratedFontFamily(const std::string &fontPath)
        {
            return "PlutoGeneratedText" + std::to_string(std::hash<std::string>{}(fontPath));
        }

        std::string ResolveGeneratedFontPath(assets::AssetManager &assets, const std::string &requestedFontPath)
        {
            if (!requestedFontPath.empty())
            {
                const std::string resolved = ResolveDocumentPath(assets, requestedFontPath);
                std::error_code error;
                if (!resolved.empty() && std::filesystem::is_regular_file(resolved, error))
                    return resolved;
            }

            const std::filesystem::path candidates[] = {
                "editor/resources/fonts/MartianMono-StdRg.ttf",
                "resources/fonts/MartianMono-StdRg.ttf",
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/arial.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/System/Library/Fonts/Supplemental/Arial.ttf",
            };
            for (const auto &candidate : candidates)
            {
                std::error_code error;
                if (std::filesystem::is_regular_file(candidate, error))
                    return candidate.lexically_normal().string();
            }
            return {};
        }

        std::string GeneratedTextDocument(const scene::UITextComponent &text)
        {
            static constexpr const char *horizontal[] = {"left", "center", "right", "left", "center", "right", "left", "center", "right"};
            static constexpr const char *vertical[] = {"flex-start", "flex-start", "flex-start", "center", "center", "center", "flex-end", "flex-end", "flex-end"};
            const int alignment = std::clamp(static_cast<int>(text.GetAlignment()), 0, 8);
            std::string content = text.IsRichText() ? text.GetText() : EscapeMarkup(text.GetText());
            std::string style = "html,body{width:100%;height:100%;margin:0;background:transparent;}"
                                "body{display:flex;align-items:" + std::string(vertical[alignment]) +
                                ";justify-content:" + (alignment % 3 == 0 ? std::string("flex-start") : alignment % 3 == 1 ? std::string("center") : std::string("flex-end")) +
                                ";color:" + CssColor(text.GetColor()) + ";font-family:" + GeneratedFontFamily(text.GetFontPath()) +
                                ";font-size:" + std::to_string(text.GetFontSize()) +
                                "px;line-height:" + std::to_string(text.GetLineSpacing()) + ";text-align:" + horizontal[alignment] +
                                ";white-space:" + (text.GetWrap() ? std::string("pre-wrap") : std::string("pre")) + ";";
            if (text.GetOutlineWidth() > 0.0f)
                style += "text-shadow:" + std::to_string(text.GetOutlineWidth()) + "px " +
                         std::to_string(text.GetOutlineWidth()) + "px " + CssColor(text.GetOutlineColor()) + ";";
            style += "}";
            return "<rml><head><style>" + style + "</style></head><body><div>" + content + "</div></body></rml>";
        }

        std::filesystem::file_time_type StylesheetDirectoryWriteTime(
            const std::filesystem::path &directory, std::error_code &error)
        {
            std::filesystem::file_time_type newest{};
            for (std::filesystem::directory_iterator it(directory, error), end;
                 !error && it != end; it.increment(error))
            {
                if (!it->is_regular_file() || it->path().extension() != ".rcss")
                    continue;
                std::error_code timeError;
                const auto writeTime = std::filesystem::last_write_time(it->path(), timeError);
                if (!timeError)
                    newest = std::max(newest, writeTime);
            }
            return newest;
        }

        bool SourceUsesBackdropFilter(const std::filesystem::path &documentPath)
        {
            const auto readText = [](const std::filesystem::path &path)
            {
                std::ifstream input(path, std::ios::binary);
                return input ? std::string(std::istreambuf_iterator<char>(input),
                                           std::istreambuf_iterator<char>())
                             : std::string{};
            };

            const std::string documentSource = readText(documentPath);
            if (documentSource.find("backdrop-filter") != std::string::npos)
                return true;

            // Only inspect stylesheets linked by this document. Scanning every
            // RCSS sibling would make an unrelated hidden pause menu force a
            // full-frame backdrop copy for the gameplay HUD.
            const std::regex stylesheetPattern(
                R"(<link[^>]*href\s*=\s*[\"']([^\"']+\.rcss)[\"'][^>]*>)",
                std::regex::icase);
            for (std::sregex_iterator it(documentSource.begin(), documentSource.end(), stylesheetPattern), end;
                 it != end; ++it)
            {
                const auto stylesheet = (documentPath.parent_path() / (*it)[1].str()).lexically_normal();
                if (readText(stylesheet).find("backdrop-filter") != std::string::npos)
                    return true;
            }
            return false;
        }

        class RuntimeEventListener final : public Rml::EventListener
        {
        public:
            RuntimeEventListener(RmlUiRuntime &owner, std::string key)
                : m_owner(owner), m_key(std::move(key)) {}

            void ProcessEvent(Rml::Event &) override { m_owner.NotifyEvent(m_key); }
            void OnDetach(Rml::Element *element) override
            {
                m_owner.NotifyEventListenerDetached(m_key, element);
            }

        private:
            RmlUiRuntime &m_owner;
            std::string m_key;
        };

        struct DocumentRequest
        {
            bool visible = false;
            float scale = 1.0f;
            bool projected = false;
            bool worldSurface = false;
            bool inFrontOfCamera = true;
            glm::vec2 position{0.0f};
            glm::vec2 size{0.0f};
            glm::vec2 pivot{0.5f};
            glm::mat4 model{1.0f};
            bool generated = false;
            std::string generatedSource;
            std::string fontPath;
        };

        bool ProjectWorldPosition(const glm::vec3 &position, const glm::mat4 &view,
                                  const glm::mat4 &projection, const glm::vec2 &viewport,
                                  glm::vec2 &screen, float &pixelsPerWorldUnit)
        {
            const glm::vec4 viewPosition = view * glm::vec4(position, 1.0f);
            const glm::vec4 clip = projection * viewPosition;
            if (clip.w <= 0.0001f) return false;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
            screen = {(ndc.x * 0.5f + 0.5f) * viewport.x,
                      (0.5f - ndc.y * 0.5f) * viewport.y};
            pixelsPerWorldUnit = std::abs(projection[1][1]) * viewport.y * 0.5f;
            if (std::abs(projection[3][3]) < 0.5f)
                pixelsPerWorldUnit /= std::max(-viewPosition.z, 0.0001f);
            return true;
        }

        void ConfigureDocument(Rml::ElementDocument &document, const DocumentRequest &request,
                               int viewportWidth, int viewportHeight,
                               bool updateTransform, bool updateDimensions)
        {
            const float scale = std::max(request.scale, 0.0001f);
            if (updateTransform)
            {
                document.SetProperty("position", "absolute");
                document.SetProperty("transform-origin", "0px 0px");
                if (request.projected)
                {
                    const glm::vec2 origin = request.position - request.size * request.pivot * scale;
                    document.SetProperty("transform", "translate(" + std::to_string(origin.x) + "px," +
                                                          std::to_string(origin.y) + "px) scale(" +
                                                          std::to_string(scale) + ")");
                }
                else
                    document.SetProperty("transform", "scale(" + std::to_string(scale) + ")");
            }
            if (request.worldSurface)
            {
                document.SetProperty("left", "0px");
                document.SetProperty("top", "0px");
                document.SetProperty("width", std::to_string(request.size.x) + "px");
                document.SetProperty("height", std::to_string(request.size.y) + "px");
            }
            else if (request.projected)
            {
                if (updateDimensions)
                {
                    document.SetProperty("left", "0px");
                    document.SetProperty("top", "0px");
                    document.SetProperty("width", std::to_string(request.size.x) + "px");
                    document.SetProperty("height", std::to_string(request.size.y) + "px");
                }
            }
            else if (updateDimensions)
            {
                document.SetProperty("left", "0px");
                document.SetProperty("top", "0px");
                document.SetProperty("width", std::to_string(static_cast<float>(viewportWidth) / scale) + "px");
                document.SetProperty("height", std::to_string(static_cast<float>(viewportHeight) / scale) + "px");
            }
        }

        void CollectDocuments(scene::Entity *entity,
                              const glm::vec2 &viewportSize,
                              std::unordered_map<std::string, DocumentRequest> &documents,
                              const glm::mat4 &view,
                              const glm::mat4 &projection,
                              float inheritedScale = 1.0f,
                              bool insideDocumentCanvas = false,
                              bool descend = true)
        {
            // The caller only descends through active parents, so checking the
            // local flag is sufficient. Entity::IsActive() walks every ancestor
            // and made this full-scene collection quadratic on deep skeletons.
            if (!entity || !entity->IsSelfActive())
                return;

            if (const auto *canvas = entity->GetComponent<scene::CanvasComponent>();
                canvas && canvas->IsEnabled())
            {
                inheritedScale = scene::ResolveCanvasScaleFactor(*canvas, viewportSize);
                const auto *text = entity->GetComponent<scene::UITextComponent>();
                const bool textSource = canvas->GetContentSource() == scene::RmlUiContentSource::Text;
                const bool textUsesRmlSurface = textSource &&
                                                canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpace;
                if (canvas->GetBackend() == scene::UIRenderBackend::RmlUi &&
                    ((textUsesRmlSurface && text && text->IsEnabled()) ||
                     (!textSource && !canvas->GetDocumentPath().empty())))
                {
                    insideDocumentCanvas = true;
                    const bool projected = canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpaceOverlay;
                    const bool worldSurface = canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpace;
                    const std::string reference = textSource ? "generated://text/" + std::to_string(entity->GetID())
                                                             : canvas->GetDocumentPath();
                    const std::string documentKey = (projected || worldSurface || textSource)
                                                        ? reference + "#entity:" + std::to_string(entity->GetID())
                                                        : reference;
                    auto &request = documents[documentKey];
                    request = {
                        .visible = true,
                        .scale = textSource ? 1.0f : inheritedScale,
                    };
                    request.projected = projected;
                    request.worldSurface = worldSurface;
                    request.generated = textSource;
                    if (textSource)
                    {
                        request.generatedSource = GeneratedTextDocument(*text);
                        request.fontPath = text->GetFontPath();
                    }
                    if (request.worldSurface)
                    {
                        // The texture is authored directly in RectTransform UI
                        // units. Physical sizing is applied by the plane model,
                        // independently of screen-space Canvas scale settings.
                        request.scale = 1.0f;
                        const auto *rect = entity->GetComponent<scene::RectTransformComponent>();
                        request.size = rect ? glm::max(rect->GetSizeDelta(), glm::vec2(1.0f)) : glm::vec2(200.0f, 50.0f);
                        request.pivot = rect ? rect->GetPivot() : glm::vec2(0.5f);
                        constexpr float uiUnitsPerWorldUnit = 100.0f;
                        glm::vec2 worldSize = request.size / uiUnitsPerWorldUnit;
                        glm::vec2 projectedPosition{0.0f};
                        float pixelsPerWorldUnit = 1.0f;
                        ProjectWorldPosition(entity->GetWorldPosition(), view, projection, viewportSize,
                                             projectedPosition, pixelsPerWorldUnit);
                        if (!textSource && canvas->GetWorldSizeMode() == scene::UIWorldSizeMode::ConstantScreenSize)
                            worldSize = request.size / std::max(pixelsPerWorldUnit, 0.0001f);
                        else if (!textSource && canvas->GetWorldSizeMode() == scene::UIWorldSizeMode::DistanceScaled)
                        {
                            const float viewDistance = std::max(-(view * glm::vec4(entity->GetWorldPosition(), 1.0f)).z,
                                                                0.0001f);
                            worldSize *= viewDistance;
                        }
                        glm::mat4 surfaceTransform = entity->GetWorldTransform();
                        if (textSource)
                        {
                            // Text surfaces use the entity's X/Y scale as their
                            // physical dimensions. RectTransform remains the
                            // texture resolution and text-layout rectangle.
                            const glm::vec3 entityScale = glm::abs(entity->GetWorldScale());
                            worldSize = glm::max(glm::vec2(entityScale.x, entityScale.y), glm::vec2(0.0001f));
                            const auto normalizedAxis = [](const glm::vec3 &axis, const glm::vec3 &fallback)
                            {
                                const float length = glm::length(axis);
                                return length > 0.0001f ? axis / length : fallback;
                            };
                            surfaceTransform[0] = glm::vec4(normalizedAxis(glm::vec3(surfaceTransform[0]), {1.0f, 0.0f, 0.0f}), 0.0f);
                            surfaceTransform[1] = glm::vec4(normalizedAxis(glm::vec3(surfaceTransform[1]), {0.0f, 1.0f, 0.0f}), 0.0f);
                            surfaceTransform[2] = glm::vec4(normalizedAxis(glm::vec3(surfaceTransform[2]), {0.0f, 0.0f, 1.0f}), 0.0f);
                        }
                        if (!textSource && canvas->GetFaceCamera())
                        {
                            const glm::vec3 worldScale = entity->GetWorldScale();
                            surfaceTransform = glm::translate(glm::mat4(1.0f), entity->GetWorldPosition()) *
                                               glm::mat4(glm::mat3(glm::inverse(view))) *
                                               glm::scale(glm::mat4(1.0f), worldScale);
                        }
                        const glm::vec2 centerOffset = (glm::vec2(0.5f) - request.pivot) * worldSize;
                        request.model = surfaceTransform *
                                        glm::translate(glm::mat4(1.0f), glm::vec3(centerOffset, 0.0f)) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(worldSize, 1.0f));
                    }
                    else if (request.projected)
                    {
                        float perspectiveScale = 1.0f;
                        request.inFrontOfCamera = ProjectWorldPosition(entity->GetWorldPosition(), view, projection,
                                                                       viewportSize, request.position, perspectiveScale);
                        const auto *rect = entity->GetComponent<scene::RectTransformComponent>();
                        request.size = rect ? glm::max(rect->GetSizeDelta(), glm::vec2(1.0f)) : glm::vec2(200.0f, 50.0f);
                        request.pivot = rect ? rect->GetPivot() : glm::vec2(0.5f);
                        const glm::vec3 worldScale = glm::abs(entity->GetWorldScale());
                        const float uniformScale = std::max(worldScale.x, worldScale.y);
                        const bool constantSize = canvas->GetRenderMode() == scene::CanvasRenderMode::WorldSpaceOverlay ||
                                                  canvas->GetWorldSizeMode() == scene::UIWorldSizeMode::ConstantScreenSize;
                        request.scale = inheritedScale * uniformScale *
                                        (constantSize ? 1.0f : perspectiveScale / 100.0f);
                        if (request.inFrontOfCamera)
                        {
                            const glm::vec2 scaledSize = request.size * request.scale;
                            const glm::vec2 min = request.position - scaledSize * request.pivot;
                            const glm::vec2 max = min + scaledSize;
                            // Avoid layout and draw work for projected widgets
                            // that cannot contribute to the current viewport.
                            request.inFrontOfCamera = scaledSize.x >= 1.0f && scaledSize.y >= 1.0f &&
                                                      max.x >= 0.0f && max.y >= 0.0f &&
                                                      min.x <= viewportSize.x && min.y <= viewportSize.y;
                        }
                    }
                }
                // A document canvas owns its subtree. Nested native canvases are
                // still discovered through their own roots during migration.
            }

            if (const auto *widget = entity->GetComponent<scene::RmlWidgetComponent>();
                !insideDocumentCanvas && widget && widget->IsEnabled() && !widget->GetSource().empty())
            {
                auto [entry, inserted] = documents.try_emplace(
                    widget->GetSource(), DocumentRequest{
                                             .visible = widget->IsVisible(),
                                             .scale = inheritedScale,
                                         });
                if (!inserted)
                    entry->second.visible = entry->second.visible || widget->IsVisible();
            }

            if (descend)
            {
                for (auto *child : entity->GetChildren())
                    CollectDocuments(child, viewportSize, documents, view, projection, inheritedScale,
                                     insideDocumentCanvas, true);
            }
        }

        int CurrentModifiers(GLFWwindow *window)
        {
            int glfwModifiers = 0;
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
                glfwModifiers |= GLFW_MOD_SHIFT;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
                glfwModifiers |= GLFW_MOD_CONTROL;
            if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
                glfwModifiers |= GLFW_MOD_ALT;
            if (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
                glfwModifiers |= GLFW_MOD_SUPER;
            return RmlGLFW::ConvertKeyModifiers(glfwModifiers);
        }
    }

    RmlUiRuntime &RmlUiRuntime::Get()
    {
        static RmlUiRuntime instance;
        return instance;
    }

    bool RmlUiRuntime::Initialize(platform::Window &window)
    {
        if (m_context)
            return true;

        m_window = &window;
        m_hotReloadEnabled = core::Engine::GetInstance().GetConfig().isEditorHost;
        m_renderer = std::make_unique<RenderInterface_GL3>();
        if (!static_cast<bool>(*m_renderer))
        {
            m_renderer.reset();
            m_window = nullptr;
            return false;
        }

        m_system = std::make_unique<SystemInterface_GLFW>();
        m_system->SetWindow(static_cast<GLFWwindow *>(window.GetWindow()));
        Rml::SetRenderInterface(m_renderer.get());
        Rml::SetSystemInterface(m_system.get());
        if (!Rml::Initialise())
        {
            m_system.reset();
            m_renderer.reset();
            m_window = nullptr;
            return false;
        }

        const auto extents = window.GetExtents();
        m_width = std::max(extents.width, 1);
        m_height = std::max(extents.height, 1);
        m_context = Rml::CreateContext("PlutoGE.Runtime", {m_width, m_height});
        if (!m_context)
        {
            Rml::Shutdown();
            m_system.reset();
            m_renderer.reset();
            m_window = nullptr;
            return false;
        }
        return true;
    }

    void RmlUiRuntime::Shutdown()
    {
        if (!m_context && !m_renderer && !m_system)
            return;

        ResetRuntimeState();
        m_loadedFontFaces.clear();
        m_fontData.clear();
        if (m_context)
        {
            Rml::RemoveContext(m_context->GetName());
            m_context = nullptr;
        }
        Rml::Shutdown();
        m_system.reset();
        m_renderer.reset();
        m_window = nullptr;
        m_width = 0;
        m_height = 0;
        m_hotReloadEnabled = false;
        CloseAssetFileWatcher();
    }

    void RmlUiRuntime::ResetRuntimeState()
    {
        DetachEventSubscriptions();
        for (const auto &[key, document] : m_documents)
        {
            if (document)
                document->Close();
        }
        m_documents.clear();
        m_documentReferences.clear();
        m_documentWriteTimes.clear();
        m_documentScales.clear();
        m_documentUsesBackdrop.clear();
        m_generatedDocumentSources.clear();
        m_resolvedGeneratedFonts.clear();
        m_documentProjectionState.clear();
        m_documentSizes.clear();
        DestroyWorldSurfaceTargets();
        m_eventSubscriptions.clear();
        m_reportedLoadFailures.clear();
        m_pendingEvents.clear();
        m_lastInputFrame = 0;
        m_cpuTiming = {};
    }

    bool RmlUiRuntime::ConsumeAssetFileChange()
    {
        if (!m_hotReloadEnabled)
            return false;

#ifdef _WIN32
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        std::filesystem::path assetDirectory = assets.GetProjectRootDirectory();
        assetDirectory /= assets.GetProjectAssetDirectory();
        assetDirectory = assetDirectory.lexically_normal();
        if (assetDirectory != m_watchedAssetDirectory)
        {
            CloseAssetFileWatcher();
            std::error_code error;
            if (!std::filesystem::is_directory(assetDirectory, error))
                return false;
            const HANDLE handle = FindFirstChangeNotificationW(
                assetDirectory.c_str(), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
            if (handle == INVALID_HANDLE_VALUE)
                return false;
            m_assetFileChangeHandle = handle;
            m_watchedAssetDirectory = assetDirectory;
            return false;
        }
        if (!m_assetFileChangeHandle)
            return false;
        const HANDLE handle = static_cast<HANDLE>(m_assetFileChangeHandle);
        if (WaitForSingleObject(handle, 0) != WAIT_OBJECT_0)
            return false;
        if (!FindNextChangeNotification(handle))
            CloseAssetFileWatcher();
        return true;
#else
        return false;
#endif
    }

    void RmlUiRuntime::CloseAssetFileWatcher()
    {
#ifdef _WIN32
        if (m_assetFileChangeHandle)
            FindCloseChangeNotification(static_cast<HANDLE>(m_assetFileChangeHandle));
#endif
        m_assetFileChangeHandle = nullptr;
        m_watchedAssetDirectory.clear();
    }

    void RmlUiRuntime::SynchronizeDocuments(const scene::Scene &scene, const glm::mat4 &view,
                                            const glm::mat4 &projection)
    {
        // The OS watcher is a non-blocking event check. Filesystem metadata is
        // inspected only after the project Assets directory actually changes.
        const bool checkForHotReload = ConsumeAssetFileChange();

        std::unordered_map<std::string, DocumentRequest> requestedDocuments;
        const glm::vec2 viewportSize{static_cast<float>(m_width), static_cast<float>(m_height)};
        const auto &canvases = scene.GetCanvasComponents();
        requestedDocuments.reserve(std::max<std::size_t>(canvases.size(), 4));
        for (const auto *canvas : canvases)
        {
            auto *owner = canvas ? canvas->GetOwner() : nullptr;
            if (owner && owner->IsActive())
                CollectDocuments(owner, viewportSize, requestedDocuments, view, projection,
                                 1.0f, false, false);
        }

        // Widgets are registered by Scene, so document discovery is O(number
        // of UI documents) instead of recursively walking every canvas child.
        for (const auto *widget : scene.GetRmlWidgetComponents())
        {
            auto *owner = widget ? widget->GetOwner() : nullptr;
            if (!owner || !owner->IsActive())
                continue;

            float inheritedScale = 1.0f;
            bool insideDocumentCanvas = false;
            for (auto *ancestor = owner; ancestor; ancestor = ancestor->GetParent())
            {
                const auto *canvas = ancestor->GetComponent<scene::CanvasComponent>();
                if (!canvas || !canvas->IsEnabled())
                    continue;
                inheritedScale = scene::ResolveCanvasScaleFactor(*canvas, viewportSize);
                insideDocumentCanvas = canvas->GetBackend() == scene::UIRenderBackend::RmlUi &&
                                       canvas->GetContentSource() == scene::RmlUiContentSource::Document &&
                                       !canvas->GetDocumentPath().empty();
                break;
            }
            CollectDocuments(owner, viewportSize, requestedDocuments, view, projection,
                             inheritedScale, insideDocumentCanvas, false);
        }

        for (auto it = m_worldSurfaceTargets.begin(); it != m_worldSurfaceTargets.end();)
        {
            const auto request = requestedDocuments.find(it->first);
            if (request == requestedDocuments.end() || !request->second.worldSurface)
            {
                if (it->second.framebuffer) Graphics::DeleteFramebuffers(1, &it->second.framebuffer);
                if (it->second.texture) Graphics::DeleteTextures(1, &it->second.texture);
                it = m_worldSurfaceTargets.erase(it);
            }
            else
                ++it;
        }

        bool detachedForDocumentChange = false;
        for (auto it = m_documents.begin(); it != m_documents.end();)
        {
            if (!requestedDocuments.contains(it->first))
            {
                if (!detachedForDocumentChange)
                {
                    DetachEventSubscriptions();
                    detachedForDocumentChange = true;
                }
                if (it->second)
                    it->second->Close();
                m_documentWriteTimes.erase(it->first);
                m_documentScales.erase(it->first);
                m_documentUsesBackdrop.erase(it->first);
                m_generatedDocumentSources.erase(it->first);
                m_documentProjectionState.erase(it->first);
                m_documentSizes.erase(it->first);
                m_documentReferences.erase(it->first);
                it = m_documents.erase(it);
            }
            else
            {
                ++it;
            }
        }

        auto &assets = core::Engine::GetInstance().GetAssetManager();
        struct HotReloadSource
        {
            std::string path;
            std::filesystem::file_time_type writeTime{};
            bool valid = false;
        };
        // Projected canvases create one RmlUi document per entity, but all
        // instances of an asset share the same source files. Resolve and scan
        // each source only once per hot-reload poll.
        std::unordered_map<std::string, HotReloadSource> hotReloadSources;
        std::unordered_map<std::string, std::filesystem::file_time_type> stylesheetDirectoryWriteTimes;
        for (const auto &[key, request] : requestedDocuments)
        {
            const auto instanceSuffix = key.rfind("#entity:");
            const std::string reference = instanceSuffix != std::string::npos
                                              ? key.substr(0, instanceSuffix)
                                              : key;
            if (request.generated)
            {
                const auto knownSource = m_generatedDocumentSources.find(key);
                if (knownSource != m_generatedDocumentSources.end() && knownSource->second != request.generatedSource)
                {
                    DetachEventSubscriptions();
                    if (auto document = m_documents.find(key); document != m_documents.end())
                    {
                        if (document->second) document->second->Close();
                        m_documents.erase(document);
                    }
                    m_documentScales.erase(key);
                    m_generatedDocumentSources.erase(knownSource);
                    if (auto target = m_worldSurfaceTargets.find(key); target != m_worldSurfaceTargets.end())
                        target->second.dirty = true;
                }
                auto [resolvedFontIt, insertedFont] = m_resolvedGeneratedFonts.try_emplace(request.fontPath);
                if (insertedFont)
                    resolvedFontIt->second = ResolveGeneratedFontPath(assets, request.fontPath);
                const std::string &resolvedFont = resolvedFontIt->second;
                if (!resolvedFont.empty())
                {
                    const std::string fontFamily = GeneratedFontFamily(request.fontPath);
                    const std::string fontKey = resolvedFont + "\x1f" + fontFamily;
                    if (!resolvedFont.empty() && !m_loadedFontFaces.contains(fontKey))
                    {
                        std::ifstream fontStream(resolvedFont, std::ios::binary);
                        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(fontStream)),
                                                         std::istreambuf_iterator<char>());
                        bool loaded = false;
                        if (!bytes.empty())
                        {
                            m_fontData.push_back(std::move(bytes));
                            const auto &stored = m_fontData.back();
                            loaded = Rml::LoadFontFace({stored.data(), stored.size()}, fontFamily,
                                                       Rml::Style::FontStyle::Normal,
                                                       Rml::Style::FontWeight::Auto);
                            if (!loaded)
                                m_fontData.pop_back();
                        }
                        if (!loaded)
                            std::cerr << "[RmlUi] Failed to load generated text font '" << resolvedFont << "'.\n";
                        m_loadedFontFaces.insert(fontKey);
                    }
                }
                else if (m_reportedLoadFailures.insert(reference + "#font").second)
                    std::cerr << "[RmlUi] Generated text has no usable font. Assign UI Text FontPath.\n";
            }
            if (request.worldSurface)
            {
                auto &target = m_worldSurfaceTargets[key];
                const int surfaceWidth = std::clamp(static_cast<int>(std::ceil(request.size.x)), 1, 4096);
                const int surfaceHeight = std::clamp(static_cast<int>(std::ceil(request.size.y)), 1, 4096);
                target.model = request.model;
                if (!target.framebuffer || target.width != surfaceWidth || target.height != surfaceHeight)
                {
                    if (target.framebuffer) Graphics::DeleteFramebuffers(1, &target.framebuffer);
                    if (target.texture) Graphics::DeleteTextures(1, &target.texture);
                    glGenTextures(1, &target.texture);
                    Graphics::BindTexture(GL_TEXTURE_2D, target.texture);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, surfaceWidth, surfaceHeight, 0,
                                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glGenFramebuffers(1, &target.framebuffer);
                    Graphics::BindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
                    target.width = surfaceWidth;
                    target.height = surfaceHeight;
                    target.dirty = true;
                }
            }
            if (m_documents.contains(key))
            {
                if (checkForHotReload && !request.generated)
                {
                    auto [sourceIt, inserted] = hotReloadSources.try_emplace(reference);
                    if (inserted)
                    {
                        sourceIt->second.path = ResolveDocumentPath(assets, reference);
                        std::error_code error;
                        const std::filesystem::path sourcePath(sourceIt->second.path);
                        sourceIt->second.writeTime = std::filesystem::last_write_time(sourcePath, error);
                        if (!error)
                        {
                            const std::string directoryKey = sourcePath.parent_path().lexically_normal().string();
                            auto [directoryIt, directoryInserted] =
                                stylesheetDirectoryWriteTimes.try_emplace(directoryKey);
                            if (directoryInserted)
                            {
                                std::error_code directoryError;
                                directoryIt->second = StylesheetDirectoryWriteTime(
                                    sourcePath.parent_path(), directoryError);
                                if (directoryError)
                                    error = directoryError;
                            }
                            if (!error)
                                sourceIt->second.writeTime = std::max(
                                    sourceIt->second.writeTime, directoryIt->second);
                        }
                        sourceIt->second.valid = !error;
                    }
                    const auto known = m_documentWriteTimes.find(key);
                    if (sourceIt->second.valid && known != m_documentWriteTimes.end() &&
                        sourceIt->second.writeTime != known->second)
                        ReloadDocument(key);
                }
                if (auto *document = m_documents.at(key))
                {
                    const float scale = std::max(request.scale, 0.0001f);
                    const auto knownScale = m_documentScales.find(key);
                    const bool firstConfiguration = knownScale == m_documentScales.end();
                    const bool scaleChanged = firstConfiguration || knownScale->second != scale;
                    if (scaleChanged)
                    {
                        m_documentScales[key] = scale;
                    }
                    // Screen documents retain their layout until scale or
                    // viewport changes. Projected documents only update their
                    // screen-space coordinates every frame.
                    // Culled projected documents remain hidden and do not need
                    // CSS mutations. Updating left/top/scale on them invalidates
                    // RmlUi style and layout work despite producing no pixels.
                    const glm::vec4 projectionState(request.position, request.pivot);
                    const auto knownProjection = m_documentProjectionState.find(key);
                    const bool projectionChanged = knownProjection == m_documentProjectionState.end() ||
                                                   knownProjection->second != projectionState;
                    const auto knownSize = m_documentSizes.find(key);
                    const bool sizeChanged = knownSize == m_documentSizes.end() || knownSize->second != request.size;
                    if (projectionChanged) m_documentProjectionState[key] = projectionState;
                    if (sizeChanged) m_documentSizes[key] = request.size;
                    if ((scaleChanged || (request.projected && projectionChanged) ||
                         (request.worldSurface && sizeChanged)) &&
                        (!request.projected || request.inFrontOfCamera))
                        ConfigureDocument(*document, request, m_width, m_height,
                                          scaleChanged || (request.projected && projectionChanged),
                                          firstConfiguration || sizeChanged ||
                                                            (!request.projected && scaleChanged));
                    if (request.visible && request.inFrontOfCamera && !request.worldSurface)
                    {
                        if (!document->IsVisible())
                            document->Show(Rml::ModalFlag::Keep, Rml::FocusFlag::Auto);
                    }
                    else if (document->IsVisible())
                    {
                        m_context->UnfocusDocument(document);
                        document->Hide();
                    }
                }
                continue;
            }

            const std::string path = request.generated ? std::string{} : ResolveDocumentPath(assets, reference);
            if (request.generated)
            {
                if (auto *document = m_context->LoadDocumentFromMemory(request.generatedSource, reference))
                {
                    ConfigureDocument(*document, request, m_width, m_height, true, true);
                    (request.visible && request.inFrontOfCamera && !request.worldSurface) ? document->Show() : document->Hide();
                    m_documents.emplace(key, document);
                    m_documentReferences[key] = reference;
                    m_documentScales[key] = std::max(request.scale, 0.0001f);
                    m_documentUsesBackdrop[key] = false;
                    m_generatedDocumentSources[key] = request.generatedSource;
                    m_documentProjectionState[key] = glm::vec4(request.position, request.pivot);
                    m_documentSizes[key] = request.size;
                }
                else if (m_reportedLoadFailures.insert(reference).second)
                    std::cerr << "[RmlUi] Failed to create generated text document for '" << reference << "'.\n";
                continue;
            }
            if (path.empty())
            {
                if (m_reportedLoadFailures.insert(reference).second)
                    std::cerr << "[RmlUi] Could not resolve Canvas document '" << reference << "'.\n";
                continue;
            }

            std::error_code existsError;
            if (!std::filesystem::is_regular_file(path, existsError))
            {
                if (m_reportedLoadFailures.insert(reference).second)
                    std::cerr << "[RmlUi] Canvas document '" << reference
                              << "' was not found at '" << path << "'.\n";
                continue;
            }

            LoadDocumentFonts(path);
            if (auto *document = m_context->LoadDocument(path))
            {
                const float scale = std::max(request.scale, 0.0001f);
                ConfigureDocument(*document, request, m_width, m_height, true, true);
                (request.visible && request.inFrontOfCamera && !request.worldSurface) ? document->Show() : document->Hide();
                m_documents.emplace(key, document);
                m_documentReferences[key] = reference;
                m_documentScales[key] = scale;
                m_documentUsesBackdrop[key] = SourceUsesBackdropFilter(path);
                m_reportedLoadFailures.erase(reference);
                std::clog << "[RmlUi] Loaded Canvas document '" << reference
                          << "' from '" << path << "'.\n";
                std::error_code error;
                const auto writeTime = DocumentSourceWriteTime(path, error);
                if (!error)
                    m_documentWriteTimes[key] = writeTime;
            }
            else if (m_reportedLoadFailures.insert(reference).second)
            {
                std::cerr << "[RmlUi] Failed to parse or load Canvas document '" << reference
                          << "' from '" << path << "'. Check the preceding RmlUi messages for markup errors.\n";
            }
        }
        AttachEventSubscriptions();
    }

    void RmlUiRuntime::LoadDocumentFonts(const std::filesystem::path &documentPath)
    {
        // RmlUi requires fonts to be registered through LoadFontFace; RCSS
        // @font-face rules are a PlutoGE authoring convenience parsed here.
        // Keep the byte buffers alive until Rml::Shutdown as required by RmlUi.
        std::ifstream documentStream(documentPath, std::ios::binary);
        if (!documentStream)
            return;
        const std::string documentSource(
            (std::istreambuf_iterator<char>(documentStream)),
            std::istreambuf_iterator<char>());

        // Only inspect stylesheets linked by this document. Scanning every
        // sibling RCSS produced misleading missing-font warnings from
        // unrelated documents stored in the same UI directory.
        static const std::regex styleLinkRule(
            R"rml(<link\b[^>]*\bhref\s*=\s*(?:"([^"]+\.rcss)"|'([^']+\.rcss)')[^>]*>)rml",
            std::regex::icase);
        std::vector<std::filesystem::path> styleSheets;
        for (std::sregex_iterator link(documentSource.begin(), documentSource.end(), styleLinkRule), end;
             link != end; ++link)
        {
            const std::string relativePath =
                (*link)[1].matched ? (*link)[1].str() : (*link)[2].str();
            styleSheets.push_back(
                (documentPath.parent_path() / relativePath).lexically_normal());
        }

        for (const auto &styleSheetPath : styleSheets)
        {
            std::ifstream stream(styleSheetPath, std::ios::binary);
            if (!stream)
                continue;
            const std::string source((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());

            static const std::regex faceRule(R"(@font-face\s*\{([^}]*)\})",
                                             std::regex::icase);
            static const std::regex familyRule(
                R"rml(font-family\s*:\s*(?:"([^"]+)"|'([^']+)'|([^;]+))\s*;)rml",
                std::regex::icase);
            static const std::regex sourceRule(
                R"rml(src\s*:\s*url\(\s*(?:"([^"]+)"|'([^']+)'|([^)]+))\s*\)\s*;)rml",
                std::regex::icase);

            for (std::sregex_iterator face(source.begin(), source.end(), faceRule), endFace;
                 face != endFace; ++face)
            {
                const std::string body = (*face)[1].str();
                std::smatch familyMatch;
                std::smatch sourceMatch;
                if (!std::regex_search(body, familyMatch, familyRule) ||
                    !std::regex_search(body, sourceMatch, sourceRule))
                    continue;

                const auto matchedValue = [](const std::smatch &match) {
                    for (std::size_t i = 1; i < match.size(); ++i)
                    {
                        if (!match[i].matched) continue;
                        std::string value = match[i].str();
                        const auto first = value.find_first_not_of(" \t\r\n");
                        const auto last = value.find_last_not_of(" \t\r\n");
                        return first == std::string::npos
                            ? std::string{}
                            : value.substr(first, last - first + 1);
                    }
                    return std::string{};
                };
                const std::string family = matchedValue(familyMatch);
                const std::string relativeSource = matchedValue(sourceMatch);
                const auto fontPath = (styleSheetPath.parent_path() / relativeSource)
                                          .lexically_normal();
                const std::string key = fontPath.generic_string() + "\x1f" + family;
                if (m_loadedFontFaces.contains(key))
                    continue;

                std::ifstream fontStream(fontPath, std::ios::binary);
                if (!fontStream)
                {
                    std::cerr << "[RmlUi] Font '" << family << "' was not found at '"
                              << fontPath.string() << "' (declared in '"
                              << styleSheetPath.string() << "').\n";
                    m_loadedFontFaces.insert(key);
                    continue;
                }

                std::vector<unsigned char> bytes(
                    (std::istreambuf_iterator<char>(fontStream)),
                    std::istreambuf_iterator<char>());
                if (bytes.empty())
                    continue;

                m_fontData.push_back(std::move(bytes));
                const auto &stored = m_fontData.back();
                if (Rml::LoadFontFace(
                        {stored.data(), stored.size()}, family,
                        Rml::Style::FontStyle::Normal,
                        Rml::Style::FontWeight::Auto))
                {
                    std::clog << "[RmlUi] Loaded font face '" << family << "' from '"
                              << fontPath.string() << "'.\n";
                    m_loadedFontFaces.insert(key);
                }
                else
                {
                    std::cerr << "[RmlUi] Failed to load font face '" << family
                              << "' from '" << fontPath.string() << "'.\n";
                    m_fontData.pop_back();
                    m_loadedFontFaces.insert(key);
                }
            }
        }
    }

    void RmlUiRuntime::DestroyWorldSurfaceTargets()
    {
        for (auto &[key, target] : m_worldSurfaceTargets)
        {
            if (target.framebuffer) Graphics::DeleteFramebuffers(1, &target.framebuffer);
            if (target.texture) Graphics::DeleteTextures(1, &target.texture);
        }
        m_worldSurfaceTargets.clear();
        m_worldSurfaceDraws.clear();
    }

    void RmlUiRuntime::RenderWorldSurfaces()
    {
        m_worldSurfaceDraws.clear();
        if (!m_context || !m_renderer || m_worldSurfaceTargets.empty())
            return;

        bool needsRender = false;
        for (auto &[key, target] : m_worldSurfaceTargets)
        {
            m_worldSurfaceDraws.push_back({target.texture, target.model});
            needsRender = needsRender || target.dirty;
        }
        if (!needsRender)
            return;

        GLint previousFramebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
        std::vector<Rml::ElementDocument *> previouslyVisible;
        for (const auto &[key, document] : m_documents)
        {
            if (document && document->IsVisible())
            {
                previouslyVisible.push_back(document);
                document->Hide();
            }
        }
        for (auto &[key, target] : m_worldSurfaceTargets)
        {
            if (!target.dirty)
                continue;
            const auto documentIt = m_documents.find(key);
            if (documentIt == m_documents.end() || !documentIt->second || !target.framebuffer || !target.texture)
                continue;

            auto *document = documentIt->second;
            document->Show(Rml::ModalFlag::Keep, Rml::FocusFlag::None);
            m_context->SetDimensions({target.width, target.height});
            m_renderer->SetViewport(target.width, target.height);
            m_context->Update();

            Graphics::BindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
            Graphics::SetViewport(0, 0, target.width, target.height);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            PlutoGE_SetRmlUiFramebuffer(target.framebuffer);
            m_renderer->BeginFrame();
            m_context->Render();
            m_renderer->EndFrame();
            document->Hide();
            target.dirty = false;
        }

        for (auto *document : previouslyVisible)
            document->Show(Rml::ModalFlag::Keep, Rml::FocusFlag::None);

        m_context->SetDimensions({m_width, m_height});
        m_renderer->SetViewport(m_width, m_height);
        Graphics::BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        Graphics::SetViewport(0, 0, m_width, m_height);
        PlutoGE_SetRmlUiFramebuffer(static_cast<GLuint>(previousFramebuffer));
    }

    Rml::ElementDocument *RmlUiRuntime::FindDocument(const std::string &document) const
    {
        const auto found = m_documents.find(document);
        if (found != m_documents.end())
            return found->second;

        if (const auto alias = m_documentAliases.find(document); alias != m_documentAliases.end())
        {
            if (const auto resolved = m_documents.find(alias->second); resolved != m_documents.end())
                return resolved->second;
            // Document synchronization may have removed or renamed the keyed
            // instance. Drop the stale alias and resolve it again below.
            m_documentAliases.erase(alias);
        }

        // Canvas paths and serialized script fields may spell the same asset
        // differently (for example a project asset URI versus a path relative
        // to the Assets directory). Resolve both forms before giving up so the
        // managed RmlDocument API addresses the document selected by Canvas.
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        const auto requestedPath = NormalizeDocumentPath(assets, document);
        if (requestedPath.empty())
            return nullptr;

        for (const auto &[key, loaded] : m_documents)
        {
            const auto mapped = m_documentReferences.find(key);
            const std::string &reference = mapped != m_documentReferences.end() ? mapped->second : key;
            if (NormalizeDocumentPath(assets, reference) == requestedPath)
            {
                m_documentAliases.insert_or_assign(document, key);
                return loaded;
            }
        }
        return nullptr;
    }

    bool RmlUiRuntime::ShowDocument(const std::string &document, bool visible)
    {
        auto *target = FindDocument(document);
        if (!target)
            return false;
        if (visible)
        {
            if (!target->IsVisible())
                target->Show(Rml::ModalFlag::Keep, Rml::FocusFlag::Auto);
        }
        else if (target->IsVisible())
        {
            m_context->UnfocusDocument(target);
            target->Hide();
        }
        return true;
    }

    bool RmlUiRuntime::ReloadDocument(const std::string &document)
    {
        if (!m_context)
            return false;
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        const auto mappedReference = m_documentReferences.find(document);
        const std::string reference = mappedReference != m_documentReferences.end() ? mappedReference->second : document;
        const std::string path = ResolveDocumentPath(assets, reference);
        if (path.empty())
            return false;
        LoadDocumentFonts(path);
        if (auto found = m_documents.find(document); found != m_documents.end())
        {
            DetachEventSubscriptions();
            if (found->second) found->second->Close();
            m_documents.erase(found);
            m_documentScales.erase(document);
            m_documentUsesBackdrop.erase(document);
            m_documentReferences.erase(document);
        }

        // LoadDocument caches parsed style sheets and templates globally.
        // Closing and reopening the RML alone would otherwise reuse the stale
        // RCSS object after the file watcher detects a change.
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();

        auto *loaded = m_context->LoadDocument(path);
        if (!loaded)
            return false;
        loaded->Show();
        m_documents[document] = loaded;
        m_documentReferences[document] = reference;
        m_documentUsesBackdrop[document] = SourceUsesBackdropFilter(path);
        std::error_code error;
        const auto writeTime = DocumentSourceWriteTime(path, error);
        if (!error) m_documentWriteTimes[document] = writeTime;
        std::clog << "[RmlUi] Hot reloaded document and styles for '" << document << "'.\n";
        AttachEventSubscriptions();
        if (auto target = m_worldSurfaceTargets.find(document); target != m_worldSurfaceTargets.end())
            target->second.dirty = true;
        return true;
    }

    void RmlUiRuntime::MarkWorldSurfaceDirty(Rml::ElementDocument *document)
    {
        if (!document)
            return;
        for (const auto &[key, loaded] : m_documents)
        {
            if (loaded != document)
                continue;
            if (auto target = m_worldSurfaceTargets.find(key); target != m_worldSurfaceTargets.end())
                target->second.dirty = true;
            return;
        }
    }

    bool RmlUiRuntime::SetElementText(const std::string &document, const std::string &id, const std::string &text)
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        if (!element) return false;
        if (element->GetInnerRML() == text)
            return true;
        element->SetInnerRML(text);
        MarkWorldSurfaceDirty(doc);
        return true;
    }

    std::string RmlUiRuntime::GetElementText(const std::string &document, const std::string &id) const
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        return element ? element->GetInnerRML() : std::string{};
    }

    bool RmlUiRuntime::SetElementAttribute(const std::string &document, const std::string &id,
                                           const std::string &name, const std::string &value)
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        if (!element) return false;
        if (element->GetAttribute<Rml::String>(name, {}) == value)
            return true;
        element->SetAttribute(name, value);
        MarkWorldSurfaceDirty(doc);
        return true;
    }

    std::string RmlUiRuntime::GetElementAttribute(const std::string &document, const std::string &id,
                                                  const std::string &name) const
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        return element ? element->GetAttribute<Rml::String>(name, {}) : std::string{};
    }

    bool RmlUiRuntime::SetElementClass(const std::string &document, const std::string &id,
                                       const std::string &name, bool enabled)
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        if (!element) return false;
        element->SetClass(name, enabled);
        MarkWorldSurfaceDirty(doc);
        return true;
    }

    bool RmlUiRuntime::SetElementStyle(const std::string &document, const std::string &id,
                                       const std::string &name, const std::string &value)
    {
        auto *doc = FindDocument(document);
        auto *element = doc ? doc->GetElementById(id) : nullptr;
        if (!element || !element->SetProperty(name, value))
            return false;
        MarkWorldSurfaceDirty(doc);
        return true;
    }

    bool RmlUiRuntime::SubscribeEvent(const std::string &document, const std::string &id, const std::string &event)
    {
        const std::string key = EventKey(document, id, event);
        if (m_attachedEvents.contains(key))
            return true;
        m_eventSubscriptions.insert(key);
        AttachEventSubscriptions();
        return m_attachedEvents.contains(key);
    }

    bool RmlUiRuntime::ConsumeEvent(const std::string &document, const std::string &id, const std::string &event)
    {
        const std::string key = EventKey(document, id, event);
        auto found = m_pendingEvents.find(key);
        if (found == m_pendingEvents.end() || found->second <= 0)
            return false;
        --found->second;
        return true;
    }

    void RmlUiRuntime::NotifyEvent(const std::string &key)
    {
        ++m_pendingEvents[key];
    }

    void RmlUiRuntime::NotifyEventListenerDetached(const std::string &key, Rml::Element *element)
    {
        const auto found = m_eventListenerElements.find(key);
        if (found != m_eventListenerElements.end() && found->second == element)
        {
            found->second = nullptr;
            m_attachedEvents.erase(key);
        }
    }

    void RmlUiRuntime::AttachEventSubscriptions()
    {
        for (const auto &key : m_eventSubscriptions)
        {
            if (m_attachedEvents.contains(key))
                continue;
            const auto first = key.find(kEventSeparator);
            const auto second = key.find(kEventSeparator, first + 1);
            if (first == std::string::npos || second == std::string::npos)
                continue;
            const std::string document = key.substr(0, first);
            const std::string id = key.substr(first + 1, second - first - 1);
            const std::string event = key.substr(second + 1);
            auto *doc = FindDocument(document);
            auto *element = doc ? doc->GetElementById(id) : nullptr;
            if (!element)
                continue;

            auto listener = m_eventListeners.find(key);
            if (listener == m_eventListeners.end())
            {
                listener = m_eventListeners.emplace(
                    key, std::make_unique<RuntimeEventListener>(*this, key)).first;
            }
            m_eventListenerElements[key] = element;
            element->AddEventListener(event, listener->second.get());
            m_attachedEvents.insert(key);
        }
    }

    void RmlUiRuntime::DetachEventSubscriptions()
    {
        for (const auto &[key, listener] : m_eventListeners)
        {
            const auto first = key.find(kEventSeparator);
            const auto second = key.find(kEventSeparator, first + 1);
            if (first == std::string::npos || second == std::string::npos)
                continue;

            const std::string event = key.substr(second + 1);
            const auto attachedElement = m_eventListenerElements.find(key);
            auto *element = attachedElement == m_eventListenerElements.end()
                                ? nullptr
                                : attachedElement->second;
            if (element)
                element->RemoveEventListener(event, listener.get());
        }
        m_eventListeners.clear();
        m_eventListenerElements.clear();
        m_attachedEvents.clear();
    }

    bool RmlUiRuntime::IsPointerInputCaptured() const
    {
        return m_context && m_context->IsMouseInteracting();
    }

    bool RmlUiRuntime::IsKeyboardInputCaptured() const
    {
        if (!m_context)
            return false;

        const auto *focusedElement = m_context->GetFocusElement();
        return focusedElement && focusedElement->IsVisible(true) &&
               rmlui_dynamic_cast<const Rml::ElementFormControl *>(focusedElement);
    }

    void RmlUiRuntime::ProcessInput(platform::Window &window, const scene::Scene &scene)
    {
        auto *glfwWindow = static_cast<GLFWwindow *>(window.GetWindow());
        if (!glfwWindow || !window.IsScriptInputEnabled())
            return;

        const auto &input = window.GetInputState();
        const int modifiers = CurrentModifiers(glfwWindow);

        int mouseX = static_cast<int>(input.mouseState.x);
        int mouseY = static_cast<int>(input.mouseState.y);
        glm::vec2 overrideCanvasSize{};
        glm::vec2 overrideMousePosition{};
        bool pointerInside = true;
        if (scene.GetRuntimeUIInputOverride(
                overrideCanvasSize, overrideMousePosition, pointerInside))
        {
            if (pointerInside)
            {
                mouseX = static_cast<int>(std::lround(overrideMousePosition.x));
                // Native runtime UI uses a bottom-left origin. RmlUi uses the
                // window/HTML convention with its origin at the top-left.
                mouseY = static_cast<int>(std::lround(
                    overrideCanvasSize.y - overrideMousePosition.y));
            }
            else
            {
                mouseX = -1;
                mouseY = -1;
            }
        }
        m_context->ProcessMouseMove(mouseX, mouseY, modifiers);

        for (int button = 0; button < 8; ++button)
        {
            if (input.IsMouseButtonPressed(static_cast<std::uint16_t>(button)))
                m_context->ProcessMouseButtonDown(button, modifiers);
            if (input.IsMouseButtonReleased(static_cast<std::uint16_t>(button)))
                m_context->ProcessMouseButtonUp(button, modifiers);
        }
        if (input.mouseState.scrollDeltaX != 0.0 || input.mouseState.scrollDeltaY != 0.0)
        {
            m_context->ProcessMouseWheel(
                {-static_cast<float>(input.mouseState.scrollDeltaX),
                 -static_cast<float>(input.mouseState.scrollDeltaY)},
                modifiers);
        }

        for (int key = 0; key < static_cast<int>(input.keys.size()); ++key)
        {
            const auto identifier = RmlGLFW::ConvertKey(key);
            if (identifier == Rml::Input::KI_UNKNOWN)
                continue;
            if (input.keys[key] && !input.previousKeys[key])
            {
                if (identifier == Rml::Input::KI_TAB)
                {
                    Rml::Element *previous = m_context->GetFocusElement();
                    m_context->ProcessKeyDown(identifier, modifiers);
                    Rml::Element *current = m_context->GetFocusElement();
                    if (current != previous)
                    {
                        if (auto *textInput = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(current))
                            textInput->Select();
                    }
                }
                else
                {
                    m_context->ProcessKeyDown(identifier, modifiers);
                }
            }
            if (!input.keys[key] && input.previousKeys[key])
                m_context->ProcessKeyUp(identifier, modifiers);
        }
        for (const int key : input.repeatedKeys)
        {
            const auto identifier = RmlGLFW::ConvertKey(key);
            if (identifier != Rml::Input::KI_UNKNOWN)
                m_context->ProcessKeyDown(identifier, modifiers);
        }
        for (const auto codepoint : input.textInput)
            m_context->ProcessTextInput(static_cast<Rml::Character>(codepoint));
    }

    void RmlUiRuntime::Render(const scene::Scene &scene, int width, int height, std::uint64_t frameSequence,
                              const glm::mat4 &view, const glm::mat4 &projection,
                              const std::function<void()> &drawWorldSurfaces)
    {
        using Clock = std::chrono::steady_clock;
        const auto elapsedMs = [](const auto begin, const auto end)
        {
            return std::chrono::duration<float, std::milli>(end - begin).count();
        };
        m_cpuTiming = {};

        auto &window = core::Engine::GetInstance().GetWindow();
        const auto initializeBegin = Clock::now();
        if (!Initialize(window))
        {
            m_cpuTiming.initializeMs = elapsedMs(initializeBegin, Clock::now());
            return;
        }
        m_cpuTiming.initializeMs = elapsedMs(initializeBegin, Clock::now());

        const auto resizeBegin = Clock::now();
        if (width != m_width || height != m_height)
        {
            m_width = width;
            m_height = height;
            m_context->SetDimensions({width, height});
            m_renderer->SetViewport(width, height);
            m_documentScales.clear();
        }
        m_cpuTiming.resizeMs = elapsedMs(resizeBegin, Clock::now());

        const auto synchronizeBegin = Clock::now();
        SynchronizeDocuments(scene, view, projection);
        m_cpuTiming.synchronizeMs = elapsedMs(synchronizeBegin, Clock::now());
        m_cpuTiming.documentCount = static_cast<int>(m_documents.size());
        m_cpuTiming.visibleDocumentCount = static_cast<int>(std::count_if(
            m_documents.begin(), m_documents.end(), [](const auto &entry)
            {
                return entry.second && entry.second->IsVisible();
            }));
        if (m_documents.empty())
            return;

        const auto inputBegin = Clock::now();
        if (frameSequence != m_lastInputFrame)
        {
            ProcessInput(window, scene);
            m_context->Update();
            m_lastInputFrame = frameSequence;
        }
        m_cpuTiming.inputUpdateMs = elapsedMs(inputBegin, Clock::now());

        const auto worldSurfaceBegin = Clock::now();
        RenderWorldSurfaces();
        m_cpuTiming.worldSurfaceMs = elapsedMs(worldSurfaceBegin, Clock::now());
        if (drawWorldSurfaces && !m_worldSurfaceDraws.empty())
            drawWorldSurfaces();

        m_renderer->SetViewport(width, height);
        const auto beginFrameBegin = Clock::now();
        m_renderer->BeginFrame();
        m_cpuTiming.beginFrameMs = elapsedMs(beginFrameBegin, Clock::now());
        const bool needsBackdrop = std::any_of(m_documents.begin(), m_documents.end(), [this](const auto &entry)
        {
            const auto usage = m_documentUsesBackdrop.find(entry.first);
            return entry.second && entry.second->IsVisible() &&
                   usage != m_documentUsesBackdrop.end() && usage->second;
        });
        const auto backdropBegin = Clock::now();
        if (needsBackdrop)
            PlutoGE_CopyRmlUiBackdrop(width, height);
        m_cpuTiming.backdropMs = elapsedMs(backdropBegin, Clock::now());
        m_cpuTiming.copiedBackdrop = needsBackdrop;

        const auto renderBegin = Clock::now();
        m_context->Render();
        m_cpuTiming.renderMs = elapsedMs(renderBegin, Clock::now());

        const auto endFrameBegin = Clock::now();
        m_renderer->EndFrame();
        m_cpuTiming.endFrameMs = elapsedMs(endFrameBegin, Clock::now());
    }
}
