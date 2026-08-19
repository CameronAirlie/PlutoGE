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
            activeShader->TrySetUniform(name, value);
        };

        const auto setFloat = [activeShader](const char *name, float value)
        {
            activeShader->TrySetUniform(name, value);
        };

        const auto setVec2 = [activeShader](const char *name, const glm::vec2 &value)
        {
            activeShader->TrySetUniform(name, value);
        };

        const auto setVec3 = [activeShader](const char *name, const glm::vec3 &value)
        {
            activeShader->TrySetUniform(name, value);
        };

        const auto setInt = [activeShader](const char *name, int value)
        {
            activeShader->TrySetUniform(name, value);
        };

        const auto setTexture = [activeShader](const char *name, Texture *texture, int slot)
        {
            activeShader->TrySetUniform(name, texture, slot);
        };

        setVec4("uColor", m_config.color);
        setInt("uSurfaceType", static_cast<int>(m_config.surfaceType));
        setInt("uAlphaMode", static_cast<int>(m_config.alphaMode));
        setInt("uTwoSided", m_config.twoSided ? 1 : 0);
        setFloat("uAlphaCutoff", m_config.alphaCutoff);
        setFloat("uMetallicFactor", m_config.metallic);
        setFloat("uRoughnessFactor", m_config.roughness);
        setFloat("uOcclusionStrength", glm::clamp(m_config.occlusionStrength, 0.0f, 1.0f));
        setVec3("uEmission", glm::max(m_config.emission, glm::vec3(0.0f)));
        setFloat("uSubsurfaceFactor", glm::clamp(m_config.subsurface, 0.0f, 1.0f));
        setVec3("uSubsurfaceColor", glm::max(m_config.subsurfaceColor, glm::vec3(0.0f)));
        setFloat("uSubsurfaceRadius", glm::max(m_config.subsurfaceRadius, 0.001f));
        setFloat("uFlipNormalY", m_config.flipNormalY ? 1.0f : 0.0f);
        setFloat("uTransmissionFactor", m_config.transmission);
        setFloat("uIor", m_config.ior);
        setFloat("uThickness", m_config.thickness);
        if (activeShader->HasUniform("uAttenuationColor"))
        {
            activeShader->SetUniform("uAttenuationColor", m_config.attenuationColor);
        }
        setFloat("uAttenuationDistance", m_config.attenuationDistance);
        setVec4("uLightmapUvTransform", m_config.lightmapUvTransform);

        // Set common uniforms (camera and model data)
        // shader->SetUniform("uModel", modelMatrix);
        // shader->SetUniform("uView", cameraData.view);
        // shader->SetUniform("uProjection", cameraData.projection);

        // Set material-specific uniforms
        if (activeShader->HasUniform("uUVScale"))
        {
            activeShader->SetUniform("uUVScale", m_config.uvScale);
        }

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

        if (m_config.occlusionTexture)
        {
            setTexture("uOcclusionTexture", m_config.occlusionTexture, 5);
            setFloat("uHasOcclusionTexture", 1.0f);
            setInt("uOcclusionTextureChannel", static_cast<int>(m_config.occlusionTextureChannel));
        }
        else
        {
            setFloat("uHasOcclusionTexture", 0.0f);
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
