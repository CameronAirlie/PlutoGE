#pragma once

#include <unordered_map>
#include <string>
#include <glad/glad.h>

namespace PlutoGE::render
{
    class Texture;
    class TextureManager
    {
    public:
        TextureManager() = default;
        ~TextureManager() = default;

        Texture *LoadTextureFromFile(const char *filePath);

    private:
        GLuint m_nextTextureID = 1;                                // Start from 1 since 0 is reserved for "no texture"
        std::unordered_map<std::string, Texture *> m_textureCache; // Cache for loaded textures
    };
}