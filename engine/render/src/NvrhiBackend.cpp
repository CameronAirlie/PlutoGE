#include "PlutoGE/render/NvrhiBackend.h"

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Renderer.h"

#include <iostream>

#if defined(PLUTOGE_WITH_NVRHI)
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <nvrhi/nvrhi.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <nvrhi/d3d12.h>
#endif

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif
#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.h>
#endif

namespace PlutoGE::render
{
#if defined(PLUTOGE_WITH_NVRHI)
    namespace nvrhi_shaders
    {
        extern const std::uint8_t BasicGeometryVS_DXIL[];
        extern const std::size_t BasicGeometryVS_DXILSize;
        extern const std::uint8_t BasicGeometryPS_DXIL[];
        extern const std::size_t BasicGeometryPS_DXILSize;
        extern const std::uint8_t BasicGeometryVS_SPIRV[];
        extern const std::size_t BasicGeometryVS_SPIRVSize;
        extern const std::uint8_t BasicGeometryPS_SPIRV[];
        extern const std::size_t BasicGeometryPS_SPIRVSize;
    }

    namespace
    {
        class NvrhiMessageCallback final : public nvrhi::IMessageCallback
        {
        public:
            void message(nvrhi::MessageSeverity severity, const char *messageText) override
            {
                const char *severityName = "Info";
                switch (severity)
                {
                case nvrhi::MessageSeverity::Warning:
                    severityName = "Warning";
                    break;
                case nvrhi::MessageSeverity::Error:
                    severityName = "Error";
                    break;
                case nvrhi::MessageSeverity::Fatal:
                    severityName = "Fatal";
                    break;
                case nvrhi::MessageSeverity::Info:
                default:
                    break;
                }

                std::cerr << "[NVRHI][" << severityName << "] " << (messageText ? messageText : "") << std::endl;
            }
        };

        constexpr std::uint32_t kSwapChainBufferCount = 2;
    }

    class NvrhiBackend::Impl
    {
    public:
        virtual ~Impl() = default;
        virtual void SetBackend(RenderBackend backend) { m_backend = backend; }
        virtual bool Initialize(const NvrhiBackendConfig &config, std::string &errorMessage) = 0;
        virtual void BeginFrame() {}
        virtual void RenderFrame(int width, int height, const CameraData &cameraData, const std::vector<RenderCommand> &renderCommands)
        {
            auto *device = GetDevice();
            if (!device || width <= 0 || height <= 0)
            {
                return;
            }

            nvrhi::CommandListHandle uploadCommandList = device->createCommandList();
            if (!uploadCommandList)
            {
                return;
            }

            bool hasUploads = false;
            uploadCommandList->open();
            for (const auto &command : renderCommands)
            {
                if (!command.mesh)
                {
                    continue;
                }

                hasUploads = EnsureMeshBuffers(*command.mesh, *uploadCommandList) || hasUploads;
            }

            if (hasUploads)
            {
                uploadCommandList->commitBarriers();
            }
            uploadCommandList->close();

            if (hasUploads)
            {
                device->executeCommandList(uploadCommandList);
            }

            EnsureFrameResources(width, height);
            ExecuteNvrhiPassStack(cameraData, renderCommands);
        }
        virtual void EndFrame() {}
        virtual void Shutdown() = 0;
        virtual void SetVSyncEnabled(bool enabled) { m_vSyncEnabled = enabled; }
        [[nodiscard]] virtual nvrhi::IDevice *GetDevice() const = 0;
#if defined(_WIN32)
        [[nodiscard]] virtual bool GetD3D12Interop(NvrhiD3D12Interop &) const { return false; }
#endif

    protected:
        struct MeshGpuBuffers
        {
            nvrhi::BufferHandle vertexBuffer;
            nvrhi::BufferHandle indexBuffer;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            uint32_t vertexStride = 0;
        };

        struct NvrhiGBufferResources
        {
            nvrhi::TextureHandle position;
            nvrhi::TextureHandle normal;
            nvrhi::TextureHandle albedo;
            nvrhi::TextureHandle motion;
            nvrhi::TextureHandle bakedLighting;
            nvrhi::TextureHandle depth;
        };

        struct NvrhiFramePassResources
        {
            int width = 0;
            int height = 0;
            NvrhiGBufferResources gBuffer;
            nvrhi::FramebufferHandle gBufferFramebuffer;
            nvrhi::TextureHandle lightingColor;
            nvrhi::TextureHandle postProcessColor;
            nvrhi::TextureHandle shadowAtlas;
            nvrhi::TextureHandle pointShadowArray;
        };

        struct BasicGeometryPipelineResources
        {
            nvrhi::ShaderHandle vertexShader;
            nvrhi::ShaderHandle pixelShader;
            nvrhi::InputLayoutHandle inputLayout;
            nvrhi::BindingLayoutHandle bindingLayout;
            nvrhi::BindingSetHandle bindingSet;
            nvrhi::BufferHandle frameConstantBuffer;
            nvrhi::BufferHandle objectConstantBuffer;
            nvrhi::GraphicsPipelineHandle pipeline;
        };

        struct GeometryFrameConstants
        {
            glm::mat4 viewProjection = glm::mat4(1.0f);
        };

        struct GeometryObjectConstants
        {
            glm::mat4 model = glm::mat4(1.0f);
            glm::vec4 baseColor = glm::vec4(1.0f);
            glm::vec4 materialFactors = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        };

        nvrhi::TextureHandle CreateTexture2D(const char *debugName,
                                             int width,
                                             int height,
                                             nvrhi::Format format,
                                             bool isRenderTarget,
                                             bool isDepthStencil,
                                             nvrhi::ResourceStates initialState)
        {
            auto *device = GetDevice();
            if (!device || width <= 0 || height <= 0)
            {
                return {};
            }

            nvrhi::TextureDesc desc{};
            desc.width = static_cast<uint32_t>(width);
            desc.height = static_cast<uint32_t>(height);
            desc.depth = 1;
            desc.arraySize = 1;
            desc.mipLevels = 1;
            desc.sampleCount = 1;
            desc.format = format;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.isRenderTarget = isRenderTarget || isDepthStencil;
            desc.isTypeless = false;
            desc.isUAV = false;
            desc.isShaderResource = true;
            desc.isVirtual = false;
            desc.initialState = initialState;
            desc.keepInitialState = true;
            desc.debugName = debugName ? debugName : "PlutoGE NVRHI Texture";
            desc.isUAV = false;

            return device->createTexture(desc);
        }

