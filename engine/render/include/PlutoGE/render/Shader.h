#pragma once
#include "PlutoGE/render/ShaderGraph.h"

#include <glad/glad.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    struct ShaderSource
    {
        std::string vertexSource;
        std::string geometrySource;
        std::string fragmentSource;
        std::string computeSource;
        std::vector<std::string> transformFeedbackVaryings;
        GLenum transformFeedbackBufferMode = GL_INTERLEAVED_ATTRIBS;
    };

    class Texture;
    class Shader
    {
    public:
        Shader() = default;
        ~Shader();
        Shader(const Shader &) = delete;
        Shader &operator=(const Shader &) = delete;

        static Shader *Create(const ShaderSource &source);

        static Shader *CreateGeometryPassShader();
        static Shader *CreateLightingPassShader();
        static Shader *CreatePostProcessShader();
        static Shader *CreateShadowPassShader();
        static Shader *CreateTransparentPassShader();

        void Bind() const;

        void Unbind() const;

        static void ResetStateCache();

        bool HasUniform(std::string_view name) const;
        void SetUniform(std::string_view name, const glm::mat4 &value) const;
        void SetUniformMatrixArray(std::string_view name, const glm::mat4 *values, std::size_t count) const;
        void SetUniform(std::string_view name, const glm::vec4 &value) const;
        void SetUniform(std::string_view name, const glm::vec3 &value) const;
        void SetUniform(std::string_view name, const glm::vec2 &value) const;
        void SetUniform(std::string_view name, float value) const;
        void SetUniform(std::string_view name, int value) const;
        void SetUniform(std::string_view name, const Texture *texture, int slot) const;
        bool TrySetUniform(std::string_view name, const glm::vec4 &value) const;
        bool TrySetUniform(std::string_view name, const glm::vec3 &value) const;
        bool TrySetUniform(std::string_view name, float value) const;
        bool TrySetUniform(std::string_view name, int value) const;
        bool TrySetUniform(std::string_view name, const Texture *texture, int slot) const;

    protected:
        friend class Graphics;

    private:
        static Shader *CreateShaderFromSource(const ShaderSource &source);
        struct TransparentStringHash
        {
            using is_transparent = void;
            std::size_t operator()(std::string_view value) const noexcept
            {
                return std::hash<std::string_view>{}(value);
            }
        };

        GLint ResolveUniformLocation(std::string_view name, bool warnIfMissing) const;
        bool CacheUniformValue(GLint location, std::uint8_t type, const void *data, std::size_t size) const;

        struct CachedUniformValue
        {
            std::array<std::byte, sizeof(float) * 16> bytes{};
            std::uint8_t type = 0;
            std::uint8_t size = 0;
        };

        GLuint m_programID = 0; // OpenGL shader program ID
        mutable std::unordered_map<std::string, GLint, TransparentStringHash, std::equal_to<>> m_uniformLocationCache;
        mutable std::vector<CachedUniformValue> m_uniformValueCache;
        GLuint GetUniformLocation(std::string_view name) const;
    };
}
