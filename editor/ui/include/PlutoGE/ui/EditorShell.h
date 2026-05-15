#pragma once

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/EditorProfiler.h"
#include "PlutoGE/ui/PanelManager.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PlutoGE::ui
{
    class EditorShell
    {
    public:
        struct EditorViewportCamera
        {
            render::Camera camera{render::CameraConfig{
                .fovY = 45.0f,
                .nearPlane = 0.1f,
                .farPlane = 100.0f,
            }};
            glm::vec3 position{0.0f, 2.0f, 6.0f};
            float yawDegrees = 0.0f;
            float pitchDegrees = 0.0f;
            std::vector<std::unique_ptr<render::IPostProcessEffect>> postProcessEffects;

            void AddPostProcessEffect(std::unique_ptr<render::IPostProcessEffect> effect)
            {
                if (!effect)
                {
                    return;
                }

                effect->Initialize();
                postProcessEffects.push_back(std::move(effect));
            }

            bool AddPostProcessEffectByType(std::string_view typeName)
            {
                auto effect = render::CreatePostProcessEffect(typeName);
                if (!effect)
                {
                    return false;
                }

                AddPostProcessEffect(std::move(effect));
                return true;
            }

            bool RemovePostProcessEffect(size_t index)
            {
                if (index >= postProcessEffects.size())
                {
                    return false;
                }

                postProcessEffects.erase(postProcessEffects.begin() + static_cast<std::ptrdiff_t>(index));
                return true;
            }

            bool MovePostProcessEffect(size_t fromIndex, size_t toIndex)
            {
                if (fromIndex >= postProcessEffects.size() || toIndex >= postProcessEffects.size() || fromIndex == toIndex)
                {
                    return false;
                }

                auto effect = std::move(postProcessEffects[fromIndex]);
                postProcessEffects.erase(postProcessEffects.begin() + static_cast<std::ptrdiff_t>(fromIndex));
                postProcessEffects.insert(postProcessEffects.begin() + static_cast<std::ptrdiff_t>(toIndex), std::move(effect));
                return true;
            }

            render::IPostProcessEffect *GetPostProcessEffect(size_t index)
            {
                if (index >= postProcessEffects.size())
                {
                    return nullptr;
                }

                return postProcessEffects[index].get();
            }

            const std::vector<std::unique_ptr<render::IPostProcessEffect>> &GetPostProcessEffects() const { return postProcessEffects; }
        };

        void Initialize();
        void Render();
        void Shutdown();

        [[nodiscard]] core::Engine &GetEngine() { return m_engine; }
        [[nodiscard]] PanelManager &GetPanelManager() { return m_panelManager; }
        [[nodiscard]] EditorProfiler &GetProfiler() { return m_profiler; }

        [[nodiscard]] static EditorShell &GetInstance()
        {
            static EditorShell instance;
            return instance;
        }

        [[nodiscard]] scene::Entity *GetSelectedEntity() { return m_selectedEntity; }
        void SetSelectedEntity(scene::Entity *entity)
        {
            m_selectedEntity = entity;
            m_isEditorCameraSelected = false;
        }
        void SelectEditorCamera()
        {
            m_selectedEntity = nullptr;
            m_isEditorCameraSelected = true;
        }
        [[nodiscard]] bool IsEditorCameraSelected() const { return m_isEditorCameraSelected; }
        [[nodiscard]] EditorViewportCamera &GetEditorCamera() { return m_editorCamera; }

    private:
        EditorShell() = default;
        ~EditorShell();

        core::Engine &m_engine = core::Engine::GetInstance();
        PanelManager m_panelManager;
        EditorProfiler m_profiler;

        scene::Entity *m_selectedEntity = nullptr;
        bool m_isEditorCameraSelected = false;
        EditorViewportCamera m_editorCamera;
        std::unique_ptr<scene::Scene> m_scene;
        std::unique_ptr<scene::SceneBakeTask> m_activeBakeTask;
        scene::SceneBakeSettings m_customBakeSettings = scene::SceneBakeSettings::BalancedPreview();
        std::string m_statusMessage;
    };
}