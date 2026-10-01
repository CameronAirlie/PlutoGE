#pragma once
// Generated shader-graph variants must be interchangeable with the interpreter.
// Include after ShaderGraphRenderingChecks.h, which provides the test graphs.
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/ShaderGraphCodegen.h"
#include "PlutoGE/render/ShaderGraphVariants.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace ShaderGraphVariantTesting
{
    inline void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // Exercises nearly every opcode: inputs, noise, texture, component pack,
    // divide, lerp, clamp, normalize, dot, sine, power, one-minus, step,
    // smoothstep and floor, across albedo, normal, metallic, roughness and emission.
    inline PlutoGE::render::ShaderGraph KitchenSinkGraph()
    {
        using namespace PlutoGE::render;
        using K = ShaderGraphNodeKind;
        ShaderGraph g;
        g.nodes = {{.id = 1, .kind = K::MeshUV},
            {.id = 2, .kind = K::NoiseTexture, .value = {8, 1, 0, 0}},
            {.id = 3, .kind = K::WorldNormal},
            {.id = 4, .kind = K::ViewDirection},
            {.id = 5, .kind = K::TextureSample},
            {.id = 10, .kind = K::Expression, .parameter = "saturate(dot(A,B))"},
            {.id = 11, .kind = K::Expression, .parameter = "pow(A,2.0)+step(0.5,B)*0.25+floor(A*3.0)/6.0"},
            {.id = 12, .kind = K::Expression, .parameter = "sin(A*6.0)*0.5+0.5"},
            {.id = 13, .kind = K::Vec3, .componentPins = true},
            {.id = 14, .kind = K::Float, .value = glm::vec4(2)},
            {.id = 15, .kind = K::Divide},
            {.id = 16, .kind = K::Multiply},
            {.id = 17, .kind = K::OneMinus},
            {.id = 18, .kind = K::Float, .value = glm::vec4(.1f)},
            {.id = 19, .kind = K::Float, .value = glm::vec4(.9f)},
            {.id = 20, .kind = K::Clamp},
            {.id = 21, .kind = K::Normalize},
            {.id = 22, .kind = K::Lerp},
            {.id = 23, .kind = K::Expression, .parameter = "smoothstep(0.2,0.8,A)"},
            {.id = 24, .kind = K::Multiply},
            {.id = 100, .kind = K::Output}};
        g.links = {{1, 1, "Out", 2, "UV"}, {2, 1, "Out", 5, "UV"},
            {3, 3, "Out", 10, "A"}, {4, 4, "Out", 10, "B"},
            {5, 10, "Out", 11, "A"}, {6, 2, "Value", 11, "B"}, {7, 2, "Value", 12, "A"},
            {8, 11, "Out", 13, "X"}, {9, 12, "Out", 13, "Y"}, {10, 10, "Out", 13, "Z"},
            {11, 13, "Vec3", 15, "A"}, {12, 14, "Out", 15, "B"},
            {13, 15, "Out", 16, "A"}, {14, 5, "R", 16, "B"},
            {15, 12, "Out", 17, "Value"},
            {16, 11, "Out", 20, "Value"}, {17, 18, "Out", 20, "Min"}, {18, 19, "Out", 20, "Max"},
            {19, 13, "Vec3", 21, "Value"},
            {20, 12, "Out", 22, "A"}, {21, 10, "Out", 22, "B"}, {22, 2, "Value", 23, "A"}, {23, 23, "Out", 22, "T"},
            {24, 15, "Out", 24, "A"}, {25, 22, "Out", 24, "B"},
            {26, 16, "Out", 100, "Albedo"}, {27, 21, "Out", 100, "Normal"}, {28, 20, "Out", 100, "Metallic"},
            {29, 17, "Out", 100, "Roughness"}, {30, 24, "Out", 100, "Emission"}};
        return g;
    }

    // Noise opacity with alpha testing: coverage and shading must discard alike.
    inline PlutoGE::render::ShaderGraph MaskedGraph()
    {
        using namespace PlutoGE::render;
        using K = ShaderGraphNodeKind;
        ShaderGraph g;
        g.nodes = {{.id = 1, .kind = K::MeshUV}, {.id = 2, .kind = K::NoiseTexture, .value = {6, 1, 0, 0}},
            {.id = 3, .kind = K::Expression, .parameter = "step(0.5,A)"}, {.id = 100, .kind = K::Output}};
        g.links = {{1, 1, "Out", 2, "UV"}, {2, 2, "Value", 3, "A"}, {3, 3, "Out", 100, "Opacity"}};
        return g;
    }

    // A Direct Lighting branch shares surface registers with Albedo.
    inline PlutoGE::render::ShaderGraph SharedLightingGraph()
    {
        using namespace PlutoGE::render;
        using K = ShaderGraphNodeKind;
        ShaderGraph g;
        g.nodes = {{.id = 1, .kind = K::MaterialInput}, {.id = 2, .kind = K::Float, .value = glm::vec4(.5f)},
            {.id = 3, .kind = K::Multiply}, {.id = 4, .kind = K::LightAttenuation}, {.id = 5, .kind = K::Multiply},
            {.id = 100, .kind = K::Output}};
        g.links = {{1, 1, "Out", 3, "A"}, {2, 2, "Out", 3, "B"}, {3, 3, "Out", 100, "Albedo"},
            {4, 3, "Out", 5, "A"}, {5, 4, "Out", 5, "B"}, {6, 5, "Out", 100, "Direct Lighting"}};
        return g;
    }

    // Writes a SPIR-V header, recording each request; fails on demand.
    class FakeCompiler final : public PlutoGE::render::ShaderGraphVariantCompiler
    {
    public:
        explicit FakeCompiler(std::atomic<int> &calls, bool fail = false) : m_calls(calls), m_fail(fail) {}
        std::string Identity() override { return "fake"; }
        std::vector<std::uint32_t> CompileFragment(const Request &request) override
        {
            ++m_calls;
            Require(std::filesystem::exists(request.variantDirectory / "ShaderGraphGenerated.slang"),
                "Generated source was not written before compilation");
            if (m_fail) throw std::runtime_error("intentional test failure");
            return {0x07230203u, 0x00010300u, static_cast<std::uint32_t>(request.entryPoint.size())};
        }

    private:
        std::atomic<int> &m_calls;
        bool m_fail;
    };
}

