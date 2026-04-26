#include <iostream>

#include "PlutoGE/render/Shader.h"
#include "PlutoGE/core/Engine.h"

namespace PlutoGE::render
{
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
}