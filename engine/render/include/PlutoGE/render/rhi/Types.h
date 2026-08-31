#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::render::rhi
{
    enum class GraphicsApi : std::uint8_t { OpenGL, Vulkan };
    enum class Format : std::uint8_t { Undefined, R8G8B8A8Unorm, R8G8B8A8Srgb, R32Float, D32Float, R32Uint, R32G32Float, R32G32B32Float, R32G32B32A32Float };
    enum class BufferUsage : std::uint8_t { Vertex, Index, Uniform };
    enum class TextureUsage : std::uint8_t { Sampled, ColorAttachment, DepthStencilAttachment };
    enum class ShaderStage : std::uint8_t { Vertex, Fragment };
    enum class ShaderStageMask : std::uint8_t { Vertex = 1, Fragment = 2, AllGraphics = 3 };
    enum class ResourceBindingType : std::uint8_t { UniformBuffer, SampledTexture };
    enum class PrimitiveTopology : std::uint8_t { TriangleList };
    enum class CullMode : std::uint8_t { None, Front, Back };
    enum class CompareOperation : std::uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };

    template <typename Tag>
    struct Handle
    {
        static constexpr std::uint32_t InvalidIndex = ~std::uint32_t{0};
        std::uint32_t index = InvalidIndex;
        std::uint32_t generation = 0;
        [[nodiscard]] constexpr bool IsValid() const noexcept { return index != InvalidIndex; }
        constexpr explicit operator bool() const noexcept { return IsValid(); }
        auto operator<=>(const Handle &) const = default;
    };

    using BufferHandle = Handle<struct BufferTag>;
    using TextureHandle = Handle<struct TextureTag>;
    using SamplerHandle = Handle<struct SamplerTag>;
    using PipelineHandle = Handle<struct PipelineTag>;
    using RenderPassHandle = Handle<struct RenderPassTag>;

    struct Viewport { float x = 0; float y = 0; float width = 0; float height = 0; float minDepth = 0; float maxDepth = 1; };
    struct Scissor { std::int32_t x = 0; std::int32_t y = 0; std::uint32_t width = 0; std::uint32_t height = 0; };
    struct BlendState { bool enabled = false; };

    struct VertexAttribute
    {
        std::uint32_t location = 0;
        Format format = Format::R32G32B32Float;
        std::uint32_t offset = 0;
    };

    struct VertexLayout
    {
        std::uint32_t stride = 0;
        std::vector<VertexAttribute> attributes;
    };

    struct BufferDescriptor
    {
        std::size_t size = 0;
        BufferUsage usage = BufferUsage::Vertex;
        std::string debugName;
    };

    struct TextureDescriptor
    {
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        Format format = Format::R8G8B8A8Unorm;
        TextureUsage usage = TextureUsage::Sampled;
        std::string debugName;
        bool sampled = false;
    };

    struct GraphicsPipelineDescriptor
    {
        struct ShaderCode
        {
            std::string glsl;
            std::vector<std::uint32_t> spirv;
        };
        ShaderCode vertexShader;
        ShaderCode fragmentShader;
        Format colorFormat = Format::R8G8B8A8Srgb;
        // Empty preserves the single-target colorFormat compatibility path.
        std::vector<Format> colorFormats;
        Format depthFormat = Format::D32Float;
        struct ResourceBinding
        {
            // slot is the backend-neutral command binding. set/binding mirror
            // the declarations emitted by Slang for Vulkan.
            std::uint32_t slot = 0;
            std::uint32_t set = 0;
            std::uint32_t binding = 0;
            ResourceBindingType type = ResourceBindingType::UniformBuffer;
            ShaderStageMask stages = ShaderStageMask::AllGraphics;
        };
        std::vector<ResourceBinding> resourceBindings;
        VertexLayout vertexLayout;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        CullMode cullMode = CullMode::Back;
        CompareOperation depthCompare = CompareOperation::GreaterOrEqual;
        bool depthTest = true;
        bool depthWrite = true;
        BlendState blend;
        std::string debugName;
    };

    struct SamplerDescriptor
    {
        bool linearFiltering = true;
        bool repeat = true;
        std::string debugName;
    };
}
