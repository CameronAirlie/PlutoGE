#pragma once
#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/scene/Timeline.h"

namespace PlutoGE::ui
{
    class SequencerEditorPanel final : public Panel
    {
    public:
        explicit SequencerEditorPanel(const PanelConfig &config) : Panel(config) {}
        void Render() override;
    private:
        scene::Timeline m_draft;
        std::string m_source, m_error;
        std::uint32_t m_owner = 0;
        std::uint64_t m_sceneRevision = 0;
        int m_frame = 0, m_firstFrame = 0, m_track = -1, m_key = -1;
        int m_dragTrack = -1, m_dragKey = -1;
        bool m_expanded = true, m_dirty = false;
    };
}