        void EnsureFrameResources(int width, int height)
        {
            if (m_frameResources.width == width && m_frameResources.height == height &&
                m_frameResources.gBuffer.position && m_frameResources.lightingColor && m_frameResources.postProcessColor)
            {
                return;
            }

            m_frameResources = {};
            m_frameResources.width = width;
            m_frameResources.height = height;

            m_frameResources.gBuffer.position = CreateTexture2D("NVRHI GBuffer Position", width, height, nvrhi::Format::RGBA16_FLOAT, true, false, nvrhi::ResourceStates::RenderTarget);
            m_frameResources.gBuffer.normal = CreateTexture2D("NVRHI GBuffer Normal", width, height, nvrhi::Format::RGBA16_FLOAT, true, false, nvrhi::ResourceStates::RenderTarget);
            m_frameResources.gBuffer.albedo = CreateTexture2D("NVRHI GBuffer Albedo", width, height, nvrhi::Format::RGBA8_UNORM, true, false, nvrhi::ResourceStates::RenderTarget);
            m_frameResources.gBuffer.motion = CreateTexture2D("NVRHI GBuffer Motion", width, height, nvrhi::Format::RG16_FLOAT, true, false, nvrhi::ResourceStates::RenderTarget);
            m_frameResources.gBuffer.bakedLighting = CreateTexture2D("NVRHI GBuffer Baked Lighting", width, height, nvrhi::Format::RGBA16_FLOAT, true, false, nvrhi::ResourceStates::RenderTarget);
            m_frameResources.gBuffer.depth = CreateTexture2D("NVRHI GBuffer Depth", width, height, nvrhi::Format::D24S8, false, true, nvrhi::ResourceStates::DepthWrite);
            m_frameResources.lightingColor = CreateTexture2D("NVRHI Lighting Color", width, height, nvrhi::Format::RGBA16_FLOAT, true, false, nvrhi::ResourceStates::RenderTarget);
            m_frameResources.postProcessColor = CreateTexture2D("NVRHI Post Process Color", width, height, nvrhi::Format::RGBA16_FLOAT, true, false, nvrhi::ResourceStates::RenderTarget);
            m_frameResources.shadowAtlas = CreateTexture2D("NVRHI Shadow Atlas", 4096, 4096, nvrhi::Format::D24S8, false, true, nvrhi::ResourceStates::DepthWrite);
            m_frameResources.pointShadowArray = CreateTexture2D("NVRHI Point Shadow Array", 1024, 1024, nvrhi::Format::D24S8, false, true, nvrhi::ResourceStates::DepthWrite);

            if (auto *device = GetDevice())
            {
                nvrhi::FramebufferDesc framebufferDesc{};
                framebufferDesc.addColorAttachment(m_frameResources.gBuffer.position);
                framebufferDesc.addColorAttachment(m_frameResources.gBuffer.normal);
                framebufferDesc.addColorAttachment(m_frameResources.gBuffer.albedo);
                framebufferDesc.addColorAttachment(m_frameResources.gBuffer.motion);
                framebufferDesc.addColorAttachment(m_frameResources.gBuffer.bakedLighting);
                framebufferDesc.setDepthAttachment(m_frameResources.gBuffer.depth);
                m_frameResources.gBufferFramebuffer = device->createFramebuffer(framebufferDesc);
                m_basicGeometry.pipeline = nullptr;
            }
        }

        bool EnsureBasicGeometryPipeline()
        {
            auto *device = GetDevice();
            if (!device || !m_frameResources.gBufferFramebuffer)
            {
                return false;
            }

            if (m_basicGeometry.pipeline)
            {
                return true;
            }

            const bool useSpirv = m_backend == RenderBackend::NvrhiVulkan;
            const auto *vertexBytes = useSpirv ? nvrhi_shaders::BasicGeometryVS_SPIRV : nvrhi_shaders::BasicGeometryVS_DXIL;
            const auto vertexByteCount = useSpirv ? nvrhi_shaders::BasicGeometryVS_SPIRVSize : nvrhi_shaders::BasicGeometryVS_DXILSize;
            const auto *pixelBytes = useSpirv ? nvrhi_shaders::BasicGeometryPS_SPIRV : nvrhi_shaders::BasicGeometryPS_DXIL;
            const auto pixelByteCount = useSpirv ? nvrhi_shaders::BasicGeometryPS_SPIRVSize : nvrhi_shaders::BasicGeometryPS_DXILSize;

            nvrhi::ShaderDesc vertexDesc{};
            vertexDesc.shaderType = nvrhi::ShaderType::Vertex;
            vertexDesc.debugName = "PlutoGE Basic Geometry VS";
            vertexDesc.entryName = "vertexMain";
            m_basicGeometry.vertexShader = device->createShader(vertexDesc, vertexBytes, vertexByteCount);

            nvrhi::ShaderDesc pixelDesc{};
            pixelDesc.shaderType = nvrhi::ShaderType::Pixel;
            pixelDesc.debugName = "PlutoGE Basic Geometry PS";
            pixelDesc.entryName = "fragmentMain";
            m_basicGeometry.pixelShader = device->createShader(pixelDesc, pixelBytes, pixelByteCount);

            if (!m_basicGeometry.vertexShader || !m_basicGeometry.pixelShader)
            {
                return false;
            }

            if (!m_basicGeometry.frameConstantBuffer)
            {
                nvrhi::BufferDesc frameBufferDesc{};
                frameBufferDesc.byteSize = sizeof(GeometryFrameConstants);
                frameBufferDesc.debugName = "PlutoGE Geometry Frame Constants";
                frameBufferDesc.isConstantBuffer = true;
                frameBufferDesc.isVolatile = true;
                frameBufferDesc.maxVersions = 16;
                m_basicGeometry.frameConstantBuffer = device->createBuffer(frameBufferDesc);
            }

            if (!m_basicGeometry.objectConstantBuffer)
            {
                nvrhi::BufferDesc objectBufferDesc{};
                objectBufferDesc.byteSize = sizeof(GeometryObjectConstants);
                objectBufferDesc.debugName = "PlutoGE Geometry Object Constants";
                objectBufferDesc.isConstantBuffer = true;
                objectBufferDesc.isVolatile = true;
                objectBufferDesc.maxVersions = 4096;
                m_basicGeometry.objectConstantBuffer = device->createBuffer(objectBufferDesc);
            }

            if (!m_basicGeometry.frameConstantBuffer || !m_basicGeometry.objectConstantBuffer)
            {
                return false;
            }

            if (!m_basicGeometry.bindingLayout)
            {
                nvrhi::BindingLayoutDesc layoutDesc{};
                layoutDesc.setVisibility(nvrhi::ShaderType::Vertex)
                    .setRegisterSpaceAndDescriptorSet(0)
                    .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
                    .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(1));
                m_basicGeometry.bindingLayout = device->createBindingLayout(layoutDesc);
            }

