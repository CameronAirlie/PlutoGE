#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
    std::vector<std::uint32_t> ReadSpirv(const char *name)
    {
        std::ifstream input(std::filesystem::path(PLUTO_RHI_TEST_SHADER_DIR) / name, std::ios::binary | std::ios::ate);
        if (!input)
            return {};
        const auto size = input.tellg();
        std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
        input.seekg(0);
        input.read(reinterpret_cast<char *>(words.data()), size);
        return words;
    }
}

int main()
{
    using namespace PlutoGE::render;
    try
    {
        rhi::vulkan::VulkanDevice device;
        BasicRendererShaderPackage shaders;
        shaders.vertex.spirv = ReadSpirv("BasicLit.vertex.spv");
        shaders.fragment.spirv = ReadSpirv("BasicLit.fragment.spv");
        shaders.shadowVertex.spirv = ReadSpirv("DirectionalShadow.vertex.spv");
        shaders.shadowFragment.spirv = ReadSpirv("DirectionalShadow.fragment.spv");
        shaders.displayOutput.vertex.spirv = ReadSpirv("DisplayOutput.vertex.spv");
        shaders.displayOutput.fragment.spirv = ReadSpirv("DisplayOutput.fragment.spv");
        const auto loadPostProcess = [&](BasicPostProcessEffectType type, const char *module)
        {
            auto &shader = shaders.postProcess[static_cast<std::size_t>(type)];
            shader.vertex.spirv = ReadSpirv((std::string(module) + ".vertex.spv").c_str());
            shader.fragment.spirv = ReadSpirv((std::string(module) + ".fragment.spv").c_str());
        };
        loadPostProcess(BasicPostProcessEffectType::ToneMapping, "ToneMapping");
        loadPostProcess(BasicPostProcessEffectType::GammaCorrection, "GammaCorrection");
        loadPostProcess(BasicPostProcessEffectType::FXAA, "FXAA");
        loadPostProcess(BasicPostProcessEffectType::ColorGrading, "ColorGrading");
        loadPostProcess(BasicPostProcessEffectType::ChromaticAberration, "ChromaticAberration");
        loadPostProcess(BasicPostProcessEffectType::LensFlare, "LensFlare");
        loadPostProcess(BasicPostProcessEffectType::MotionBlur, "MotionBlur");
        constexpr std::array<const char *, 4> bloomModules{
            "BloomPrefilter", "BloomDownsample", "BloomUpsample", "BloomComposite"};
        for (std::size_t index = 0; index < bloomModules.size(); ++index)
        {
            shaders.bloom[index].vertex.spirv = ReadSpirv((std::string(bloomModules[index]) + ".vertex.spv").c_str());
            shaders.bloom[index].fragment.spirv = ReadSpirv((std::string(bloomModules[index]) + ".fragment.spv").c_str());
        }
        constexpr std::array<const char *, 3> ssaoModules{
            "SSAO", "SSAOResolve", "SSAOComposite"};
        for (std::size_t index = 0; index < ssaoModules.size(); ++index)
        {
            shaders.ssao[index].vertex.spirv = ReadSpirv((std::string(ssaoModules[index]) + ".vertex.spv").c_str());
            shaders.ssao[index].fragment.spirv = ReadSpirv((std::string(ssaoModules[index]) + ".fragment.spv").c_str());
        }
        shaders.vctCompute[0].spirv = ReadSpirv("VCTResolve.compute.spv");
        shaders.vctCompute[1].spirv = ReadSpirv("VCTDirectionalMip.compute.spv");
        shaders.vctVoxelization.vertexShader.spirv = ReadSpirv("VCTVoxelize.vertex.spv");
        shaders.vctVoxelization.geometryShader.spirv = ReadSpirv("VCTVoxelize.geometry.spv");
        shaders.vctVoxelization.fragmentShader.spirv = ReadSpirv("VCTVoxelize.fragment.spv");
        constexpr std::array<const char *, 3> vctModules{"VCTConeTrace", "VCTTemporal", "VCTMetadata"};
        for (std::size_t index = 0; index < vctModules.size(); ++index)
        {
            shaders.vctPostProcess[index].vertex.spirv = ReadSpirv((std::string(vctModules[index]) + ".vertex.spv").c_str());
            shaders.vctPostProcess[index].fragment.spirv = ReadSpirv((std::string(vctModules[index]) + ".fragment.spv").c_str());
        }
        BasicRenderer renderer;
        if (!renderer.Initialize(device, shaders) || !renderer.Resize(96, 64))
            return 1;

        constexpr std::array<BasicVertex, 8> vertices = {{
            {{{-0.5f, -0.5f, -0.5f}}, {{-0.577f, -0.577f, -0.577f}}, {{0, 0}}},
            {{{0.5f, -0.5f, -0.5f}}, {{0.577f, -0.577f, -0.577f}}, {{1, 0}}},
            {{{0.5f, 0.5f, -0.5f}}, {{0.577f, 0.577f, -0.577f}}, {{1, 1}}},
            {{{-0.5f, 0.5f, -0.5f}}, {{-0.577f, 0.577f, -0.577f}}, {{0, 1}}},
            {{{-0.5f, -0.5f, 0.5f}}, {{-0.577f, -0.577f, 0.577f}}, {{0, 0}}},
            {{{0.5f, -0.5f, 0.5f}}, {{0.577f, -0.577f, 0.577f}}, {{1, 0}}},
            {{{0.5f, 0.5f, 0.5f}}, {{0.577f, 0.577f, 0.577f}}, {{1, 1}}},
            {{{-0.5f, 0.5f, 0.5f}}, {{-0.577f, 0.577f, 0.577f}}, {{0, 1}}},
        }};
        constexpr std::array<std::uint32_t, 36> indices = {
            0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
            1, 2, 6, 1, 6, 5, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4};
        auto cube = renderer.CreateMesh({vertices, indices});
        constexpr std::array<BasicVertex, 8> cornerVertices = {{
            {{{-2.0f, 0.0f, 0.0f}}, {{0, 1, 0}}, {{0, 0}}},
            {{{2.0f, 0.0f, 0.0f}}, {{0, 1, 0}}, {{1, 0}}},
            {{{2.0f, 0.0f, 2.0f}}, {{0, 1, 0}}, {{1, 1}}},
            {{{-2.0f, 0.0f, 2.0f}}, {{0, 1, 0}}, {{0, 1}}},
            {{{-2.0f, 0.0f, 0.0f}}, {{0, 0, 1}}, {{0, 0}}},
            {{{2.0f, 0.0f, 0.0f}}, {{0, 0, 1}}, {{1, 0}}},
            {{{2.0f, 3.0f, 0.0f}}, {{0, 0, 1}}, {{1, 1}}},
            {{{-2.0f, 3.0f, 0.0f}}, {{0, 0, 1}}, {{0, 1}}},
        }};
        constexpr std::array<std::uint32_t, 12> cornerIndices = {
            0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7};
        auto corner = renderer.CreateMesh({cornerVertices, cornerIndices});

        glm::mat4 projection(0.0f);
        constexpr float nearPlane = 0.1f, farPlane = 100.0f;
        const float focal = 1.0f / glm::tan(glm::radians(50.0f) * 0.5f);
        projection[0][0] = focal / (96.0f / 64.0f);
        projection[1][1] = focal;
        projection[2][2] = nearPlane / (farPlane - nearPlane);
        projection[2][3] = -1.0f;
        projection[3][2] = farPlane * nearPlane / (farPlane - nearPlane);
        const glm::mat4 view = glm::lookAtRH(glm::vec3(2.5f, 1.8f, 3.0f), glm::vec3(0), glm::vec3(0, 1, 0));
        const std::array draws{
            BasicDraw{&cube, glm::translate(glm::mat4(1), glm::vec3(-0.65f, 0, 0))},
            BasicDraw{&cube, glm::translate(glm::mat4(1), glm::vec3(0.65f, 0, 0))},
            BasicDraw{&corner, glm::mat4(1)},
        };
        BasicLighting neutralLighting;
        neutralLighting.view = view;
        neutralLighting.cameraPosition = glm::vec3(glm::inverse(view)[3]);
        neutralLighting.ambientIntensity = 1.0f;
        neutralLighting.directionalIntensity = 0.0f;
        neutralLighting.shadowsEnabled = true;
        renderer.Render(projection * view, neutralLighting, draws);
        const auto pixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        const auto normalPixels = device.ReadTextureRgba8(renderer.GetNormalTexture());
        const auto materialPixels = device.ReadTextureRgba8(renderer.GetMaterialTexture());
        const auto motionPixels = device.ReadTextureRgba8(renderer.GetMotionTexture());
        if (normalPixels.size() != pixels.size() || materialPixels.size() != pixels.size() || motionPixels.size() != pixels.size())
        {
            std::cerr << "Vulkan G-buffer attachments returned inconsistent extents\n";
            return 7;
        }

        std::size_t changed = 0;
        std::uint64_t red = 0, green = 0, blue = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            const auto r = std::to_integer<unsigned char>(pixels[i]);
            const auto g = std::to_integer<unsigned char>(pixels[i + 1]);
            const auto b = std::to_integer<unsigned char>(pixels[i + 2]);
            if (r > 30 || g > 30 || b > 35)
            {
                ++changed;
                red += r;
                green += g;
                blue += b;
            }
        }
        if (changed < 100)
        {
            std::cerr << "Vulkan BasicRenderer produced a blank image (" << changed << " changed pixels)\n";
            return 2;
        }
        // A white material under white lighting must remain neutral. This
        // catches channel/order and constant-buffer layout regressions that can
        // otherwise make every Vulkan viewport mesh appear red.
        if (red > green + changed * 3 || red > blue + changed * 3)
        {
            std::cerr << "Vulkan BasicRenderer introduced a red channel bias ("
                      << red << ", " << green << ", " << blue << ")\n";
            return 4;
        }
        auto ssao = BasicPostProcessEffect{BasicPostProcessEffectType::SSAO};
        ssao.quality = 32;
        ssao.parameters[0] = {1.5f, 0.02f, 3.0f, 1.0f};
        ssao.parameters[1] = {1.0f, 1.0f, 0.0f, 0.0f};
        ssao.parameters[2] = {0.02f, 0.85f, 0.0f, 0.0f};
        renderer.Render(projection * view, neutralLighting, draws, std::span(&ssao, 1));
        const auto aoPixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        std::size_t occludedPixels = 0;
        for (std::size_t i = 0; i + 3 < aoPixels.size(); i += 4)
        {
            const auto value = std::to_integer<unsigned char>(aoPixels[i]);
            if (value < 254)
                ++occludedPixels;
        }
        if (occludedPixels < 10)
        {
            std::cerr << "Vulkan SSAO produced an all-white AO diagnostic ("
                      << occludedPixels << " occluded pixels)\n";
            return 8;
        }
        auto vctgi = BasicPostProcessEffect{BasicPostProcessEffectType::VCTGI};
        vctgi.quality = 3;
        vctgi.parameters[0] = {16.0f, 1.0f, 0.5f, 32.0f};
        vctgi.parameters[1] = {0.1f, 0.9f, 0.05f, 0.8f};
        vctgi.parameters[2] = {32.0f, 1.0f, 1.0f, 1.0f};
        vctgi.parameters[3].w = 256.0f;
        renderer.Render(projection * view, neutralLighting, draws, std::span(&vctgi, 1));
        if (device.ReadTextureRgba8(renderer.GetColorTexture()).size() != 96u * 64u * 4u)
        {
            std::cerr << "Vulkan VCTGI returned an invalid image\n";
            return 9;
        }
        auto colorGrading = BasicPostProcessEffect{BasicPostProcessEffectType::ColorGrading};
        colorGrading.parameters[0] = {0.0f, 1.05f, 1.05f, 0.04f};
        colorGrading.parameters[1] = {0.0f, 0.05f, 0.0f, 1.0f};
        colorGrading.parameters[2] = {1.0f, 0.0f, 0.1f, 0.01f};
        auto chromaticAberration = BasicPostProcessEffect{BasicPostProcessEffectType::ChromaticAberration};
        chromaticAberration.parameters[0].x = 0.003f;
        auto bloom = BasicPostProcessEffect{BasicPostProcessEffectType::Bloom};
        bloom.quality = 4;
        bloom.parameters[0] = {0.35f, 0.8f, 0.5f, 1.0f};
        auto lensFlare = BasicPostProcessEffect{BasicPostProcessEffectType::LensFlare};
        lensFlare.parameters[0] = {0.2f, 0.8f, 1.0f, 0.55f};
        auto motionBlur = BasicPostProcessEffect{BasicPostProcessEffectType::MotionBlur};
        motionBlur.parameters[0] = {1.0f, 0.5f, 20.0f, 0.35f};
        motionBlur.parameters[1].x = 1.0f;
        const std::array postEffects{
            bloom,
            lensFlare,
            motionBlur,
            BasicPostProcessEffect{BasicPostProcessEffectType::ToneMapping, 1.0f, 2.2f},
            BasicPostProcessEffect{BasicPostProcessEffectType::GammaCorrection, 1.0f, 2.2f},
            BasicPostProcessEffect{BasicPostProcessEffectType::FXAA, 1.0f, 2.2f, 1},
            colorGrading,
            chromaticAberration,
        };
        renderer.Render(projection * view, neutralLighting, draws, postEffects);
        const auto postProcessedPixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        if (postProcessedPixels.size() != 96u * 64u * 4u)
        {
            std::cerr << "Vulkan post-process chain returned an invalid image\n";
            return 6;
        }

        // Exercise the editor's persistent readback allocation across a
        // render-target resize. This also verifies that retiring the old
        // texture waits for its targeted copy without violating VMA's
        // persistent-map ownership.
        (void)device.ReadTextureRgba8Buffered(renderer.GetColorTexture());
        if (!renderer.Resize(64, 48))
            return 5;
        renderer.Render(projection * view, neutralLighting, draws);
        (void)device.ReadTextureRgba8Buffered(renderer.GetColorTexture());
        std::cout << "Vulkan mesh rendered on " << device.GetDeviceName() << " (" << changed << " changed pixels)\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 3;
    }
    return 0;
}
