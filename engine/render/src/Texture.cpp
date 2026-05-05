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

    Texture *Texture::DepthTexture(int width, int height)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().CreateDepthTexture(width, height);
        if (texture == nullptr)
        {
            delete texture;
            return nullptr; // Failed to create depth texture
        }

        return texture;
    }

    Texture *Texture::DepthCubemap(int width, int height)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().CreateDepthCubemap(width, height);
        if (texture == nullptr)
        {
            delete texture;
            return nullptr;
        }

        return texture;
    }
}