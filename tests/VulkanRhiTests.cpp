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
        if (!input) return {};
        const auto size = input.tellg();
        std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
        input.seekg(0); input.read(reinterpret_cast<char *>(words.data()), size);
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
        BasicRenderer renderer;
        if (!renderer.Initialize(device, shaders) || !renderer.Resize(96, 64)) return 1;

        constexpr std::array<BasicVertex, 8> vertices = {{
            {{{-0.5f,-0.5f,-0.5f}},{{-0.577f,-0.577f,-0.577f}},{{0,0}}},
            {{{ 0.5f,-0.5f,-0.5f}},{{ 0.577f,-0.577f,-0.577f}},{{1,0}}},
            {{{ 0.5f, 0.5f,-0.5f}},{{ 0.577f, 0.577f,-0.577f}},{{1,1}}},
            {{{-0.5f, 0.5f,-0.5f}},{{-0.577f, 0.577f,-0.577f}},{{0,1}}},
            {{{-0.5f,-0.5f, 0.5f}},{{-0.577f,-0.577f, 0.577f}},{{0,0}}},
            {{{ 0.5f,-0.5f, 0.5f}},{{ 0.577f,-0.577f, 0.577f}},{{1,0}}},
            {{{ 0.5f, 0.5f, 0.5f}},{{ 0.577f, 0.577f, 0.577f}},{{1,1}}},
            {{{-0.5f, 0.5f, 0.5f}},{{-0.577f, 0.577f, 0.577f}},{{0,1}}},
        }};
        constexpr std::array<std::uint32_t, 36> indices = {
            0,2,1,0,3,2, 4,5,6,4,6,7, 0,4,7,0,7,3,
            1,2,6,1,6,5, 3,7,6,3,6,2, 0,1,5,0,5,4};
        auto cube = renderer.CreateMesh({vertices, indices});

        glm::mat4 projection(0.0f);
        constexpr float nearPlane = 0.1f, farPlane = 100.0f;
        const float focal = 1.0f / glm::tan(glm::radians(50.0f) * 0.5f);
        projection[0][0] = focal / (96.0f / 64.0f); projection[1][1] = focal;
        projection[2][2] = nearPlane / (farPlane - nearPlane); projection[2][3] = -1.0f;
        projection[3][2] = farPlane * nearPlane / (farPlane - nearPlane);
        const glm::mat4 view = glm::lookAtRH(glm::vec3(2.5f, 1.8f, 3.0f), glm::vec3(0), glm::vec3(0,1,0));
        const std::array draws{
            BasicDraw{&cube, glm::translate(glm::mat4(1), glm::vec3(-0.65f, 0, 0))},
            BasicDraw{&cube, glm::translate(glm::mat4(1), glm::vec3(0.65f, 0, 0))},
        };
        BasicLighting neutralLighting;
        neutralLighting.ambientIntensity = 1.0f;
        neutralLighting.directionalIntensity = 0.0f;
        renderer.Render(projection * view, neutralLighting, draws);
        const auto pixels = device.ReadTextureRgba8(renderer.GetColorTexture());

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
        std::cout << "Vulkan mesh rendered on " << device.GetDeviceName() << " (" << changed << " changed pixels)\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 3;
    }
    return 0;
}
