#pragma once

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
    class CameraComponent : public TypedComponent<CameraComponent>
    {
    public:
        explicit CameraComponent(render::Camera *camera = nullptr);
        ~CameraComponent() override;

        void Update(float deltaTime) override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetCamera(render::Camera *camera) { m_camera.reset(camera); }
        render::Camera *GetCamera() const { return m_camera.get(); }

        render::CameraData GetCameraData(int width, int height) const;

        void AddPostProcessEffect(std::unique_ptr<render::IPostProcessEffect> effect);
        bool AddPostProcessEffectByType(std::string_view typeName);
        void ClearPostProcessEffects();
        bool RemovePostProcessEffect(size_t index);
        bool MovePostProcessEffect(size_t fromIndex, size_t toIndex);
        const std::vector<std::unique_ptr<render::IPostProcessEffect>> &GetPostProcessEffects() const { return m_postProcessEffects; }
        render::IPostProcessEffect *GetPostProcessEffect(size_t index);
        const render::IPostProcessEffect *GetPostProcessEffect(size_t index) const;

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
        std::vector<std::unique_ptr<render::IPostProcessEffect>> m_postProcessEffects;
    };
}