inline void CheckShaderGraphCodegen()
{
    using namespace PlutoGE::render;
    using namespace ShaderGraphVariantTesting;
    const auto parameters = BuildShaderGraphProgram(ParameterTestGraph());
    const std::array overrides{ShaderGraphVariable{"Tint", ShaderGraphValueType::Vec3, {.1f, .2f, .3f, 1}}};
    const auto overridden = BuildShaderGraphProgram(ParameterTestGraph(), overrides);
    const auto toon = BuildShaderGraphProgram(ToonTestGraph());
    Require(parameters && overridden && toon, "Codegen test graphs failed to compile");
    Require(parameters->hash != overridden->hash, "Parameter override did not change the program");
    Require(parameters->structureHash == overridden->structureHash && parameters->structureHash != 0,
        "Parameter values must not change the generated-code structure");
    Require(parameters->structureHash != toon->structureHash, "Different graphs share a structure hash");
    Require(ShaderGraphStructureHash(parameters->data) == parameters->structureHash, "Cached structure hash is stale");

    const auto source = GenerateShaderGraphSlang(toon->data);
    Require(source.find("registers") == std::string::npos, "Generated code kept the interpreter register file");
    for (const char *function : {"evaluateShaderGraph(", "shaderGraphVertexOffset(", "evaluateShaderGraphLighting("})
        Require(source.find(function) != std::string::npos, std::string("Generated code lacks ") + function);

    ShaderGraphProgramData malformed{};
    malformed.header.x = 1;
    malformed.instructions[0] = {2, 0, 0, 0}; // Add reads its own result.
    bool rejected = false;
    try { (void)GenerateShaderGraphSlang(malformed); }
    catch (const std::invalid_argument &) { rejected = true; }
    Require(rejected, "Codegen accepted bytecode that reads an uncomputed register");
}

