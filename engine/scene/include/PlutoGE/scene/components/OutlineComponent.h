#pragma once

#include "PlutoGE/render/Outline.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/vec4.hpp>

#include <algorithm>

namespace PlutoGE::scene
{
    // Draws a screen-space outline around everything its entity renders (and, by default, everything its children
    // render): interaction highlights, objective markers, a picked target. Games add, enable and restyle it at run
    // time (docs/OUTLINES.md); built games and the editor's views draw it (render::BuildSceneOutline, OutlinePass).
    //
    // Every outline keeps its own style. Outlines with the same style share one layer of the outline pass, so a
    // frame pays for the distinct styles in use, up to render::MaxOutlineLayers; the editor selection draws on top.
    class OutlineComponent : public TypedComponent<OutlineComponent>
    {
    public:
        // Outlines are sampled at most this many output pixels out (SelectionOutline.slang kMaxRadius).
        static constexpr float MinWidth = render::MinOutlineWidth, MaxWidth = render::MaxOutlineWidth;

        void Update(float) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // Linear RGB and opacity.
        const glm::vec4 &GetColor() const { return m_color; }
        // Width in pixels of a 1080-line output; other output sizes scale it so the outline looks the same.
        float GetWidth() const { return m_width; }
        // Opacity multiplier where other geometry hides the outlined object: 0 never shows it through walls.
        float GetHiddenOpacity() const { return m_hiddenOpacity; }
        // True to outline the entity's descendants too (a pickup's visual child, a character's model).
        bool GetIncludeChildren() const { return m_includeChildren; }

        void SetColor(const glm::vec4 &color);
        void SetWidth(float width);
        void SetHiddenOpacity(float opacity);
        void SetIncludeChildren(bool include) { m_includeChildren = include; }

    private:
        glm::vec4 m_color{1.0f, 0.82f, 0.36f, 1.0f};
        float m_width = 2.0f;
        float m_hiddenOpacity = 0.0f;
        bool m_includeChildren = true;
    };
}
