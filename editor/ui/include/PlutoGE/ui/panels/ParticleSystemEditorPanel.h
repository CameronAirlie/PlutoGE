#pragma once

#include "PlutoGE/assets/ParticleSystemAsset.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <string>

namespace PlutoGE::ui
{
    class ParticleSystemEditorPanel : public Panel
    {
    public:
        ParticleSystemEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~ParticleSystemEditorPanel() override = default;

        void Render() override;

    private:
        void LoadActiveAsset();

        std::string m_loadedReference;
        assets::ParticleSystemAsset m_asset;
        bool m_dirty = false;
    };
}
