#pragma once

#include <glad/glad.h>
#include <string>
#include <iostream>

namespace PlutoGE::render
{
    struct ShaderSource
    {
        std::string vertexSource;
        std::string fragmentSource;
    };

    struct ShaderConfig
    {
        // Future configuration options can be added here
        std::string vertexShaderPath;
        std::string fragmentShaderPath;
    };

    class Shader
    {
    public:
        Shader() = default;
        ~Shader() = default;

        static Shader *Create(const ShaderConfig &config);
        static Shader *Create(const ShaderSource &source);

        static Shader *CreateDefault()
        {
            ShaderSource defaultSource;

            defaultSource.vertexSource = R"(
                #version 330 core
                layout(location = 0) in vec3 aPos;
                layout(location = 1) in vec3 aNormal;
                layout(location = 2) in vec2 aUV;
                layout(location = 3) in vec4 aTangent;

                uniform mat4 uModel;
                uniform mat4 uView;
                uniform mat4 uProjection;

                out vec3 FragPos;
                out vec3 Normal;
                out vec2 UV;

                void main()
                {
                    FragPos = vec3(uModel * vec4(aPos, 1.0));
                    Normal = mat3(transpose(inverse(uModel))) * aNormal; // Transform normal to world space
                    UV = aUV;
                    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
                }
            )";

            defaultSource.fragmentSource = R"(
                #version 330 core
                out vec4 FragColor;

                in vec3 FragPos;
                in vec3 Normal;
                in vec2 UV;

                uniform vec3 uColor;

                void main()
                {
                    // Simple diffuse lighting
                    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.6)); // Example light direction
                    float diff = max(dot(Normal, lightDir), 0.0);
                    vec3 diffuse = diff * uColor;

                    FragColor = vec4(diffuse, 1.0);
                }
            )";

            return CreateShaderFromSource(defaultSource);
        }

    protected:
        friend class Graphics;
        GLuint GetProgramID() const { return m_programID; }

    private:
        static Shader *CreateShaderFromSource(const ShaderSource &source);

        ShaderConfig m_config;
        GLuint m_programID = 0; // OpenGL shader program ID
    };
}