            if (!m_basicGeometry.bindingSet)
            {
                nvrhi::BindingSetDesc bindingSetDesc{};
                bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_basicGeometry.frameConstantBuffer))
                    .addItem(nvrhi::BindingSetItem::ConstantBuffer(1, m_basicGeometry.objectConstantBuffer));
                m_basicGeometry.bindingSet = device->createBindingSet(bindingSetDesc, m_basicGeometry.bindingLayout);
            }

            if (!m_basicGeometry.bindingLayout || !m_basicGeometry.bindingSet)
            {
                return false;
            }

            const std::array<nvrhi::VertexAttributeDesc, 3> attributes = {
                nvrhi::VertexAttributeDesc()
                    .setName("POSITION")
                    .setFormat(nvrhi::Format::RGB32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(static_cast<uint32_t>(offsetof(MeshVertexData, position)))
                    .setElementStride(static_cast<uint32_t>(sizeof(MeshVertexData))),
                nvrhi::VertexAttributeDesc()
                    .setName("NORMAL")
                    .setFormat(nvrhi::Format::RGB32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(static_cast<uint32_t>(offsetof(MeshVertexData, normal)))
                    .setElementStride(static_cast<uint32_t>(sizeof(MeshVertexData))),
                nvrhi::VertexAttributeDesc()
                    .setName("TEXCOORD")
                    .setFormat(nvrhi::Format::RG32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(static_cast<uint32_t>(offsetof(MeshVertexData, uv)))
                    .setElementStride(static_cast<uint32_t>(sizeof(MeshVertexData))),
            };

            m_basicGeometry.inputLayout = device->createInputLayout(attributes.data(), static_cast<uint32_t>(attributes.size()), m_basicGeometry.vertexShader);
            if (!m_basicGeometry.inputLayout)
            {
                return false;
            }

            nvrhi::RenderState renderState{};
            renderState.rasterState.setCullBack();
            renderState.depthStencilState.setDepthTestEnable(true).enableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);

            nvrhi::GraphicsPipelineDesc pipelineDesc{};
            pipelineDesc.setPrimType(nvrhi::PrimitiveType::TriangleList)
                .setVertexShader(m_basicGeometry.vertexShader)
                .setPixelShader(m_basicGeometry.pixelShader)
                .setInputLayout(m_basicGeometry.inputLayout)
                .setRenderState(renderState)
                .addBindingLayout(m_basicGeometry.bindingLayout);

            m_basicGeometry.pipeline = device->createGraphicsPipeline(pipelineDesc, m_frameResources.gBufferFramebuffer->getFramebufferInfo());
            return m_basicGeometry.pipeline != nullptr;
        }

        void ClearTexture(nvrhi::ICommandList &commandList, nvrhi::ITexture *texture, const nvrhi::Color &color)
        {
            if (!texture)
            {
                return;
            }

            commandList.setTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);
            commandList.commitBarriers();
            commandList.clearTextureFloat(texture, nvrhi::AllSubresources, color);
        }

        void ClearDepth(nvrhi::ICommandList &commandList, nvrhi::ITexture *texture)
        {
            if (!texture)
            {
                return;
            }

            commandList.setTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::DepthWrite);
            commandList.commitBarriers();
            commandList.clearDepthStencilTexture(texture, nvrhi::AllSubresources, true, 1.0f, false, 0);
        }

        void ExecuteNvrhiPassStack(const CameraData &cameraData, const std::vector<RenderCommand> &renderCommands)
        {
            auto *device = GetDevice();
            if (!device)
            {
                return;
            }

            auto commandList = device->createCommandList();
            if (!commandList)
            {
                return;
            }

            commandList->open();
            ClearTexture(*commandList, m_frameResources.gBuffer.position.Get(), nvrhi::Color(0.0f));
            ClearTexture(*commandList, m_frameResources.gBuffer.normal.Get(), nvrhi::Color(0.0f));
            ClearTexture(*commandList, m_frameResources.gBuffer.albedo.Get(), nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            ClearTexture(*commandList, m_frameResources.gBuffer.motion.Get(), nvrhi::Color(0.0f));
            ClearTexture(*commandList, m_frameResources.gBuffer.bakedLighting.Get(), nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            ClearDepth(*commandList, m_frameResources.gBuffer.depth.Get());
            ClearDepth(*commandList, m_frameResources.shadowAtlas.Get());
            ClearDepth(*commandList, m_frameResources.pointShadowArray.Get());
            DrawBasicGeometry(*commandList, cameraData, renderCommands);
            ClearTexture(*commandList, m_frameResources.lightingColor.Get(), nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            ClearTexture(*commandList, m_frameResources.postProcessColor.Get(), nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            commandList->close();
            device->executeCommandList(commandList);
        }

        void DrawBasicGeometry(nvrhi::ICommandList &commandList, const CameraData &cameraData, const std::vector<RenderCommand> &renderCommands)
        {
            if (!EnsureBasicGeometryPipeline())
            {
                return;
            }

            nvrhi::Viewport viewport(
                0.0f,
                static_cast<float>(m_frameResources.width),
                0.0f,
                static_cast<float>(m_frameResources.height),
                0.0f,
                1.0f);

            nvrhi::GraphicsState graphicsState{};
            graphicsState.setPipeline(m_basicGeometry.pipeline)
                .setFramebuffer(m_frameResources.gBufferFramebuffer)
                .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(viewport))
                .addBindingSet(m_basicGeometry.bindingSet);

            GeometryFrameConstants frameConstants{};
            frameConstants.viewProjection = cameraData.projection * cameraData.view;
            commandList.writeBuffer(m_basicGeometry.frameConstantBuffer, &frameConstants, sizeof(frameConstants));

            for (const auto &command : renderCommands)
            {
                if (!command.mesh)
                {
                    continue;
                }

                auto buffersIt = m_meshBuffers.find(command.mesh);
                if (buffersIt == m_meshBuffers.end())
                {
                    continue;
                }

                const auto &buffers = buffersIt->second;
                if (!buffers.vertexBuffer || !buffers.indexBuffer)
                {
                    continue;
                }

                const auto submeshIndex = static_cast<std::size_t>(command.submeshIndex);
                if (submeshIndex >= command.mesh->GetSubmeshCount())
                {
                    continue;
                }

                const auto &submesh = command.mesh->GetSubmesh(submeshIndex);
                if (submesh.indexCount == 0)
                {
                    continue;
                }

                graphicsState.vertexBuffers.resize(0);
                graphicsState.setIndexBuffer(nvrhi::IndexBufferBinding()
                                                 .setBuffer(buffers.indexBuffer)
                                                 .setFormat(nvrhi::Format::R32_UINT)
                                                 .setOffset(0));
                graphicsState.addVertexBuffer(nvrhi::VertexBufferBinding()
                                                  .setBuffer(buffers.vertexBuffer)
                                                  .setSlot(0)
                                                  .setOffset(0));

                GeometryObjectConstants objectConstants{};
                objectConstants.model = command.model;
                if (command.material)
                {
                    const auto &materialConfig = command.material->GetConfig();
                    objectConstants.baseColor = materialConfig.color;
                    objectConstants.materialFactors = glm::vec4(materialConfig.metallic, materialConfig.roughness, 0.0f, 0.0f);
                }
                commandList.writeBuffer(m_basicGeometry.objectConstantBuffer, &objectConstants, sizeof(objectConstants));
                commandList.setGraphicsState(graphicsState);
                commandList.drawIndexed(nvrhi::DrawArguments()
                                            .setVertexCount(submesh.indexCount)
                                            .setInstanceCount(1)
                                            .setStartIndexLocation(submesh.indexOffset)
                                            .setStartVertexLocation(0)
                                            .setStartInstanceLocation(0));
            }
        }

        bool EnsureMeshBuffers(const Mesh &mesh, nvrhi::ICommandList &uploadCommandList)
        {
            if (m_meshBuffers.find(&mesh) != m_meshBuffers.end())
            {
                return false;
            }

            auto *device = GetDevice();
            if (!device)
            {
                return false;
            }

            const auto &meshData = mesh.GetMeshData();
            if (meshData.vertices.empty() || meshData.indices.empty())
            {
                return false;
            }

            MeshGpuBuffers buffers{};
            buffers.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
            buffers.indexCount = static_cast<uint32_t>(meshData.indices.size());
            buffers.vertexStride = static_cast<uint32_t>(sizeof(MeshVertexData));

            nvrhi::BufferDesc vertexDesc{};
            vertexDesc.byteSize = static_cast<uint64_t>(meshData.vertices.size() * sizeof(MeshVertexData));
            vertexDesc.debugName = "PlutoGE Mesh Vertex Buffer";
            vertexDesc.isVertexBuffer = true;
            vertexDesc.initialState = nvrhi::ResourceStates::Common;
            buffers.vertexBuffer = device->createBuffer(vertexDesc);

            nvrhi::BufferDesc indexDesc{};
            indexDesc.byteSize = static_cast<uint64_t>(meshData.indices.size() * sizeof(unsigned int));
            indexDesc.debugName = "PlutoGE Mesh Index Buffer";
            indexDesc.isIndexBuffer = true;
            indexDesc.initialState = nvrhi::ResourceStates::Common;
            buffers.indexBuffer = device->createBuffer(indexDesc);

            if (!buffers.vertexBuffer || !buffers.indexBuffer)
            {
                return false;
            }

            uploadCommandList.writeBuffer(buffers.vertexBuffer.Get(), meshData.vertices.data(), static_cast<size_t>(vertexDesc.byteSize));
            uploadCommandList.setBufferState(buffers.vertexBuffer.Get(), nvrhi::ResourceStates::VertexBuffer);
            uploadCommandList.writeBuffer(buffers.indexBuffer.Get(), meshData.indices.data(), static_cast<size_t>(indexDesc.byteSize));
            uploadCommandList.setBufferState(buffers.indexBuffer.Get(), nvrhi::ResourceStates::IndexBuffer);

            m_meshBuffers.emplace(&mesh, std::move(buffers));
            return true;
        }

        NvrhiMessageCallback m_messageCallback;
        std::unordered_map<const Mesh *, MeshGpuBuffers> m_meshBuffers;
        NvrhiFramePassResources m_frameResources;
        BasicGeometryPipelineResources m_basicGeometry;
        RenderBackend m_backend = RenderBackend::NvrhiD3D12;
        bool m_vSyncEnabled = true;
    };

#if defined(_WIN32)
    class D3D12NvrhiBackend final : public NvrhiBackend::Impl
    {
    public:
        bool Initialize(const NvrhiBackendConfig &config, std::string &errorMessage) override
        {
            m_vSyncEnabled = config.vSyncEnabled;

            auto *glfwWindow = config.window ? static_cast<GLFWwindow *>(config.window->GetWindow()) : nullptr;
            if (!glfwWindow)
            {
                errorMessage = "D3D12 backend requires a valid GLFW window.";
                return false;
            }

            UINT factoryFlags = 0;
#if defined(_DEBUG)
            Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();
                factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
#endif

            if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory))))
            {
                errorMessage = "Failed to create DXGI factory.";
                return false;
            }

            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            for (UINT adapterIndex = 0; m_factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex)
            {
                DXGI_ADAPTER_DESC1 adapterDesc{};
                adapter->GetDesc1(&adapterDesc);
                if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                {
                    continue;
                }

                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))))
                {
                    break;
                }
            }

            if (!m_device)
            {
                if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))))
                {
                    errorMessage = "Failed to create D3D12 device.";
                    return false;
                }
            }

            if (!CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT, m_graphicsQueue, errorMessage) ||
                !CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeQueue, errorMessage) ||
                !CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY, m_copyQueue, errorMessage))
            {
                return false;
            }

            if (!CreateSwapChain(glfwWindow, errorMessage))
            {
                return false;
            }

            nvrhi::d3d12::DeviceDesc deviceDesc{};
            deviceDesc.errorCB = &m_messageCallback;
            deviceDesc.pDevice = m_device.Get();
            deviceDesc.pGraphicsCommandQueue = m_graphicsQueue.Get();
            deviceDesc.pComputeCommandQueue = m_computeQueue.Get();
            deviceDesc.pCopyCommandQueue = m_copyQueue.Get();
            m_nvrhiDevice = nvrhi::d3d12::createDevice(deviceDesc);
            if (!m_nvrhiDevice)
            {
                errorMessage = "Failed to create NVRHI D3D12 device.";
                return false;
            }

            m_commandList = m_nvrhiDevice->createCommandList();
            if (!m_commandList)
            {
                errorMessage = "Failed to create NVRHI D3D12 command list.";
                return false;
            }

            if (!WrapSwapChainBuffers(errorMessage))
            {
                return false;
            }

            return true;
        }

        void BeginFrame() override
        {
            if (!m_swapChain || !m_commandList || m_backBufferTextures.empty())
            {
                return;
            }

            const auto backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
            if (backBufferIndex >= m_backBufferTextures.size() || !m_backBufferTextures[backBufferIndex])
            {
                return;
            }

            auto *backBuffer = m_backBufferTextures[backBufferIndex].Get();
            m_commandList->open();
            m_commandList->beginTrackingTextureState(backBuffer, nvrhi::AllSubresources, nvrhi::ResourceStates::Present);
            m_commandList->setTextureState(backBuffer, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);
            m_commandList->commitBarriers();
            m_commandList->clearTextureFloat(backBuffer, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            m_commandList->setTextureState(backBuffer, nvrhi::AllSubresources, nvrhi::ResourceStates::Present);
            m_commandList->commitBarriers();
            m_commandList->close();
            m_nvrhiDevice->executeCommandList(m_commandList);
        }

        void EndFrame() override
        {
            if (m_swapChain)
            {
                m_swapChain->Present(m_vSyncEnabled ? 1 : 0, 0);
            }
        }

        void Shutdown() override
        {
            m_backBufferTextures.clear();
            m_commandList = nullptr;
            m_nvrhiDevice = nullptr;
            m_swapChain.Reset();
            m_copyQueue.Reset();
            m_computeQueue.Reset();
            m_graphicsQueue.Reset();
            m_device.Reset();
            m_factory.Reset();
        }

        [[nodiscard]] nvrhi::IDevice *GetDevice() const override
        {
            return m_nvrhiDevice.Get();
        }

    private:
        bool CreateCommandQueue(D3D12_COMMAND_LIST_TYPE type, Microsoft::WRL::ComPtr<ID3D12CommandQueue> &queue, std::string &errorMessage)
        {
            D3D12_COMMAND_QUEUE_DESC queueDesc{};
            queueDesc.Type = type;
            queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.NodeMask = 0;

            if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))))
            {
                errorMessage = "Failed to create D3D12 command queue.";
                return false;
            }

            return true;
        }

        bool CreateSwapChain(GLFWwindow *glfwWindow, std::string &errorMessage)
        {
            HWND hwnd = glfwGetWin32Window(glfwWindow);
            if (!hwnd)
            {
                errorMessage = "Failed to get Win32 window handle for D3D12 swap chain.";
                return false;
            }

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(glfwWindow, &width, &height);
            width = (std::max)(width, 1);
            height = (std::max)(height, 1);

            DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
            swapChainDesc.Width = static_cast<UINT>(width);
            swapChainDesc.Height = static_cast<UINT>(height);
            swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapChainDesc.SampleDesc.Count = 1;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.BufferCount = kSwapChainBufferCount;
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
            swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

            Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
            if (FAILED(m_factory->CreateSwapChainForHwnd(m_graphicsQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain)))
            {
                errorMessage = "Failed to create D3D12 swap chain.";
                return false;
            }

            m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            if (FAILED(swapChain.As(&m_swapChain)))
            {
                errorMessage = "Failed to query IDXGISwapChain3.";
                return false;
            }

            return true;
        }

        bool WrapSwapChainBuffers(std::string &errorMessage)
        {
            m_backBufferTextures.clear();
            m_backBufferResources.clear();
            m_backBufferTextures.reserve(kSwapChainBufferCount);
            m_backBufferResources.reserve(kSwapChainBufferCount);

            DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
            if (FAILED(m_swapChain->GetDesc1(&swapChainDesc)))
            {
                errorMessage = "Failed to query D3D12 swap chain description.";
                return false;
            }

            m_backBufferFormat = swapChainDesc.Format;

            for (std::uint32_t index = 0; index < kSwapChainBufferCount; ++index)
            {
                Microsoft::WRL::ComPtr<ID3D12Resource> resource;
                if (FAILED(m_swapChain->GetBuffer(index, IID_PPV_ARGS(&resource))))
                {
                    errorMessage = "Failed to get D3D12 swap chain backbuffer.";
                    return false;
                }

                nvrhi::TextureDesc textureDesc{};
                textureDesc.width = swapChainDesc.Width;
                textureDesc.height = swapChainDesc.Height;
                textureDesc.depth = 1;
                textureDesc.arraySize = 1;
                textureDesc.mipLevels = 1;
                textureDesc.sampleCount = 1;
                textureDesc.format = nvrhi::Format::RGBA8_UNORM;
                textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
                textureDesc.isRenderTarget = true;
                textureDesc.initialState = nvrhi::ResourceStates::Present;
                textureDesc.keepInitialState = true;
                textureDesc.debugName = "D3D12 Swapchain Backbuffer";

                auto texture = m_nvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(resource.Get()), textureDesc);
                if (!texture)
                {
                    errorMessage = "Failed to wrap D3D12 swap chain backbuffer as an NVRHI texture.";
                    return false;
                }

                m_backBufferTextures.push_back(texture);
                m_backBufferResources.push_back(resource);
            }

            return true;
        }

        [[nodiscard]] bool GetD3D12Interop(NvrhiD3D12Interop &interop) const override
        {
            if (!m_device || !m_graphicsQueue || !m_swapChain || m_backBufferResources.empty())
            {
                return false;
            }

            const auto backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
            if (backBufferIndex >= m_backBufferResources.size() || !m_backBufferResources[backBufferIndex])
            {
                return false;
            }

            interop.device = m_device.Get();
            interop.graphicsQueue = m_graphicsQueue.Get();
            interop.swapChain = m_swapChain.Get();
            interop.currentBackBuffer = m_backBufferResources[backBufferIndex].Get();
            interop.backBufferFormat = m_backBufferFormat;
            interop.bufferCount = static_cast<unsigned int>(m_backBufferResources.size());
            return true;
        }

        Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_graphicsQueue;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_computeQueue;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_copyQueue;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
        nvrhi::d3d12::DeviceHandle m_nvrhiDevice;
        nvrhi::CommandListHandle m_commandList;
        std::vector<nvrhi::TextureHandle> m_backBufferTextures;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_backBufferResources;
        DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_UNKNOWN;
    };
