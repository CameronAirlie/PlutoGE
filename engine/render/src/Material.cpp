#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"

#include <iostream>

namespace PlutoGE::render
{
    void Material::Bind(Shader *shader)
    {
        if (!shader)
        {
            std::cerr << "Material has no shader assigned!" << std::endl;
            return;
        }

        shader->Bind();

        const auto setVec4 = [shader](const char *name, const glm::vec4 &value)
        {
            if (shader->HasUniform(name))
            {
                shader->SetUniform(name, value);
            }
        };

        const auto setFloat = [shader](const char *name, float value)
        {
            if (shader->HasUniform(name))
            {
                shader->SetUniform(name, value);
            }
        };

        const auto setTexture = [shader](const char *name, Texture *texture, int slot)
        {
            if (texture && shader->HasUniform(name))
            {
                shader->SetUniform(name, texture, slot);
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
        }
        else
        {
            setFloat("uHasMetallicTexture", 0.0f);
        }

        if (m_config.roughnessTexture)
        {
            setTexture("uRoughnessTexture", m_config.roughnessTexture, 3);
            setFloat("uHasRoughnessTexture", 1.0f);
        }
        else
        {
            setFloat("uHasRoughnessTexture", 0.0f);
        }
    }
}