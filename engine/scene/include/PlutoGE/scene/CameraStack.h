#pragma once

#include "PlutoGE/render/RenderCommandView.h"
#include "PlutoGE/render/CameraView.h"
#include "PlutoGE/render/RhiRenderTextureRenderer.h"
#include "PlutoGE/scene/Entity.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    class IPostProcessEffect;
}

namespace PlutoGE::scene
{
    class CameraComponent;
    class CameraTagFilter;
    struct Light;
    class Scene;

    // Ignore tags always exclude lights. Include tags select lights only when
    // the camera enables FilterLightsByTags.
    [[nodiscard]] std::vector<Light *> CollectCameraLights(const Scene &scene, const CameraComponent &camera);

    // The cameras that produce one frame: a base camera plus the overlay
    // cameras composited over it, and the cameras rendering into textures.
    struct CameraStack
    {
        CameraComponent *base = nullptr;
        // Ascending overlay order; ties keep scene hierarchy order.
        std::vector<CameraComponent *> overlays;
        // Rendered before the screen views, in hierarchy order. Independent of
        // the base camera; never part of the on-screen stack.
        std::vector<CameraComponent *> textureCameras;

        explicit operator bool() const noexcept { return base != nullptr; }
    };

    // Considers enabled cameras on active entities. Cameras with a target
    // texture render offscreen. Of the rest, the base camera is the main base
    // camera, falling back to the first base camera in hierarchy order.
    // Overlays are ignored when the scene has no base camera.
    [[nodiscard]] CameraStack ResolveCameraStack(const Scene &scene);

    // Narrows a frame's render commands to those a camera's tag filter accepts.
    // Keep one instance per filtered list and reuse it to avoid reallocation.
    class CameraCommandFilter
    {
    public:
        // Returns `commands` itself when the filter accepts everything. Otherwise
        // the view borrows `commands` and this object until the next Apply.
        [[nodiscard]] render::RenderCommandView Apply(const Scene &scene, const CameraTagFilter &filter,
                                                      render::RenderCommandView commands);

    private:
        std::vector<const render::RenderCommand *> m_accepted;
        std::unordered_map<EntityID, bool> m_ownerAcceptance;
    };

    // Storage behind the view built for one camera in a frame.
    struct CameraViewStorage
    {
        CameraCommandFilter filter;
        std::vector<render::IPostProcessEffect *> postProcessEffects;
        std::vector<Light *> lights;
    };

    // Translates a stack's overlay cameras into render layers for one frame.
    class CameraOverlayLayerBuilder
    {
    public:
        // The returned layers borrow `commands` and this object until the next Build.
        [[nodiscard]] std::span<const render::CameraView> Build(
            const Scene &scene, std::span<CameraComponent *const> overlays,
            render::RenderCommandView commands, int width, int height);

    private:
        std::vector<CameraViewStorage> m_storage;
        std::vector<render::CameraView> m_layers;
    };

    // Translates a stack's texture cameras into render texture views for one
    // frame. Each camera renders at its target texture's size.
    class RenderTextureViewBuilder
    {
    public:
        // The returned views borrow `commands` and this object until the next Build.
        [[nodiscard]] std::span<const render::RenderTextureView> Build(
            const Scene &scene, std::span<CameraComponent *const> textureCameras,
            render::RenderCommandView commands);

    private:
        std::vector<CameraViewStorage> m_storage;
        std::vector<render::RenderTextureView> m_views;
    };
}