#endif

    class VulkanNvrhiBackend final : public NvrhiBackend::Impl
    {
    public:
        bool Initialize(const NvrhiBackendConfig &config, std::string &errorMessage) override
        {
            auto *glfwWindow = config.window ? static_cast<GLFWwindow *>(config.window->GetWindow()) : nullptr;
            if (!glfwWindow)
            {
                errorMessage = "Vulkan backend requires a valid GLFW window.";
                return false;
            }

            if (!LoadGlobalEntryPoints(errorMessage))
            {
                return false;
            }

            std::uint32_t glfwExtensionCount = 0;
            const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            if (!glfwExtensions || glfwExtensionCount == 0)
            {
                errorMessage = "GLFW did not report required Vulkan instance extensions.";
                return false;
            }

            m_instanceExtensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);

            VkApplicationInfo appInfo{};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = "PlutoGE";
            appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
            appInfo.pEngineName = "PlutoGE";
            appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
            appInfo.apiVersion = VK_API_VERSION_1_2;

            VkInstanceCreateInfo instanceInfo{};
            instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            instanceInfo.pApplicationInfo = &appInfo;
            instanceInfo.enabledExtensionCount = glfwExtensionCount;
            instanceInfo.ppEnabledExtensionNames = m_instanceExtensions.data();

            VkResult result = m_vkCreateInstance(&instanceInfo, nullptr, &m_instance);
            if (result != VK_SUCCESS)
            {
                errorMessage = std::string("Failed to create Vulkan instance: ") + nvrhi::vulkan::resultToString(result);
                return false;
            }

            if (!LoadInstanceEntryPoints(errorMessage))
            {
                return false;
            }
            VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance, m_vkGetInstanceProcAddr);

            result = CreateSurface(glfwWindow);
            if (result != VK_SUCCESS)
            {
                errorMessage = std::string("Failed to create Vulkan surface: ") + nvrhi::vulkan::resultToString(result);
                return false;
            }

            if (!ChoosePhysicalDevice(errorMessage) || !CreateLogicalDevice(errorMessage) || !CreateSwapChain(glfwWindow, errorMessage))
            {
                return false;
            }

            nvrhi::vulkan::DeviceDesc deviceDesc{};
            deviceDesc.errorCB = &m_messageCallback;
            deviceDesc.instance = m_instance;
            deviceDesc.physicalDevice = m_physicalDevice;
            deviceDesc.device = m_device;
            deviceDesc.graphicsQueue = m_graphicsQueue;
            deviceDesc.graphicsQueueIndex = static_cast<int>(m_graphicsQueueFamily);
            deviceDesc.transferQueue = m_graphicsQueue;
            deviceDesc.transferQueueIndex = static_cast<int>(m_graphicsQueueFamily);
            deviceDesc.computeQueue = m_graphicsQueue;
            deviceDesc.computeQueueIndex = static_cast<int>(m_graphicsQueueFamily);
            deviceDesc.instanceExtensions = m_instanceExtensions.data();
            deviceDesc.numInstanceExtensions = m_instanceExtensions.size();
            deviceDesc.deviceExtensions = m_deviceExtensions.data();
            deviceDesc.numDeviceExtensions = m_deviceExtensions.size();
            m_nvrhiDevice = nvrhi::vulkan::createDevice(deviceDesc);
            if (!m_nvrhiDevice)
            {
                errorMessage = "Failed to create NVRHI Vulkan device.";
                return false;
            }

            m_commandList = m_nvrhiDevice->createCommandList();
            if (!m_commandList)
            {
                errorMessage = "Failed to create NVRHI Vulkan command list.";
                return false;
            }

            if (!WrapSwapChainImages(errorMessage) || !CreateAcquireFence(errorMessage))
            {
                return false;
            }

            return true;
        }

        void BeginFrame() override
        {
            if (!m_swapChain || !m_commandList || m_swapChainTextures.empty() || !m_vkAcquireNextImageKHR || !m_vkQueuePresentKHR)
            {
                return;
            }

            m_vkResetFences(m_device, 1, &m_acquireFence);

            std::uint32_t imageIndex = 0;
            VkResult result = m_vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX, VK_NULL_HANDLE, m_acquireFence, &imageIndex);
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            {
                std::cerr << "Failed to acquire Vulkan swap chain image: " << nvrhi::vulkan::resultToString(result) << std::endl;
                return;
            }

            result = m_vkWaitForFences(m_device, 1, &m_acquireFence, VK_TRUE, UINT64_MAX);
            if (result != VK_SUCCESS)
            {
                std::cerr << "Failed to wait for Vulkan swap chain image: " << nvrhi::vulkan::resultToString(result) << std::endl;
                return;
            }

            if (imageIndex >= m_swapChainTextures.size() || !m_swapChainTextures[imageIndex])
            {
                return;
            }

            auto *swapChainTexture = m_swapChainTextures[imageIndex].Get();
            m_commandList->open();
            m_commandList->beginTrackingTextureState(swapChainTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Present);
            m_commandList->setTextureState(swapChainTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);
            m_commandList->commitBarriers();
            m_commandList->clearTextureFloat(swapChainTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            m_commandList->setTextureState(swapChainTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Present);
            m_commandList->commitBarriers();
            m_commandList->close();
            m_nvrhiDevice->executeCommandList(m_commandList);

            m_vkDeviceWaitIdle(m_device);

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &m_swapChain;
            presentInfo.pImageIndices = &imageIndex;
            result = m_vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            {
                std::cerr << "Failed to present Vulkan swap chain image: " << nvrhi::vulkan::resultToString(result) << std::endl;
            }
        }

        void Shutdown() override
        {
            m_swapChainTextures.clear();
            m_commandList = nullptr;
            m_nvrhiDevice = nullptr;

            if (m_acquireFence != VK_NULL_HANDLE)
            {
                m_vkDestroyFence(m_device, m_acquireFence, nullptr);
                m_acquireFence = VK_NULL_HANDLE;
            }

            for (auto imageView : m_swapChainImageViews)
            {
                m_vkDestroyImageView(m_device, imageView, nullptr);
            }
            m_swapChainImageViews.clear();

            if (m_swapChain != VK_NULL_HANDLE)
            {
                m_vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
                m_swapChain = VK_NULL_HANDLE;
            }
            if (m_device != VK_NULL_HANDLE)
            {
                m_vkDestroyDevice(m_device, nullptr);
                m_device = VK_NULL_HANDLE;
            }
            if (m_surface != VK_NULL_HANDLE)
            {
                m_vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
                m_surface = VK_NULL_HANDLE;
            }
            if (m_instance != VK_NULL_HANDLE)
            {
                m_vkDestroyInstance(m_instance, nullptr);
                m_instance = VK_NULL_HANDLE;
            }
#if defined(_WIN32)
            if (m_vulkanLibrary)
            {
                FreeLibrary(m_vulkanLibrary);
                m_vulkanLibrary = nullptr;
            }
#endif
        }

        [[nodiscard]] nvrhi::IDevice *GetDevice() const override
        {
            return m_nvrhiDevice.Get();
        }

    private:
        bool LoadGlobalEntryPoints(std::string &errorMessage)
        {
#if defined(_WIN32)
            m_vulkanLibrary = LoadLibraryA("vulkan-1.dll");
            if (!m_vulkanLibrary)
            {
                errorMessage = "Failed to load vulkan-1.dll.";
                return false;
            }

            m_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(m_vulkanLibrary, "vkGetInstanceProcAddr"));
