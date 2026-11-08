#pragma once

#include "PlutoGE/assets/ParticleSystemAsset.h"
#include "PlutoGE/ui/EditorDocument.h"
#include "PlutoGE/ui/UnsavedSwitchGuard.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <string>

namespace PlutoGE::ui
{
    class ParticleSystemEditorPanel : public Panel, public IEditorDocument
    {
    public:
        ParticleSystemEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~ParticleSystemEditorPanel() override = default;

        void Render() override;
        void OnProjectChanged() override
        {
            m_loadedReference.clear();
            m_dirty = false;
            m_switchGuard.Reset();
        }

        // IEditorDocument
        [[nodiscard]] std::string GetDocumentName() const override;
        [[nodiscard]] bool HasUnsavedChanges() const override;
        bool SaveDocument(std::string &error) override;
        void DiscardChanges() override;
        void FocusDocument() override { Focus(); }

    private:
        void LoadAsset(const std::string &reference);

        std::string m_loadedReference;
        assets::ParticleSystemAsset m_asset;
        bool m_dirty = false;
        UnsavedSwitchGuard m_switchGuard;
    };
}
