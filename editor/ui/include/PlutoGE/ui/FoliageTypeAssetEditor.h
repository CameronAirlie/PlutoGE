#pragma once

#include "PlutoGE/scene/FoliageTypeAsset.h"

#include <string>

namespace PlutoGE::render
{
    class Mesh;
}

namespace PlutoGE::ui
{
    // The details of a .plutofoliage asset in the Content Browser: a draft edited with the shared sections, applied by
    // Save, which also updates every type in the open scene linked to the file.
    class FoliageTypeAssetEditor
    {
    public:
        void Render(const std::string &reference);

    private:
        void Load(const std::string &reference);

        std::string m_reference;
        scene::FoliageTypeAsset m_draft;
        scene::FoliageTypeAsset m_saved;
        std::string m_error;
        std::string m_meshReference;
        render::Mesh *m_mesh = nullptr;
    };
}
