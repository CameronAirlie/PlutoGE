#pragma once

#include "PlutoGE/scene/ui/UIGraphic.h"

#include <filesystem>
#include <memory>
#include <string>

namespace PlutoGE::render
{
    class Texture;
}

namespace PlutoGE::scene
{
    // Resolves a UI asset reference (asset://, project or engine reference,
    // or a plain path) to a file path through the engine's asset manager.
    [[nodiscard]] std::filesystem::path ResolveUIAssetPath(const std::string &reference);

    // Loads the texture an image references (cached by the texture manager).
    [[nodiscard]] render::Texture *LoadUITexture(const std::string &reference);

    // Texture sizes from the engine texture manager.
    [[nodiscard]] std::shared_ptr<IUITextureInfo> CreateEngineTextureInfo();

    // Points the shared font library at the engine's asset resolution. Safe
    // to call repeatedly.
    void ConfigureUIFontsForEngine();
}
