#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/ui/EditorDocument.h"
#include "PlutoGE/ui/UnsavedSwitchGuard.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <string>
#include <vector>
#include <cstdint>

namespace PlutoGE::ui
{
    class AnimationClipEditorPanel final : public Panel, public IEditorDocument
    {
    public:
        explicit AnimationClipEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~AnimationClipEditorPanel() override;
        void Render() override;

        // IEditorDocument
        [[nodiscard]] std::string GetDocumentName() const override;
        [[nodiscard]] bool HasUnsavedChanges() const override;
        bool SaveDocument(std::string &error) override;
        void DiscardChanges() override;
        void FocusDocument() override { Focus(); }

    private:
        void LoadClip(const std::string &reference);
        bool BeginPreview();
        void StopPreview();
        void UpdatePreview(bool advancePlayback);

        std::string m_loadedReference;
        render::AnimationClip m_clip;
        int m_currentFrame = 0;
        int m_firstFrame = 0;
        int m_selectedItem = -1;
        int m_selectedKey = -1;
        std::vector<std::string> m_expandedBones;
        std::vector<render::AnimationClip> m_previewOriginalClips;
        std::uint32_t m_previewEntityId = 0;
        int m_previewOriginalClipIndex = 0;
        float m_previewOriginalTime = 0.0f;
        float m_previewOriginalSpeed = 1.0f;
        bool m_previewOriginalPlaying = false;
        bool m_previewOriginalLooping = true;
        bool m_previewPlaying = false;
        bool m_previewLooping = true;
        std::size_t m_previewClipSignature = 0;
        bool m_expanded = true;
        bool m_dirty = false;
        UnsavedSwitchGuard m_switchGuard;
    };
}
