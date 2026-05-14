#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"

#include <iostream>

namespace PlutoGE::render
{
    void Material::Bind(Shader *shader)
    {
        Shader *activeShader = shader ? shader : m_overrideShader;
        if (!activeShader)
        {
            std::cerr << "Material has no shader assigned!" << std::endl;
            return;
        }

        const auto setVec4 = [activeShader](const char *name, const glm::vec4 &value)
        {
            if (activeShader->HasUniform(name))
            {
                activeShader->SetUniform(name, value);
            }
        };

        const auto setFloat = [activeShader](const char *name, float value)
        {
            if (activeShader->HasUniform(name))
            {
                activeShader->SetUniform(name, value);
            }
        };

        const auto setInt = [activeShader](const char *name, int value)
        {
            if (activeShader->HasUniform(name))
            {
                activeShader->SetUniform(name, value);
            }
        };

        const auto setTexture = [activeShader](const char *name, Texture *texture, int slot)
        {
            if (texture && activeShader->HasUniform(name))
            {
                activeShader->SetUniform(name, texture, slot);
            }
        };

        setVec4("uColor", m_config.color);
        setFloat("uMetallicFactor", m_config.metallic);
        setFloat("uRoughnessFactor", m_config.roughness);
        setFloat("uFlipNormalY", m_config.flipNormalY ? 1.0f : 0.0f);

        // Set common uniforms (camera and model data)
        // shader->SetUniform("uModel", modelMatrix);
        // shader->SetUniform("uView", cameraData.view);
        // shader->SetUniform("uProjection", cameraData.projection);

        // Set material-specific uniforms
        if (m_config.albedoTexture)
        {
            setTexture("uAlbedoTexture", m_config.albedoTexture, 0);
            setFloat("uHasAlbedoTexture", 1.0f);
        }
        else
        {
            setFloat("uHasAlbedoTexture", 0.0f);
        }

        if (m_config.normalTexture)
        {
            setTexture("uNormalTexture", m_config.normalTexture, 1);
            setFloat("uHasNormalTexture", 1.0f);
        }
        else
        {
            setFloat("uHasNormalTexture", 0.0f);
        }

        if (m_config.metallicTexture)
        {
            setTexture("uMetallicTexture", m_config.metallicTexture, 2);
            setFloat("uHasMetallicTexture", 1.0f);
            setInt("uMetallicTextureChannel", static_cast<int>(m_config.metallicTextureChannel));
        }
        else
        {
            setFloat("uHasMetallicTexture", 0.0f);
        }

        if (m_config.roughnessTexture)
        {
            setTexture("uRoughnessTexture", m_config.roughnessTexture, 3);
            setFloat("uHasRoughnessTexture", 1.0f);
            setInt("uRoughnessTextureChannel", static_cast<int>(m_config.roughnessTextureChannel));
        }
        else
        {
            setFloat("uHasRoughnessTexture", 0.0f);
        }

        if (m_config.lightmapTexture)
        {
            setTexture("uLightmapTexture", m_config.lightmapTexture, 4);
            setFloat("uHasLightmapTexture", 1.0f);
        }
        else
        {
            setFloat("uHasLightmapTexture", 0.0f);
        }
    }
}