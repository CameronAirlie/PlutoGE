#include <iostream>
#include <vector>

#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/core/Engine.h"

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kTexture2DCacheIndex = 0;
        constexpr int kTexture3DCacheIndex = 1;
        constexpr int kTextureCubeCacheIndex = 2;
        constexpr int kCachedTextureTargetCount = 3;

        struct TextureUnitState
        {
            std::array<GLuint, kCachedTextureTargetCount> textureIds{};
        };

        struct RenderStateCache
        {
            GLuint boundProgram = 0;
            int activeTextureSlot = -1;
            std::vector<TextureUnitState> textureUnits;
        };

        int GetTextureCacheIndex(GLenum textureType)
        {
            switch (textureType)
            {
            case GL_TEXTURE_2D:
                return kTexture2DCacheIndex;
            case GL_TEXTURE_3D:
                return kTexture3DCacheIndex;
            case GL_TEXTURE_CUBE_MAP:
                return kTextureCubeCacheIndex;
            default:
                return -1;
            }
        }

        RenderStateCache &GetRenderStateCache()
        {
            static RenderStateCache cache;
            return cache;
        }

        std::vector<TextureUnitState> &GetTextureUnitCache()
        {
            auto &cache = GetRenderStateCache();
            if (!cache.textureUnits.empty())
            {
                return cache.textureUnits;
            }

            GLint unitCount = 0;
            glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &unitCount);
            if (unitCount <= 0)
            {
                unitCount = 16;
            }

            cache.textureUnits.resize(static_cast<std::size_t>(unitCount));
            return cache.textureUnits;
        }

        void BindTextureUnit(GLenum textureType, GLuint textureId, int slot)
        {
            if (slot < 0)
            {
                return;
            }

            auto &cache = GetRenderStateCache();
            auto &textureUnits = GetTextureUnitCache();
            if (slot >= static_cast<int>(textureUnits.size()))
            {
                glActiveTexture(GL_TEXTURE0 + slot);
                glBindTexture(textureType, textureId);
                cache.activeTextureSlot = slot;
                return;
            }

            if (cache.activeTextureSlot != slot)
            {
                glActiveTexture(GL_TEXTURE0 + slot);
                cache.activeTextureSlot = slot;
            }

            const int textureCacheIndex = GetTextureCacheIndex(textureType);
            if (textureCacheIndex < 0)
            {
                glBindTexture(textureType, textureId);
                return;
            }

            auto &unitState = textureUnits[slot];
            if (unitState.textureIds[textureCacheIndex] == textureId)
            {
                return;
            }

            glBindTexture(textureType, textureId);
            unitState.textureIds[textureCacheIndex] = textureId;
        }
    }

    // Helper: Compile shader
    GLuint CompileShader(GLenum type, const char *src)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "Shader compilation failed: " << infoLog << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    bool LinkShaderProgram(GLuint programID, const ShaderConfig &config)
    {
        glLinkProgram(programID);

        // Check for linking errors
        GLint success;
        glGetProgramiv(programID, GL_LINK_STATUS, &success);
        if (!success)
        {
            char infoLog[512];
            glGetProgramInfoLog(programID, 512, nullptr, infoLog);
            std::cerr << "Shader program linking failed: " << infoLog << std::endl
                      << "Vertex Shader Path: " << config.vertexShaderPath << std::endl
                      << "Fragment Shader Path: " << config.fragmentShaderPath << std::endl;
            glDeleteProgram(programID);
            return false;
        }
        return true;
    }

    // Helper function to create shader from source
    Shader *Shader::CreateShaderFromSource(const ShaderSource &source)
    {
        GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, source.vertexSource.c_str());
        GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, source.fragmentSource.c_str());

        if (vertexShader == 0 || fragmentShader == 0)
        {
            // Handle error: failed to compile shaders
            std::cerr << "Failed to compile shaders from source." << std::endl;
            return nullptr;
        }

        GLuint programID = glCreateProgram();
        glAttachShader(programID, vertexShader);
        glAttachShader(programID, fragmentShader);

        if (!LinkShaderProgram(programID, ShaderConfig{}))
        {
            std::cerr << "Failed to link shader program from source." << std::endl;
            return nullptr; // Linking failed, error already logged
        }

        // Clean up shaders as they are no longer needed after linking
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        Shader *shader = new Shader();
        shader->m_programID = programID;
        return shader;
    }

    Shader *Shader::Create(const ShaderConfig &config)
    {
        auto &engine = core::Engine::GetInstance();
        ShaderSource source = engine.GetAssetManager().LoadShader(config.vertexShaderPath.c_str(), config.fragmentShaderPath.c_str());

        if (source.vertexSource.empty() || source.fragmentSource.empty())
        {
            // Handle error: failed to load shader source
            std::cerr << "Failed to load shader source for paths: " << config.vertexShaderPath << ", " << config.fragmentShaderPath << std::endl;
            return nullptr;
        }

        return CreateShaderFromSource(source);
    }

    Shader *Shader::Create(const ShaderSource &source)
    {
        return CreateShaderFromSource(source);
    }

    void Shader::Bind() const
    {
        if (m_programID == 0)
        {
            std::cerr << "Error: Attempting to bind an uninitialized shader!" << std::endl;
            return;
        }

        auto &cache = GetRenderStateCache();
        if (cache.boundProgram == m_programID)
        {
            return;
        }

        glUseProgram(m_programID);
        cache.boundProgram = m_programID;
    }

    void Shader::Unbind() const
    {
        auto &cache = GetRenderStateCache();
        if (cache.boundProgram == 0)
        {
            return;
        }

        glUseProgram(0);
        cache.boundProgram = 0;
    }

    void Shader::ResetStateCache()
    {
        auto &cache = GetRenderStateCache();
        cache.boundProgram = 0;
        cache.activeTextureSlot = -1;
        cache.textureUnits.clear();
    }

    GLint Shader::ResolveUniformLocation(const std::string &name, bool warnIfMissing) const
    {
        if (m_uniformLocationCache.find(name) != m_uniformLocationCache.end())
        {
            return m_uniformLocationCache[name];
        }

        GLint location = glGetUniformLocation(m_programID, name.c_str());
        if (location == -1 && warnIfMissing)
        {
            std::cerr << "Warning: Uniform '" << name << "' not found in shader program." << std::endl;
        }
        m_uniformLocationCache[name] = location;
        return location;
    }

    GLuint Shader::GetUniformLocation(const std::string &name) const
    {
        return ResolveUniformLocation(name, true);
    }

    bool Shader::HasUniform(const std::string &name) const
    {
        return ResolveUniformLocation(name, false) != -1;
    }

    void Shader::SetUniform(const std::string &name, const glm::mat4 &value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
        }
    }

    void Shader::SetUniform(const std::string &name, const glm::vec4 &value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform4f(location, value.x, value.y, value.z, value.w);
        }
    }

    void Shader::SetUniform(const std::string &name, const glm::vec3 &value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform3f(location, value.x, value.y, value.z);
        }
    }

    void Shader::SetUniform(const std::string &name, const glm::vec2 &value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform2f(location, value.x, value.y);
        }
    }

    void Shader::SetUniform(const std::string &name, float value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform1f(location, value);
        }
    }

    void Shader::SetUniform(const std::string &name, int value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform1i(location, value);
        }
    }

    void Shader::SetUniform(const std::string &name, const Texture *texture, int slot) const
    {
        if (!texture)
        {
            std::cerr << "Error: Attempting to set uniform '" << name << "' with a null texture." << std::endl;
            return;
        }

        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            BindTextureUnit(texture->GetType(), texture->GetTextureID(), slot);
            glUniform1i(location, slot);
        }
    }

    bool Shader::TrySetUniform(const std::string &name, const glm::vec4 &value) const
    {
        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        glUniform4f(location, value.x, value.y, value.z, value.w);
        return true;
    }

    bool Shader::TrySetUniform(const std::string &name, float value) const
    {
        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        glUniform1f(location, value);
        return true;
    }

    bool Shader::TrySetUniform(const std::string &name, int value) const
    {
        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        glUniform1i(location, value);
        return true;
    }

    bool Shader::TrySetUniform(const std::string &name, const Texture *texture, int slot) const
    {
        if (!texture)
        {
            return false;
        }

        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        BindTextureUnit(texture->GetType(), texture->GetTextureID(), slot);
        glUniform1i(location, slot);
        return true;
    }
}