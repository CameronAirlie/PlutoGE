#pragma once

#include <glad/glad.h>

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The GLSL stages of one program, as ShaderArtifactLibrary loads them.
struct GlTestProgramSource
{
    std::string vertexSource;
    std::string geometrySource;
    std::string fragmentSource;
    std::string computeSource;
};

// Links generated GLSL for checks that drive OpenGL directly, owning the program.
class GlTestProgram
{
public:
    [[nodiscard]] static std::unique_ptr<GlTestProgram> Create(const GlTestProgramSource &source)
    {
        const std::pair<GLenum, const std::string *> stages[] = {
            {GL_VERTEX_SHADER, &source.vertexSource},
            {GL_GEOMETRY_SHADER, &source.geometrySource},
            {GL_FRAGMENT_SHADER, &source.fragmentSource},
            {GL_COMPUTE_SHADER, &source.computeSource},
        };
        const GLuint program = glCreateProgram();
        std::vector<GLuint> shaders;
        bool compiled = true;
        for (const auto &[stage, text] : stages)
        {
            if (text->empty())
                continue;
            const GLuint shader = glCreateShader(stage);
            const char *data = text->c_str();
            glShaderSource(shader, 1, &data, nullptr);
            glCompileShader(shader);
            GLint status = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (!status)
            {
                std::cerr << "GLSL compile failed: " << InfoLog(shader, glGetShaderiv, glGetShaderInfoLog) << '\n';
                compiled = false;
            }
            glAttachShader(program, shader);
            shaders.push_back(shader);
        }
        GLint linked = GL_FALSE;
        if (compiled && !shaders.empty())
        {
            glLinkProgram(program);
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (!linked)
                std::cerr << "GLSL link failed: " << InfoLog(program, glGetProgramiv, glGetProgramInfoLog) << '\n';
        }
        for (const GLuint shader : shaders)
        {
            glDetachShader(program, shader);
            glDeleteShader(shader);
        }
        if (!linked)
        {
            glDeleteProgram(program);
            return nullptr;
        }
        return std::unique_ptr<GlTestProgram>(new GlTestProgram(program));
    }

    ~GlTestProgram() { glDeleteProgram(m_program); }
    GlTestProgram(const GlTestProgram &) = delete;
    GlTestProgram &operator=(const GlTestProgram &) = delete;

    void Bind() const { glUseProgram(m_program); }

private:
    explicit GlTestProgram(GLuint program) : m_program(program) {}

    template <typename Query, typename Log>
    static std::string InfoLog(GLuint object, Query query, Log log)
    {
        GLint length = 0;
        query(object, GL_INFO_LOG_LENGTH, &length);
        std::string text(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        log(object, length, nullptr, text.data());
        return text;
    }

    GLuint m_program = 0;
};