inline void CheckShaderGraphVariantCache()
{
    using namespace PlutoGE::render;
    using namespace ShaderGraphVariantTesting;
    const auto root = std::filesystem::temp_directory_path() /
        ("PlutoGEVariantCacheTest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "source" / "shaders");
    std::ofstream(root / "source" / "shaders" / "BasicLit.slang") << "// test package\n";
    const ShaderGraphVariantCache::Options options{root / "source", root / "cache"};
    const auto program = BuildShaderGraphProgram(ToonTestGraph());
    std::atomic<int> calls = 0;
    {
        ShaderGraphVariantCache cache(options, std::make_unique<FakeCompiler>(calls));
        Require(!cache.Find(*program, ShaderGraphVariantStage::Surface), "A variant was available before compilation");
        cache.WaitIdle();
        const auto code = cache.Find(*program, ShaderGraphVariantStage::Surface);
        Require(code && code->size() == 3 && calls == 1, "Variant was not compiled asynchronously");
        Require(cache.Find(*program, ShaderGraphVariantStage::Surface) == code && calls == 1,
            "A resolved variant was compiled again");
    }
    {
        // A new process reuses the published SPIR-V without compiling.
        ShaderGraphVariantCache cache(options, std::make_unique<FakeCompiler>(calls));
        (void)cache.Find(*program, ShaderGraphVariantStage::Surface);
        cache.WaitIdle();
        Require(cache.Find(*program, ShaderGraphVariantStage::Surface) && calls == 1 && cache.GetStats().diskHits == 1,
            "Disk cache was not reused");
    }
    {
        ShaderGraphVariantCache cache(options, std::make_unique<FakeCompiler>(calls, true));
        (void)cache.Find(*program, ShaderGraphVariantStage::Coverage);
        cache.WaitIdle();
        Require(!cache.Find(*program, ShaderGraphVariantStage::Coverage) && cache.GetStats().failures == 1,
            "A failed variant must leave the interpreter in use");
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

// Renders each graph with the interpreter and with its generated variant.
template <class ReadPixels>
void CheckShaderGraphVariantRendering(PlutoGE::render::rhi::IRenderDevice &device,
    PlutoGE::render::BasicRendererShaderPackage shaders, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    using namespace ShaderGraphVariantTesting;
    CheckShaderGraphCodegen();
    CheckShaderGraphVariantCache();
    const auto variants = ShaderGraphVariantCache::ForShaderPackage(PLUTO_RHI_TEST_SHADER_DIR);
    if (!variants)
    {
        std::cout << "Shader graph variants unavailable (no Slang runtime or staged sources); GPU comparison skipped\n";
        return;
    }
    shaders.graphVariants.reset();
    BasicRenderer interpreted;
    Require(interpreted.Initialize(device, shaders) && interpreted.Resize(96, 64), "Interpreter renderer failed");
    shaders.graphVariants = variants;
    BasicRenderer specialised;
    Require(specialised.Initialize(device, shaders) && specialised.Resize(96, 64), "Variant renderer failed");

    const std::array<BasicVertex, 4> vertices{{
        {{{-.8f, -.8f, .5f}}, {{0, 0, 1}}, {{0, 0}}}, {{{.8f, -.8f, .5f}}, {{0, 0, 1}}, {{1, 0}}},
        {{{.8f, .8f, .5f}}, {{0, 0, 1}}, {{1, 1}}}, {{{-.8f, .8f, .5f}}, {{0, 0, 1}}, {{0, 1}}}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto interpretedMesh = interpreted.CreateMesh({vertices, indices});
    auto specialisedMesh = specialised.CreateMesh({vertices, indices});
    const std::array<std::byte, 16> texels{std::byte{230}, std::byte{40}, std::byte{90}, std::byte{255},
        std::byte{30}, std::byte{200}, std::byte{60}, std::byte{255}, std::byte{80}, std::byte{80}, std::byte{220}, std::byte{255},
        std::byte{250}, std::byte{240}, std::byte{20}, std::byte{255}};
    rhi::Texture texture(device, device.CreateTexture({2, 2, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "Variant test albedo"}, texels));

    BasicLighting lighting;
    lighting.ambientIntensity = .2f;
    lighting.directionalIntensity = 1;
    lighting.directionalColor = {1, .9f, .8f};
    lighting.directionalDirection = {-.3f, -.4f, -1};
    lighting.cameraPosition = {0, 0, 2};
    lighting.pointLights = {{{.4f, .3f, 1.5f}, 6, {1, .5f, .25f}, 2}, {{-.5f, -.2f, 1.2f}, 4, {.2f, .6f, 1}, 1.5f}};

    struct Case { const char *name; ShaderGraph graph; std::uint32_t alphaMode; };
    const std::array cases{Case{"parameter", ParameterTestGraph(), 0}, Case{"toon lighting", ToonTestGraph(), 0},
        Case{"shared lighting registers", SharedLightingGraph(), 0}, Case{"kitchen sink", KitchenSinkGraph(), 0},
        Case{"masked coverage", MaskedGraph(), 1}};
    for (const auto &test : cases)
    {
        const auto program = BuildShaderGraphProgram(test.graph);
        Require(bool(program), std::string(test.name) + " graph failed to compile");
        BasicDraw draw;
        draw.baseColor = {.8f, .7f, .6f, 1};
        draw.baseColorTexture = texture.Get();
        draw.metallic = .3f;
        draw.roughness = .5f;
        draw.alphaMode = test.alphaMode;
        draw.shaderGraphProgram = program;
        draw.mesh = &interpretedMesh;
        interpreted.Render(glm::mat4(1), lighting, {&draw, 1});
        const auto expected = readPixels(interpreted.GetColorTexture());

        draw.mesh = &specialisedMesh;
        specialised.Render(glm::mat4(1), lighting, {&draw, 1}); // Schedules the variants.
        variants->WaitIdle();
        specialised.Render(glm::mat4(1), lighting, {&draw, 1});
        const auto &stats = specialised.GetFrameStats();
        Require(stats.graphSpecializedDraws > 0 && stats.graphInterpretedDraws == 0,
            std::string(test.name) + " did not use its generated variant");
        const auto actual = readPixels(specialised.GetColorTexture());
        Require(actual.size() == expected.size() && !actual.empty(), "Variant readback size mismatch");
        // Identical operations; allow rounding and rare threshold flips from
        // differing floating-point contraction after constant folding.
        std::size_t differing = 0;
        double total = 0;
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            if (i % 4 == 3) continue;
            const int difference = std::abs(int(actual[i]) - int(expected[i]));
            total += difference;
            differing += difference > 2;
        }
        const double mean = total / (actual.size() * 3 / 4);
        Require(mean < .05 && differing * 200 <= actual.size() * 3 / 4,
            std::string(test.name) + " variant differs from the interpreter: mean " + std::to_string(mean) +
                ", " + std::to_string(differing) + " channels over tolerance");
    }
    Require(variants->GetStats().failures == 0, "A generated shader-graph variant failed to compile");
    std::cout << "Shader graph variants match the interpreter for " << cases.size() << " graphs\n";
}
