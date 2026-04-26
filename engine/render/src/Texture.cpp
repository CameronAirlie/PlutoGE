#include "PlutoGE/render/Texture.h"
#include "PlutoGE/core/Engine.h"

namespace PlutoGE::render
{
    Texture *Texture::LoadFromFile(const char *filePath)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().LoadTextureFromFile(filePath);
        if (texture == nullptr)
        {
            delete texture;
            return nullptr; // Failed to load texture
        }

        return texture;
    }
}