#else
            errorMessage = "Dynamic Vulkan loading is only implemented for Windows.";
            return false;
#endif

            if (!m_vkGetInstanceProcAddr)
            {
                errorMessage = "Failed to load Vulkan global entry point vkGetInstanceProcAddr.";
                return false;
            }

            VULKAN_HPP_DEFAULT_DISPATCHER.init(m_vkGetInstanceProcAddr);
            m_vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(m_vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
            if (!m_vkCreateInstance)
            {
                errorMessage = "Failed to load Vulkan global entry point vkCreateInstance.";
                return false;
            }

            return true;
        }

        bool LoadInstanceEntryPoints(std::string &errorMessage)
        {
            auto load = [this](const char *name)
            {
                return m_vkGetInstanceProcAddr(m_instance, name);
            };

            m_vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(load("vkDestroyInstance"));
            m_vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(load("vkEnumeratePhysicalDevices"));
            m_vkGetPhysicalDeviceQueueFamilyProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(load("vkGetPhysicalDeviceQueueFamilyProperties"));
            m_vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(load("vkCreateDevice"));
            m_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(load("vkGetDeviceProcAddr"));
            m_vkGetPhysicalDeviceSurfaceSupportKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(load("vkGetPhysicalDeviceSurfaceSupportKHR"));
            m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(load("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
            m_vkGetPhysicalDeviceSurfaceFormatsKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(load("vkGetPhysicalDeviceSurfaceFormatsKHR"));
            m_vkDestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(load("vkDestroySurfaceKHR"));
#if defined(_WIN32)
            m_vkCreateWin32SurfaceKHR = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(load("vkCreateWin32SurfaceKHR"));
#endif

            if (!m_vkDestroyInstance || !m_vkEnumeratePhysicalDevices || !m_vkGetPhysicalDeviceQueueFamilyProperties ||
                !m_vkCreateDevice || !m_vkGetDeviceProcAddr || !m_vkGetPhysicalDeviceSurfaceSupportKHR ||
                !m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR || !m_vkGetPhysicalDeviceSurfaceFormatsKHR ||
                !m_vkDestroySurfaceKHR
#if defined(_WIN32)
                || !m_vkCreateWin32SurfaceKHR
#endif
            )
            {
                errorMessage = "Failed to load required Vulkan instance entry points.";
                return false;
            }

            return true;
        }

        bool LoadDeviceEntryPoints(std::string &errorMessage)
        {
            auto load = [this](const char *name)
            {
                return m_vkGetDeviceProcAddr(m_device, name);
            };

            m_vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(load("vkDestroyDevice"));
            m_vkGetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(load("vkGetDeviceQueue"));
            m_vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(load("vkCreateImageView"));
            m_vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(load("vkDestroyImageView"));
            m_vkCreateFence = reinterpret_cast<PFN_vkCreateFence>(load("vkCreateFence"));
            m_vkDestroyFence = reinterpret_cast<PFN_vkDestroyFence>(load("vkDestroyFence"));
            m_vkWaitForFences = reinterpret_cast<PFN_vkWaitForFences>(load("vkWaitForFences"));
            m_vkResetFences = reinterpret_cast<PFN_vkResetFences>(load("vkResetFences"));
            m_vkDeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(load("vkDeviceWaitIdle"));
            m_vkCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(load("vkCreateSwapchainKHR"));
            m_vkDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(load("vkDestroySwapchainKHR"));
            m_vkGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(load("vkGetSwapchainImagesKHR"));
            m_vkAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(load("vkAcquireNextImageKHR"));
            m_vkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(load("vkQueuePresentKHR"));

            if (!m_vkDestroyDevice || !m_vkGetDeviceQueue || !m_vkCreateImageView || !m_vkDestroyImageView ||
                !m_vkCreateFence || !m_vkDestroyFence || !m_vkWaitForFences || !m_vkResetFences || !m_vkDeviceWaitIdle ||
                !m_vkCreateSwapchainKHR || !m_vkDestroySwapchainKHR || !m_vkGetSwapchainImagesKHR ||
                !m_vkAcquireNextImageKHR || !m_vkQueuePresentKHR)
            {
                errorMessage = "Failed to load required Vulkan device entry points.";
                return false;
            }

            return true;
        }

        VkResult CreateSurface(GLFWwindow *glfwWindow)
        {
#if defined(_WIN32)
            VkWin32SurfaceCreateInfoKHR surfaceInfo{};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.hinstance = GetModuleHandle(nullptr);
            surfaceInfo.hwnd = glfwGetWin32Window(glfwWindow);
            return m_vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
#else
            (void)glfwWindow;
            return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
        }

        bool ChoosePhysicalDevice(std::string &errorMessage)
        {
            std::uint32_t deviceCount = 0;
            m_vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
            if (deviceCount == 0)
            {
                errorMessage = "No Vulkan physical devices were found.";
                return false;
            }

            std::vector<VkPhysicalDevice> devices(deviceCount);
            m_vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

            for (auto device : devices)
            {
                std::uint32_t queueFamilyCount = 0;
                m_vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
                std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                m_vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

                for (std::uint32_t index = 0; index < queueFamilyCount; ++index)
                {
                    VkBool32 presentSupported = VK_FALSE;
                    m_vkGetPhysicalDeviceSurfaceSupportKHR(device, index, m_surface, &presentSupported);
                    if ((queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && presentSupported)
                    {
                        m_physicalDevice = device;
                        m_graphicsQueueFamily = index;
                        return true;
                    }
                }
            }

            errorMessage = "No Vulkan device with graphics and present support was found.";
            return false;
        }

        bool CreateLogicalDevice(std::string &errorMessage)
        {
            constexpr float queuePriority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = m_graphicsQueueFamily;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;

            m_deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

            VkPhysicalDeviceFeatures deviceFeatures{};
            VkDeviceCreateInfo deviceInfo{};
            deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceInfo.queueCreateInfoCount = 1;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            deviceInfo.pEnabledFeatures = &deviceFeatures;
            deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(m_deviceExtensions.size());
            deviceInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

            const VkResult result = m_vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device);
            if (result != VK_SUCCESS)
            {
                errorMessage = std::string("Failed to create Vulkan logical device: ") + nvrhi::vulkan::resultToString(result);
                return false;
            }

            if (!LoadDeviceEntryPoints(errorMessage))
            {
                return false;
            }

            VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(m_device));
            m_vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
            return true;
        }

        bool CreateAcquireFence(std::string &errorMessage)
        {
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            const VkResult result = m_vkCreateFence(m_device, &fenceInfo, nullptr, &m_acquireFence);
            if (result != VK_SUCCESS)
            {
                errorMessage = std::string("Failed to create Vulkan swap chain acquire fence: ") + nvrhi::vulkan::resultToString(result);
                return false;
            }

            return true;
        }

        bool CreateSwapChain(GLFWwindow *glfwWindow, std::string &errorMessage)
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);

            std::uint32_t formatCount = 0;
            m_vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(formatCount);
            m_vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
            if (formats.empty())
            {
                errorMessage = "Vulkan surface does not expose any supported formats.";
                return false;
            }

            VkSurfaceFormatKHR selectedFormat = formats.front();
            for (const auto &format : formats)
            {
                if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    selectedFormat = format;
                    break;
                }
            }

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(glfwWindow, &width, &height);
            VkExtent2D extent{
                static_cast<std::uint32_t>((std::max)(width, 1)),
                static_cast<std::uint32_t>((std::max)(height, 1)),
            };
            if (capabilities.currentExtent.width != UINT32_MAX)
            {
                extent = capabilities.currentExtent;
            }
            else
            {
                extent.width = (std::max)(capabilities.minImageExtent.width, (std::min)(capabilities.maxImageExtent.width, extent.width));
                extent.height = (std::max)(capabilities.minImageExtent.height, (std::min)(capabilities.maxImageExtent.height, extent.height));
            }

            std::uint32_t imageCount = capabilities.minImageCount + 1;
            if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
            {
                imageCount = capabilities.maxImageCount;
            }

            VkSwapchainCreateInfoKHR swapChainInfo{};
            swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            swapChainInfo.surface = m_surface;
            swapChainInfo.minImageCount = imageCount;
            swapChainInfo.imageFormat = selectedFormat.format;
            swapChainInfo.imageColorSpace = selectedFormat.colorSpace;
            swapChainInfo.imageExtent = extent;
            swapChainInfo.imageArrayLayers = 1;
            swapChainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapChainInfo.preTransform = capabilities.currentTransform;
            swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            swapChainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            swapChainInfo.clipped = VK_TRUE;

            VkResult result = m_vkCreateSwapchainKHR(m_device, &swapChainInfo, nullptr, &m_swapChain);
            if (result != VK_SUCCESS)
            {
                errorMessage = std::string("Failed to create Vulkan swap chain: ") + nvrhi::vulkan::resultToString(result);
                return false;
            }

            std::uint32_t swapChainImageCount = 0;
            m_vkGetSwapchainImagesKHR(m_device, m_swapChain, &swapChainImageCount, nullptr);
            m_swapChainImages.resize(swapChainImageCount);
            m_vkGetSwapchainImagesKHR(m_device, m_swapChain, &swapChainImageCount, m_swapChainImages.data());
            m_swapChainImageViews.reserve(m_swapChainImages.size());
            m_swapChainFormat = selectedFormat.format;
            m_swapChainExtent = extent;

            for (auto image : m_swapChainImages)
            {
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = selectedFormat.format;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;

                VkImageView imageView = VK_NULL_HANDLE;
                result = m_vkCreateImageView(m_device, &viewInfo, nullptr, &imageView);
                if (result != VK_SUCCESS)
                {
                    errorMessage = std::string("Failed to create Vulkan swap chain image view: ") + nvrhi::vulkan::resultToString(result);
                    return false;
                }
                m_swapChainImageViews.push_back(imageView);
            }

            return true;
        }

        bool WrapSwapChainImages(std::string &errorMessage)
        {
            m_swapChainTextures.clear();
            m_swapChainTextures.reserve(m_swapChainImages.size());

            const nvrhi::Format nvrhiFormat = m_swapChainFormat == VK_FORMAT_B8G8R8A8_UNORM
                                                  ? nvrhi::Format::BGRA8_UNORM
                                                  : nvrhi::Format::RGBA8_UNORM;

            for (auto image : m_swapChainImages)
            {
                nvrhi::TextureDesc textureDesc{};
                textureDesc.width = m_swapChainExtent.width;
                textureDesc.height = m_swapChainExtent.height;
                textureDesc.depth = 1;
                textureDesc.arraySize = 1;
                textureDesc.mipLevels = 1;
                textureDesc.sampleCount = 1;
                textureDesc.format = nvrhiFormat;
                textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
                textureDesc.isRenderTarget = true;
                textureDesc.initialState = nvrhi::ResourceStates::Present;
                textureDesc.keepInitialState = true;
                textureDesc.debugName = "Vulkan Swapchain Image";

                auto texture = m_nvrhiDevice->createHandleForNativeTexture(
                    nvrhi::ObjectTypes::VK_Image,
                    nvrhi::Object(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(image))),
                    textureDesc);
                if (!texture)
                {
                    errorMessage = "Failed to wrap Vulkan swap chain image as an NVRHI texture.";
                    return false;
                }

                m_swapChainTextures.push_back(texture);
            }

            return true;
        }

        std::vector<const char *> m_instanceExtensions;
        std::vector<const char *> m_deviceExtensions;
