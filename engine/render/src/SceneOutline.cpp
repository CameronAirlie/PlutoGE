#include "PlutoGE/render/SceneOutline.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/OutlineComponent.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        void CollectHierarchy(const scene::Entity &entity, std::vector<std::uint32_t> &entities)
        {
            entities.push_back(entity.GetID());
            for (const auto *child : entity.GetChildren())
                if (child && child->IsActive())
                    CollectHierarchy(*child, entities);
        }

        OutlineStyle StyleOf(const scene::OutlineComponent &outline, float outputHeight)
        {
            const float scale = outputHeight > 0.0f ? outputHeight / OutlineReferenceHeight : 1.0f;
            return {.color = outline.GetColor(),
                    .width = std::clamp(outline.GetWidth() * scale, MinOutlineWidth, MaxOutlineWidth),
                    .hiddenOpacity = outline.GetHiddenOpacity()};
        }
    }

    void BuildSceneOutline(const scene::Scene *scene, float outputHeight, SceneOutline &outline, std::size_t maxLayers)
    {
        // Rebuilt in place: each reused layer keeps its entity storage from the last frame.
        std::size_t used = 0;
        const auto layerFor = [&](const OutlineStyle &style) -> OutlineLayer &
        {
            for (std::size_t index = 0; index < used; ++index)
                if (outline.layers[index].style == style)
                    return outline.layers[index];
            if (used == maxLayers)
                return outline.layers[used - 1];
            if (used == outline.layers.size())
                outline.layers.emplace_back();
            auto &layer = outline.layers[used++];
            layer.style = style;
            layer.entities.clear();
            return layer;
        };

        if (scene && maxLayers > 0)
            for (const auto *component : scene->GetOutlineComponents())
            {
                const auto *owner = component->GetOwner();
                if (!component->IsEnabled() || !owner || !owner->IsActive())
                    continue;
                auto &entities = layerFor(StyleOf(*component, outputHeight)).entities;
                if (component->GetIncludeChildren())
                    CollectHierarchy(*owner, entities);
                else
                    entities.push_back(owner->GetID());
            }
        outline.layers.resize(used);
    }
}
