#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <string>

namespace PlutoGE::ui
{
    class AnimationClipEditorPanel final : public Panel
    {
    public:
        explicit AnimationClipEditorPanel(const PanelConfig &config) : Panel(config) {}
        void Render() override;

    private:
        void LoadActiveClip();

        std::string m_loadedReference;
        render::AnimationClip m_clip;
        int m_currentFrame = 0;
        int m_firstFrame = 0;
        int m_selectedEvent = -1;
        bool m_expanded = true;
        bool m_dirty = false;
    };
}