#if defined(_WIN32)
        HMODULE m_vulkanLibrary = nullptr;
#endif
        PFN_vkGetInstanceProcAddr m_vkGetInstanceProcAddr = nullptr;
        PFN_vkCreateInstance m_vkCreateInstance = nullptr;
        PFN_vkDestroyInstance m_vkDestroyInstance = nullptr;
        PFN_vkEnumeratePhysicalDevices m_vkEnumeratePhysicalDevices = nullptr;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties m_vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
        PFN_vkCreateDevice m_vkCreateDevice = nullptr;
        PFN_vkGetDeviceProcAddr m_vkGetDeviceProcAddr = nullptr;
        PFN_vkDestroyDevice m_vkDestroyDevice = nullptr;
        PFN_vkGetDeviceQueue m_vkGetDeviceQueue = nullptr;
        PFN_vkCreateImageView m_vkCreateImageView = nullptr;
        PFN_vkDestroyImageView m_vkDestroyImageView = nullptr;
        PFN_vkCreateFence m_vkCreateFence = nullptr;
        PFN_vkDestroyFence m_vkDestroyFence = nullptr;
        PFN_vkWaitForFences m_vkWaitForFences = nullptr;
        PFN_vkResetFences m_vkResetFences = nullptr;
        PFN_vkDeviceWaitIdle m_vkDeviceWaitIdle = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR m_vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR m_vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
        PFN_vkDestroySurfaceKHR m_vkDestroySurfaceKHR = nullptr;
        PFN_vkCreateSwapchainKHR m_vkCreateSwapchainKHR = nullptr;
        PFN_vkDestroySwapchainKHR m_vkDestroySwapchainKHR = nullptr;
        PFN_vkGetSwapchainImagesKHR m_vkGetSwapchainImagesKHR = nullptr;
        PFN_vkAcquireNextImageKHR m_vkAcquireNextImageKHR = nullptr;
        PFN_vkQueuePresentKHR m_vkQueuePresentKHR = nullptr;
