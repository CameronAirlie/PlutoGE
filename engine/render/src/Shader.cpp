#include <algorithm>
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
            // A significant amount of renderer code binds textures directly
            // with glActiveTexture/glBindTexture. Those calls cannot update this
            // private cache, so treating the cached active unit or texture ID as
            // authoritative can bind a material texture to the wrong unit or
            // skip the bind entirely. Progressive passes are especially exposed:
            // another pass changes unit 0 between chunks, then the next material
            // incorrectly samples a shadow map, G-buffer texture, or a previous
            // material as its albedo.
            //
            // Always establish both pieces of GL state here. Keep the cache
            // synchronized for diagnostics and for callers of ResetStateCache,
            // but do not use it to elide texture binds until all direct binding
            // sites have been routed through one shared state tracker.
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(textureType, textureId);
            cache.activeTextureSlot = slot;

            if (slot < static_cast<int>(textureUnits.size()))
            {
                const int textureCacheIndex = GetTextureCacheIndex(textureType);
                if (textureCacheIndex >= 0)
                {
                    textureUnits[slot].textureIds[textureCacheIndex] = textureId;
                }
            }
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
        const bool isCompute = !source.computeSource.empty();
        GLuint vertexShader = isCompute ? 0 : CompileShader(GL_VERTEX_SHADER, source.vertexSource.c_str());
        GLuint geometryShader = source.geometrySource.empty() ? 0 : CompileShader(GL_GEOMETRY_SHADER, source.geometrySource.c_str());
        GLuint fragmentShader = source.fragmentSource.empty() ? 0 : CompileShader(GL_FRAGMENT_SHADER, source.fragmentSource.c_str());
        GLuint computeShader = isCompute ? CompileShader(GL_COMPUTE_SHADER, source.computeSource.c_str()) : 0;

        if ((!isCompute && vertexShader == 0) || (isCompute && computeShader == 0) || (!source.geometrySource.empty() && geometryShader == 0) || (!source.fragmentSource.empty() && fragmentShader == 0))
        {
            // Handle error: failed to compile shaders
            std::cerr << "Failed to compile shaders from source." << std::endl;
            return nullptr;
        }

        GLuint programID = glCreateProgram();
        if (vertexShader != 0)
            glAttachShader(programID, vertexShader);
        if (computeShader != 0)
            glAttachShader(programID, computeShader);
        if (geometryShader != 0)
        {
            glAttachShader(programID, geometryShader);
        }
        if (fragmentShader != 0)
        {
            glAttachShader(programID, fragmentShader);
        }

        std::vector<const char *> feedbackVaryings;
        feedbackVaryings.reserve(source.transformFeedbackVaryings.size());
        for (const auto &varying : source.transformFeedbackVaryings)
        {
            feedbackVaryings.push_back(varying.c_str());
        }
        if (!feedbackVaryings.empty())
        {
            glTransformFeedbackVaryings(programID,
                                        static_cast<GLsizei>(feedbackVaryings.size()),
                                        feedbackVaryings.data(),
                                        source.transformFeedbackBufferMode);
        }

        if (!LinkShaderProgram(programID, ShaderConfig{}))
        {
            std::cerr << "Failed to link shader program from source." << std::endl;
            return nullptr; // Linking failed, error already logged
        }

        // Clean up shaders as they are no longer needed after linking
        if (vertexShader != 0)
            glDeleteShader(vertexShader);
        if (geometryShader != 0)
        {
            glDeleteShader(geometryShader);
        }
        if (fragmentShader != 0)
        {
            glDeleteShader(fragmentShader);
        }
        if (computeShader != 0)
        {
            glDeleteShader(computeShader);
        }

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
        // Keep the queried texture-unit count across frames. Clearing the
        // vector forced a synchronous GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
        // query on the first material bind of every viewport render.
        std::fill(cache.textureUnits.begin(), cache.textureUnits.end(), TextureUnitState{});
    }

    GLint Shader::ResolveUniformLocation(std::string_view name, bool warnIfMissing) const
    {
        if (const auto cached = m_uniformLocationCache.find(name); cached != m_uniformLocationCache.end())
        {
            return cached->second;
        }

        std::string ownedName(name);
        GLint location = glGetUniformLocation(m_programID, ownedName.c_str());
        if (location == -1 && warnIfMissing)
        {
            std::cerr << "Warning: Uniform '" << name << "' not found in shader program." << std::endl;
        }
        m_uniformLocationCache.emplace(std::move(ownedName), location);
        return location;
    }

    GLuint Shader::GetUniformLocation(std::string_view name) const
    {
        return ResolveUniformLocation(name, true);
    }

    bool Shader::HasUniform(std::string_view name) const
    {
        return ResolveUniformLocation(name, false) != -1;
    }

    void Shader::SetUniform(std::string_view name, const glm::mat4 &value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
        }
    }

    void Shader::SetUniform(std::string_view name, const glm::vec4 &value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform4f(location, value.x, value.y, value.z, value.w);
        }
    }

    void Shader::SetUniform(std::string_view name, const glm::vec3 &value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform3f(location, value.x, value.y, value.z);
        }
    }

    void Shader::SetUniform(std::string_view name, const glm::vec2 &value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform2f(location, value.x, value.y);
        }
    }

    void Shader::SetUniform(std::string_view name, float value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform1f(location, value);
        }
    }

    void Shader::SetUniform(std::string_view name, int value) const
    {
        GLint location = GetUniformLocation(name);
        if (location != -1)
        {
            glUniform1i(location, value);
        }
    }

    void Shader::SetUniform(std::string_view name, const Texture *texture, int slot) const
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

    bool Shader::TrySetUniform(std::string_view name, const glm::vec4 &value) const
    {
        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        glUniform4f(location, value.x, value.y, value.z, value.w);
        return true;
    }

    bool Shader::TrySetUniform(std::string_view name, const glm::vec3 &value) const
    {
        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        glUniform3f(location, value.x, value.y, value.z);
        return true;
    }

    bool Shader::TrySetUniform(std::string_view name, float value) const
    {
        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        glUniform1f(location, value);
        return true;
    }

    bool Shader::TrySetUniform(std::string_view name, int value) const
    {
        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        glUniform1i(location, value);
        return true;
    }

    bool Shader::TrySetUniform(std::string_view name, const Texture *texture, int slot) const
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

    bool Shader::TrySetUniform(std::string_view name, const glm::vec2 &value) const
    {
        const GLint location = ResolveUniformLocation(name, false);
        if (location == -1)
        {
            return false;
        }

        glUniform2f(location, value.x, value.y);
        return true;
    }
}
