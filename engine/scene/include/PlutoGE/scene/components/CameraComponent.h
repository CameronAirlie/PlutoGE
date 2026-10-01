#pragma once

#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace PlutoGE::render
{
    class Camera;
    struct CameraData;
}

namespace PlutoGE::scene
{
    // Base cameras render the scene; overlay cameras are composited on top of
    // the main base camera, in OverlayOrder (for example a weapon camera).
    enum class CameraRenderType
    {
        Base,
        Overlay,
    };

    class CameraComponent : public TypedComponent<CameraComponent>
    {
    public:
        explicit CameraComponent(render::Camera *camera = nullptr, bool createDefaultEffects = true);
        ~CameraComponent() override;

        void Update(float deltaTime) override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetCamera(render::Camera *camera) { m_camera.reset(camera); }
        render::Camera *GetCamera() const { return m_camera.get(); }
        void SetMainCamera(bool isMainCamera) { m_isMainCamera = isMainCamera; }
        bool IsMainCamera() const { return m_isMainCamera; }
        void SetPrimary(bool isPrimary) { SetMainCamera(isPrimary); }
        bool IsPrimary() const { return IsMainCamera(); }
        void SetRenderType(CameraRenderType renderType) { m_renderType = renderType; }
        CameraRenderType GetRenderType() const { return m_renderType; }
        bool IsOverlay() const { return m_renderType == CameraRenderType::Overlay; }
        // Overlays composite in ascending order; later overlays draw on top.
        void SetOverlayOrder(int order) { m_overlayOrder = order; }
        int GetOverlayOrder() const { return m_overlayOrder; }
        void SetTagFilter(CameraTagFilter filter) { m_tagFilter = std::move(filter); }
        const CameraTagFilter &GetTagFilter() const { return m_tagFilter; }

        render::CameraData GetCameraData(int width, int height) const;

        void AddPostProcessEffect(std::unique_ptr<render::IPostProcessEffect> effect);
        bool AddPostProcessEffectByType(std::string_view typeName);
        void ClearPostProcessEffects();
        bool RemovePostProcessEffect(size_t index);
        bool MovePostProcessEffect(size_t fromIndex, size_t toIndex);
        const std::vector<std::unique_ptr<render::IPostProcessEffect>> &GetPostProcessEffects() const { return m_postProcessEffects; }
        render::IPostProcessEffect *GetPostProcessEffect(size_t index);
        const render::IPostProcessEffect *GetPostProcessEffect(size_t index) const;
        bool SetPostProcessPresetAssetReference(std::string assetReference);
        const std::string &GetPostProcessPresetAssetReference() const { return m_postProcessPresetAssetReference; }

        template <typename TEffect, typename... TArgs>
        TEffect &EmplacePostProcessEffect(TArgs &&...args)
        {
            static_assert(std::is_base_of_v<render::IPostProcessEffect, TEffect>);

            auto effect = std::make_unique<TEffect>(std::forward<TArgs>(args)...);
            TEffect &effectRef = *effect;
            AddPostProcessEffect(std::move(effect));
            return effectRef;
        }

    private:
        std::unique_ptr<render::Camera> m_camera;
        bool m_isMainCamera = false;
        CameraRenderType m_renderType = CameraRenderType::Base;
        int m_overlayOrder = 0;
        CameraTagFilter m_tagFilter;
        std::vector<std::unique_ptr<render::IPostProcessEffect>> m_postProcessEffects;
        std::string m_postProcessPresetAssetReference;
    };
}