#if defined(_WIN32)
        PFN_vkCreateWin32SurfaceKHR m_vkCreateWin32SurfaceKHR = nullptr;
#endif
        VkInstance m_instance = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        std::uint32_t m_graphicsQueueFamily = 0;
        VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
        VkFence m_acquireFence = VK_NULL_HANDLE;
        VkFormat m_swapChainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D m_swapChainExtent{};
        std::vector<VkImage> m_swapChainImages;
        std::vector<VkImageView> m_swapChainImageViews;
        nvrhi::vulkan::DeviceHandle m_nvrhiDevice;
        nvrhi::CommandListHandle m_commandList;
        std::vector<nvrhi::TextureHandle> m_swapChainTextures;
    };
#else
    class NvrhiBackend::Impl
    {
    public:
        bool Initialize(const NvrhiBackendConfig &, std::string &)
        {
            return false;
        }

        void BeginFrame() {}
        void RenderFrame(int, int, const CameraData &, const std::vector<RenderCommand> &) {}
        void EndFrame() {}
        void Shutdown() {}
        void SetVSyncEnabled(bool) {}

        [[nodiscard]] nvrhi::IDevice *GetDevice() const
        {
            return nullptr;
        }
        [[nodiscard]] bool GetD3D12Interop(NvrhiD3D12Interop &) const
        {
            return false;
        }
    };
