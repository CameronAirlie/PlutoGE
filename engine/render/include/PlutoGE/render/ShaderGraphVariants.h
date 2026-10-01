#pragma once

#include "PlutoGE/render/ShaderGraph.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::render
{
    // BasicLit.slang fragment entry points whose graph evaluation can be specialised.
    enum class ShaderGraphVariantStage : std::uint8_t
    {
        Surface,     // fragmentMain: all geometry outputs
        Color,       // colorMain
        ColorMotion, // colorMotionMain
        Coverage,    // coverageMain: graph-aware depth prepass
    };
    [[nodiscard]] std::string_view ShaderGraphVariantEntryPoint(ShaderGraphVariantStage stage) noexcept;

    // Compiles BasicLit.slang to SPIR-V with PLUTO_SHADER_GRAPH_GENERATED defined,
    // resolving ShaderGraphGenerated.slang from the variant directory.
    class ShaderGraphVariantCompiler
    {
    public:
        struct Request
        {
            std::filesystem::path variantDirectory; // Holds ShaderGraphGenerated.slang.
            std::filesystem::path sourceDirectory;  // Directory containing BasicLit.slang.
            std::string_view entryPoint;
        };
        virtual ~ShaderGraphVariantCompiler() = default;
        // Identifies the compiler build; part of the cache key.
        [[nodiscard]] virtual std::string Identity() = 0;
        // Throws std::runtime_error with diagnostics on failure.
        [[nodiscard]] virtual std::vector<std::uint32_t> CompileFragment(const Request &request) = 0;
    };

    // Loads the Slang runtime library on demand. Returns null when it is not
    // installed beside the executable; graphs then remain interpreted.
    [[nodiscard]] std::unique_ptr<ShaderGraphVariantCompiler> CreateSlangShaderGraphCompiler();

    // Specialised BasicLit fragment shaders per graph structure. Lookups never
    // block the render thread: a missing variant is generated, compiled and
    // cached on disk by a worker thread, and the caller keeps using the
    // interpreter until it becomes available. Variants are Vulkan SPIR-V.
    class ShaderGraphVariantCache
    {
    public:
        using Code = std::shared_ptr<const std::vector<std::uint32_t>>;
        struct Options
        {
            // Shader package sources: shaders/BasicLit.slang and the headers it
            // includes. Every file contributes to the cache key.
            std::filesystem::path sourceRoot;
            std::filesystem::path cacheDirectory; // Writable compiled-variant cache.
        };

        ShaderGraphVariantCache(Options options, std::unique_ptr<ShaderGraphVariantCompiler> compiler);
        ~ShaderGraphVariantCache();
        ShaderGraphVariantCache(const ShaderGraphVariantCache &) = delete;
        ShaderGraphVariantCache &operator=(const ShaderGraphVariantCache &) = delete;

        // Shared per shader package, using the Slang runtime and a per-user cache.
        // Null when the package has no sources, Slang is unavailable, or
        // PLUTOGE_SHADER_GRAPH_VARIANTS=0 disables specialisation.
        [[nodiscard]] static std::shared_ptr<ShaderGraphVariantCache> ForShaderPackage(
            const std::filesystem::path &shaderRoot);

        // Returns the compiled variant, or null while pending or after failure.
        [[nodiscard]] Code Find(const ShaderGraphProgram &program, ShaderGraphVariantStage stage);
        // Blocks until every scheduled variant is resolved. For tests and warm-up.
        void WaitIdle();

        struct Stats
        {
            std::uint64_t compiled = 0, diskHits = 0, failures = 0;
        };
        [[nodiscard]] Stats GetStats() const;

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
