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
        Texture auxiliaryColor(device, device.CreateTexture({.width = 64, .height = 64, .format = Format::R8G8B8A8Unorm, .usage = TextureUsage::ColorAttachment, .debugName = "Test auxiliary color"}));
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
layout(location=1) out vec4 auxiliaryColor;
void main() { outputColor = vec4(vertexColor, 1.0); auxiliaryColor = vec4(1.0 - vertexColor, 1.0); })";
        pipelineDescriptor.colorFormats = {Format::R8G8B8A8Unorm, Format::R8G8B8A8Unorm};
        pipelineDescriptor.vertexLayout = {
            .stride = 6 * sizeof(float),
            .attributes = {{0, Format::R32G32B32Float, 0}, {1, Format::R32G32B32Float, 3 * sizeof(float)}},
        };
        pipelineDescriptor.debugName = "RHI test pipeline";
        GraphicsPipeline pipeline(device, device.CreateGraphicsPipeline(pipelineDescriptor));

        auto &commands = device.GetImmediateContext();
        RenderingInfo renderingInfo;
        renderingInfo.colorAttachments = {color.Get(), auxiliaryColor.Get()};
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
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        centerPixel.fill(0);
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centerPixel.data());
        if (glGetError() != GL_NO_ERROR || (centerPixel[0] <= 10 && centerPixel[1] <= 10 && centerPixel[2] <= 10))
        {
            std::cerr << "OpenGL RHI second color attachment was not written\n";
            return 11;
        }
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        render::BasicRenderer basicRenderer;
        render::BasicRendererShaderPackage shaders;
        shaders.vertex.glsl = ReadText("BasicLit.vertex.glsl");
        shaders.instancedVertex.glsl = ReadText("BasicLitInstanced.vertex.glsl");
        shaders.fragment.glsl = ReadText("BasicLit.fragment.glsl");
        shaders.shadowVertex.glsl = ReadText("DirectionalShadow.vertex.glsl");
        shaders.shadowInstancedVertex.glsl = ReadText("DirectionalShadowInstanced.vertex.glsl");
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
        loadPostProcess(render::BasicPostProcessEffectType::LensFlare, "LensFlare");
        loadPostProcess(render::BasicPostProcessEffectType::MotionBlur, "MotionBlur");
        constexpr std::array<const char *, 4> bloomModules{
            "BloomPrefilter", "BloomDownsample", "BloomUpsample", "BloomComposite"};
        for (std::size_t index = 0; index < bloomModules.size(); ++index)
        {
            shaders.bloom[index].vertex.glsl = ReadText((std::string(bloomModules[index]) + ".vertex.glsl").c_str());
            shaders.bloom[index].fragment.glsl = ReadText((std::string(bloomModules[index]) + ".fragment.glsl").c_str());
        }
        shaders.vctCompute[0].glsl = ReadText("VCTResolve.compute.glsl");
        shaders.vctCompute[1].glsl = ReadText("VCTDirectionalMip.compute.glsl");
        shaders.vctVoxelization.vertexShader.glsl = ReadText("VCTVoxelize.vertex.glsl");
        shaders.vctVoxelization.geometryShader.glsl = ReadText("VCTVoxelize.geometry.glsl");
        shaders.vctVoxelization.fragmentShader.glsl = ReadText("VCTVoxelize.fragment.glsl");
        constexpr std::array<const char *, 3> vctModules{"VCTConeTrace", "VCTTemporal", "VCTMetadata"};
        for (std::size_t index = 0; index < vctModules.size(); ++index)
        {
            shaders.vctPostProcess[index].vertex.glsl = ReadText((std::string(vctModules[index]) + ".vertex.glsl").c_str());
            shaders.vctPostProcess[index].fragment.glsl = ReadText((std::string(vctModules[index]) + ".fragment.glsl").c_str());
        }
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
        const auto instances = std::make_shared<const std::vector<glm::mat4>>(
            std::vector<glm::mat4>{glm::mat4(1.0f),
                glm::translate(glm::mat4(1.0f), glm::vec3(0.75f, 0.0f, 0.0f))});
        const std::array draws = {render::BasicDraw{.mesh = &cube, .instanceModels = instances}};
        // Model the shared editor context after a clipped ImGui draw. The RHI
        // render pass must establish its own raster state rather than inherit
        // a zero-area UI scissor.
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 0, 0);
        basicRenderer.Render(projection * view, draws);
        if (basicRenderer.GetFrameStats().geometryDraws != 1 ||
            basicRenderer.GetFrameStats().geometryInstances != 2)
        {
            std::cerr << "OpenGL BasicRenderer did not batch geometry instances\n";
            return 12;
        }

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
        auto bloom = render::BasicPostProcessEffect{render::BasicPostProcessEffectType::Bloom};
        bloom.quality = 4;
        bloom.parameters[0] = {0.35f, 0.8f, 0.5f, 1.0f};
        auto lensFlare = render::BasicPostProcessEffect{render::BasicPostProcessEffectType::LensFlare};
        lensFlare.parameters[0] = {0.2f, 0.8f, 1.0f, 0.55f};
        auto motionBlur = render::BasicPostProcessEffect{render::BasicPostProcessEffectType::MotionBlur};
        motionBlur.parameters[0] = {1.0f, 0.5f, 20.0f, 0.35f};
        motionBlur.parameters[1].x = 1.0f;
        const std::array postEffects{
            bloom,
            lensFlare,
            motionBlur,
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
        auto vctgi = render::BasicPostProcessEffect{render::BasicPostProcessEffectType::VCTGI};
        vctgi.quality = 3;
        vctgi.parameters[0] = {16.0f, 1.0f, 0.5f, 32.0f};
        vctgi.parameters[1] = {0.1f, 0.9f, 0.05f, 0.8f};
        vctgi.parameters[2] = {32.0f, 1.0f, 1.0f, 1.0f};
        vctgi.parameters[3].w = 256.0f;
        try
        {
            basicRenderer.Render(projection * view, render::BasicLighting{}, draws,
                                 std::span(&vctgi, 1));
        }
        catch (const std::exception &error)
        {
            std::cerr << "OpenGL VCTGI execution failed: " << error.what() << '\n';
            return 12;
        }
        if (const GLenum error = glGetError(); error != GL_NO_ERROR)
        {
            std::cerr << "OpenGL VCTGI execution produced error: " << error << '\n';
            return 12;
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