#endif

    NvrhiBackend::NvrhiBackend() = default;
    NvrhiBackend::~NvrhiBackend()
    {
        Shutdown();
    }

    bool NvrhiBackend::Initialize(const NvrhiBackendConfig &config)
    {
        Shutdown();
        m_config = config;
        m_lastError.clear();

        if (!IsNvrhiBackend(config.backend))
        {
            m_lastError = "NvrhiBackend can only initialize NVRHI render backends.";
            return false;
        }

#if defined(PLUTOGE_WITH_NVRHI)
        if (config.backend == RenderBackend::NvrhiD3D12)
        {
#if defined(_WIN32)
            m_impl = std::make_unique<D3D12NvrhiBackend>();
#else
            m_lastError = "NVRHI D3D12 backend is only available on Windows.";
            return false;
#endif
        }
        else
        {
            m_impl = std::make_unique<VulkanNvrhiBackend>();
        }

        m_impl->SetBackend(config.backend);
        if (!m_impl->Initialize(config, m_lastError))
        {
            m_impl->Shutdown();
            m_impl.reset();
            return false;
        }

        return true;
#else
        m_lastError = "PlutoGE was built without PLUTO_WITH_NVRHI. Reconfigure with -DPLUTO_WITH_NVRHI=ON to enable DX12/Vulkan backends.";
        return false;
#endif
    }

    void NvrhiBackend::BeginFrame()
    {
        if (m_impl)
        {
            m_impl->BeginFrame();
        }
    }

    void NvrhiBackend::RenderFrame(int width, int height, const CameraData &cameraData, const std::vector<RenderCommand> &renderCommands)
    {
        if (m_impl)
        {
            m_impl->RenderFrame(width, height, cameraData, renderCommands);
        }
    }

    void NvrhiBackend::EndFrame()
    {
        if (m_impl)
        {
            m_impl->EndFrame();
        }
    }

    void NvrhiBackend::Shutdown()
    {
        if (m_impl)
        {
            m_impl->Shutdown();
            m_impl.reset();
        }
    }

    void NvrhiBackend::SetVSyncEnabled(bool enabled)
    {
        m_config.vSyncEnabled = enabled;
        if (m_impl)
        {
            m_impl->SetVSyncEnabled(enabled);
        }
    }

    nvrhi::IDevice *NvrhiBackend::GetDevice() const
    {
        return m_impl ? m_impl->GetDevice() : nullptr;
    }

#if defined(_WIN32)
    bool NvrhiBackend::GetD3D12Interop(NvrhiD3D12Interop &interop) const
    {
        return m_impl ? m_impl->GetD3D12Interop(interop) : false;
    }
#endif
}
