#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/Resource.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>

#include <glm/gtc/matrix_transform.hpp>
#include <span>

namespace
{
    template <typename T, std::size_t Size>
    std::span<const std::byte> Bytes(const std::array<T, Size> &values)
    {
        return std::as_bytes(std::span(values));
    }

    std::string ReadText(const char *name)
    {
        std::ifstream input(std::filesystem::path(PLUTO_RHI_TEST_SHADER_DIR) / name, std::ios::binary);
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }
}

int main()
{
    using namespace PlutoGE;
    using namespace render::rhi;

    platform::Window window;
    if (!window.Create({.title = "PlutoGE OpenGL RHI Test", .width = 64, .height = 64, .resizable = false, .visible = false}))
        return 1;
    if (!window.EnsureOpenGLContextCurrent(true))
        return 2;

    {
        opengl::OpenGLDevice device;
        if (device.GetApi() != GraphicsApi::OpenGL)
            return 3;

        constexpr std::array<float, 18> vertices = {
             0.0f,  0.8f, 0.5f, 1.0f, 0.1f, 0.1f,
            -0.8f, -0.8f, 0.5f, 0.1f, 1.0f, 0.1f,
             0.8f, -0.8f, 0.5f, 0.1f, 0.1f, 1.0f,
        };
        constexpr std::array<std::uint32_t, 3> indices = {0, 1, 2};

        Buffer vertexBuffer(device, device.CreateBuffer({.size = sizeof(vertices), .usage = BufferUsage::Vertex, .debugName = "Test triangle vertices"}, Bytes(vertices)));
        Buffer indexBuffer(device, device.CreateBuffer({.size = sizeof(indices), .usage = BufferUsage::Index, .debugName = "Test triangle indices"}, Bytes(indices)));
        Texture color(device, device.CreateTexture({.width = 64, .height = 64, .format = Format::R8G8B8A8Unorm, .usage = TextureUsage::ColorAttachment, .debugName = "Test color"}));
        Texture depth(device, device.CreateTexture({.width = 64, .height = 64, .format = Format::D32Float, .usage = TextureUsage::DepthStencilAttachment, .debugName = "Test depth"}));

        GraphicsPipelineDescriptor pipelineDescriptor;
        pipelineDescriptor.vertexShader.glsl = R"(#version 430 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
out vec3 vertexColor;
void main() { gl_Position = vec4(position, 1.0); vertexColor = color; })";
        pipelineDescriptor.fragmentShader.glsl = R"(#version 430 core
in vec3 vertexColor;
layout(location=0) out vec4 outputColor;
void main() { outputColor = vec4(vertexColor, 1.0); })";
        pipelineDescriptor.vertexLayout = {
            .stride = 6 * sizeof(float),
            .attributes = {{0, Format::R32G32B32Float, 0}, {1, Format::R32G32B32Float, 3 * sizeof(float)}},
        };
        pipelineDescriptor.debugName = "RHI test pipeline";
        GraphicsPipeline pipeline(device, device.CreateGraphicsPipeline(pipelineDescriptor));

        auto &commands = device.GetImmediateContext();
        RenderingInfo renderingInfo;
        renderingInfo.colorAttachment = color.Get();
        renderingInfo.depthAttachment = depth.Get();
        renderingInfo.width = 64;
        renderingInfo.height = 64;
        renderingInfo.clearColorValue[3] = 1.0f;
        commands.BeginRendering(renderingInfo);
        commands.BindPipeline(pipeline.Get());
        commands.BindVertexBuffer(vertexBuffer.Get());
        commands.BindIndexBuffer(indexBuffer.Get());
        commands.DrawIndexed(3);
        commands.EndRendering();

        std::array<unsigned char, 4> centerPixel{};
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centerPixel.data());
        if (const GLenum error = glGetError(); error != GL_NO_ERROR)
        {
            std::cerr << "OpenGL error after RHI draw: " << error << '\n';
            return 4;
        }
        if (centerPixel[0] <= 10 && centerPixel[1] <= 10 && centerPixel[2] <= 10)
        {
            std::cerr << "RHI draw produced a blank center pixel\n";
            return 5;
        }

        render::BasicRenderer basicRenderer;
        render::BasicRendererShaderPackage shaders;
        shaders.vertex.glsl = ReadText("BasicLit.vertex.glsl");
        shaders.fragment.glsl = ReadText("BasicLit.fragment.glsl");
        shaders.shadowVertex.glsl = ReadText("DirectionalShadow.vertex.glsl");
        shaders.shadowFragment.glsl = ReadText("DirectionalShadow.fragment.glsl");
        const auto loadPostProcess = [&](render::BasicPostProcessEffectType type, const char *module)
        {
            auto &shader = shaders.postProcess[static_cast<std::size_t>(type)];
            shader.vertex.glsl = ReadText((std::string(module) + ".vertex.glsl").c_str());
            shader.fragment.glsl = ReadText((std::string(module) + ".fragment.glsl").c_str());
        };
        loadPostProcess(render::BasicPostProcessEffectType::ToneMapping, "ToneMapping");
        loadPostProcess(render::BasicPostProcessEffectType::GammaCorrection, "GammaCorrection");
        loadPostProcess(render::BasicPostProcessEffectType::FXAA, "FXAA");
        loadPostProcess(render::BasicPostProcessEffectType::ColorGrading, "ColorGrading");
        loadPostProcess(render::BasicPostProcessEffectType::ChromaticAberration, "ChromaticAberration");
        try
        {
            if (!basicRenderer.Initialize(device, shaders) || !basicRenderer.Resize(96, 64))
                return 6;
        }
        catch (const std::exception &error)
        {
            std::cerr << "BasicRenderer initialization failed: " << error.what() << '\n';
            return 6;
        }

        constexpr std::array<render::BasicVertex, 8> cubeVertices = {{
            {{{-0.5f, -0.5f, -0.5f}}, {{-0.577f, -0.577f, -0.577f}}, {{0.0f, 0.0f}}},
            {{{ 0.5f, -0.5f, -0.5f}}, {{ 0.577f, -0.577f, -0.577f}}, {{1.0f, 0.0f}}},
            {{{ 0.5f,  0.5f, -0.5f}}, {{ 0.577f,  0.577f, -0.577f}}, {{1.0f, 1.0f}}},
            {{{-0.5f,  0.5f, -0.5f}}, {{-0.577f,  0.577f, -0.577f}}, {{0.0f, 1.0f}}},
            {{{-0.5f, -0.5f,  0.5f}}, {{-0.577f, -0.577f,  0.577f}}, {{0.0f, 0.0f}}},
            {{{ 0.5f, -0.5f,  0.5f}}, {{ 0.577f, -0.577f,  0.577f}}, {{1.0f, 0.0f}}},
            {{{ 0.5f,  0.5f,  0.5f}}, {{ 0.577f,  0.577f,  0.577f}}, {{1.0f, 1.0f}}},
            {{{-0.5f,  0.5f,  0.5f}}, {{-0.577f,  0.577f,  0.577f}}, {{0.0f, 1.0f}}},
        }};
        constexpr std::array<std::uint32_t, 36> cubeIndices = {
            0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
            0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
            3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4,
        };
        auto cube = basicRenderer.CreateMesh({cubeVertices, cubeIndices});

        glm::mat4 projection(0.0f);
        constexpr float nearPlane = 0.1f;
        constexpr float farPlane = 100.0f;
        const float aspect = 96.0f / 64.0f;
        const float focalLength = 1.0f / glm::tan(glm::radians(50.0f) * 0.5f);
        projection[0][0] = focalLength / aspect;
        projection[1][1] = focalLength;
        projection[2][2] = nearPlane / (farPlane - nearPlane);
        projection[2][3] = -1.0f;
        projection[3][2] = farPlane * nearPlane / (farPlane - nearPlane);
        const glm::mat4 view = glm::lookAtRH(glm::vec3(2.5f, 1.8f, 3.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
        const std::array draws = {render::BasicDraw{&cube, glm::mat4(1.0f)}};
        basicRenderer.Render(projection * view, draws);

        centerPixel.fill(0);
        glReadPixels(48, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centerPixel.data());
        if (const GLenum error = glGetError(); error != GL_NO_ERROR)
        {
            std::cerr << "OpenGL error after BasicRenderer draw: " << error << '\n';
            return 7;
        }
        if (centerPixel[0] < 20 && centerPixel[1] < 20 && centerPixel[2] < 20)
        {
            std::cerr << "BasicRenderer cube produced a blank center pixel\n";
            return 8;
        }
        auto colorGrading = render::BasicPostProcessEffect{render::BasicPostProcessEffectType::ColorGrading};
        colorGrading.parameters[0] = {0.0f, 1.05f, 1.05f, 0.04f};
        colorGrading.parameters[1] = {0.0f, 0.05f, 0.0f, 1.0f};
        colorGrading.parameters[2] = {1.0f, 0.0f, 0.1f, 0.01f};
        auto chromaticAberration = render::BasicPostProcessEffect{render::BasicPostProcessEffectType::ChromaticAberration};
        chromaticAberration.parameters[0].x = 0.003f;
        const std::array postEffects{
            render::BasicPostProcessEffect{render::BasicPostProcessEffectType::ToneMapping, 1.0f, 2.2f},
            render::BasicPostProcessEffect{render::BasicPostProcessEffectType::GammaCorrection, 1.0f, 2.2f},
            render::BasicPostProcessEffect{render::BasicPostProcessEffectType::FXAA, 1.0f, 2.2f, 1},
            colorGrading,
            chromaticAberration,
        };
        basicRenderer.Render(projection * view, render::BasicLighting{}, draws, postEffects);
        centerPixel.fill(0);
        glReadPixels(48, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centerPixel.data());
        if (glGetError() != GL_NO_ERROR ||
            (centerPixel[0] < 20 && centerPixel[1] < 20 && centerPixel[2] < 20))
        {
            std::cerr << "Backend-neutral OpenGL post-process chain produced a blank image\n";
            return 10;
        }

        auto swapchain = device.CreateSwapchain({
            .nativeWindow = window.GetWindow(),
            .width = 64,
            .height = 64,
            .vSync = false,
        });
        if (!swapchain || swapchain->GetFormat() != Format::R8G8B8A8Srgb ||
            !swapchain->Resize(96, 64) || swapchain->GetWidth() != 96 || swapchain->GetHeight() != 64 ||
            !swapchain->Present(basicRenderer.GetColorTexture()))
        {
            std::cerr << "OpenGL swapchain failed to present an RHI texture\n";
            return 9;
        }
    }

    window.Close();
    return 0;
}
