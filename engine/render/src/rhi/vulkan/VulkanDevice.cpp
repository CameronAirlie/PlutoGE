#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "../HandleRegistry.h"

#include <volk.h>
#include <GLFW/glfw3.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render::rhi::vulkan
{
    namespace
    {
        void Check(VkResult result, const char *operation)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
        }

        VkFormat ToVkFormat(Format format)
        {
            switch (format)
            {
            case Format::R8G8B8A8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
            case Format::R8G8B8A8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
            case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
            case Format::D32Float: return VK_FORMAT_D32_SFLOAT;
            case Format::R32Uint: return VK_FORMAT_R32_UINT;
            case Format::R32G32Float: return VK_FORMAT_R32G32_SFLOAT;
            case Format::R32G32B32Float: return VK_FORMAT_R32G32B32_SFLOAT;
            case Format::R32G32B32A32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
            default: throw std::invalid_argument("Unsupported Vulkan RHI format");
            }
        }

        VkCompareOp ToVkCompare(CompareOperation operation)
        {
            switch (operation)
            {
            case CompareOperation::Never: return VK_COMPARE_OP_NEVER;
            case CompareOperation::Less: return VK_COMPARE_OP_LESS;
            case CompareOperation::Equal: return VK_COMPARE_OP_EQUAL;
            case CompareOperation::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOperation::Greater: return VK_COMPARE_OP_GREATER;
            case CompareOperation::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOperation::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOperation::Always: return VK_COMPARE_OP_ALWAYS;
            }
            return VK_COMPARE_OP_ALWAYS;
        }

        VkBufferUsageFlags BufferUsageFlags(BufferUsage usage)
        {
            switch (usage)
            {
            case BufferUsage::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            case BufferUsage::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            case BufferUsage::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            }
            return 0;
        }

        float SrgbToLinear(std::uint8_t value)
        {
            const float encoded = static_cast<float>(value) / 255.0f;
            return encoded <= 0.04045f ? encoded / 12.92f
                                      : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

        std::byte LinearToSrgb(float value)
        {
            const float linear = std::clamp(value, 0.0f, 1.0f);
            const float encoded = linear <= 0.0031308f ? linear * 12.92f
                                                       : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::byte>(static_cast<std::uint8_t>(std::lround(encoded * 255.0f)));
        }

        struct BufferResource
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            std::vector<std::byte> uniformData;
            std::array<VkDeviceSize, 3> uniformOffsets{VK_WHOLE_SIZE, VK_WHOLE_SIZE, VK_WHOLE_SIZE};
            std::array<std::uint64_t, 3> uniformEpochs{};
            std::size_t size = 0;
            BufferUsage usage{};
        };

        struct TextureResource
        {
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            TextureDescriptor descriptor;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            std::uint32_t mipLevels = 1;
        };

        struct SamplerResource { VkSampler sampler = VK_NULL_HANDLE; };

        struct PipelineResource
        {
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
            std::vector<VkDescriptorSetLayout> setLayouts;
            std::vector<bool> populatedSets;
            std::vector<std::vector<GraphicsPipelineDescriptor::ResourceBinding>> bindingsBySet;
            GraphicsPipelineDescriptor descriptor;
        };

        VkImageAspectFlags Aspect(const TextureResource &texture)
        {
            return texture.descriptor.format == Format::D32Float ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    struct VulkanDevice::Impl
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        std::uint32_t presentQueueFamily = 0;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        struct ReadbackSlot
        {
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            void *mapped = nullptr;
            std::size_t byteCount = 0;
            bool pending = false;
            std::uint64_t sequence = 0;
            TextureHandle source;
        };
        std::array<ReadbackSlot, 3> readbacks;
        std::uint64_t nextReadbackSequence = 1;
        std::size_t activeFrameIndex = 0;
        struct UniformArena
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            void *mapped = nullptr;
            VkDeviceSize cursor = 0;
            VkDeviceSize dirtyStart = VK_WHOLE_SIZE;
            VkDeviceSize dirtyEnd = 0;
            std::uint64_t epoch = 1;
        };
        std::array<UniformArena, 3> uniformArenas;
        VkDeviceSize uniformAlignment = 256;
        static constexpr VkDeviceSize UniformArenaSize = 32ull * 1024ull * 1024ull;
        std::string deviceName;
        float timestampPeriodNs = 1.0f;
        RenderDeviceTimingStats timingStats;
        detail::HandleRegistry<BufferHandle, BufferResource> buffers;
        detail::HandleRegistry<TextureHandle, TextureResource> textures;
        detail::HandleRegistry<SamplerHandle, SamplerResource> samplers;
        detail::HandleRegistry<PipelineHandle, PipelineResource> pipelines;
        std::unique_ptr<VulkanCommandContext> context;

        void BeginUniformFrame(std::size_t frameIndex)
        {
            activeFrameIndex = frameIndex;
            auto &arena = uniformArenas[frameIndex];
            arena.cursor = 0;
            arena.dirtyStart = VK_WHOLE_SIZE;
            arena.dirtyEnd = 0;
            ++arena.epoch;
            if (arena.epoch == 0) arena.epoch = 1;
        }

        VkDeviceSize EnsureUniformResident(BufferResource &resource)
        {
            auto &arena = uniformArenas[activeFrameIndex];
            if (resource.uniformEpochs[activeFrameIndex] == arena.epoch)
                return resource.uniformOffsets[activeFrameIndex];
            const auto aligned = (arena.cursor + uniformAlignment - 1) & ~(uniformAlignment - 1);
            if (aligned + resource.size > UniformArenaSize)
                throw std::runtime_error("Vulkan per-frame uniform arena exhausted");
            std::memcpy(static_cast<std::byte *>(arena.mapped) + aligned,
                        resource.uniformData.data(), resource.size);
            arena.dirtyStart = std::min(arena.dirtyStart, aligned);
            arena.dirtyEnd = std::max(arena.dirtyEnd, aligned + resource.size);
            arena.cursor = aligned + resource.size;
            resource.uniformOffsets[activeFrameIndex] = aligned;
            resource.uniformEpochs[activeFrameIndex] = arena.epoch;
            timingStats.uniformBytesUploaded += resource.size;
            return aligned;
        }

        void FlushUniformArena()
        {
            auto &arena = uniformArenas[activeFrameIndex];
            if (arena.dirtyStart == VK_WHOLE_SIZE || arena.dirtyEnd <= arena.dirtyStart) return;
            vmaFlushAllocation(allocator, arena.allocation, arena.dirtyStart, arena.dirtyEnd - arena.dirtyStart);
        }

        void Immediate(const auto &record)
        {
            Check(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer");
            record(commandBuffer);
            Check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &commandBuffer;
            Check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
            Check(vkQueueWaitIdle(queue), "vkQueueWaitIdle");
        }

        void Transition(VkCommandBuffer command, TextureResource &texture, VkImageLayout next,
                        VkPipelineStageFlags destinationStage, VkAccessFlags destinationAccess)
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = texture.layout;
            barrier.newLayout = next;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture.image;
            barrier.subresourceRange = {Aspect(texture), 0, texture.mipLevels, 0, 1};
            barrier.srcAccessMask = texture.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
                                    texture.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ? VK_ACCESS_TRANSFER_WRITE_BIT :
                                    texture.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT :
                                    texture.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_ACCESS_SHADER_READ_BIT :
                                    texture.descriptor.format == Format::D32Float ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            const VkPipelineStageFlags sourceStage = texture.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            vkCmdPipelineBarrier(command, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            texture.layout = next;
        }
    };

    class VulkanCommandContext final : public ICommandContext
    {
    public:
        explicit VulkanCommandContext(VulkanDevice::Impl &impl) : m_impl(impl)
        {
            for (auto &frame : m_frames)
            {
                VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                pool.queueFamilyIndex = m_impl.queueFamily;
                Check(vkCreateCommandPool(m_impl.device, &pool, nullptr, &frame.commandPool), "vkCreateCommandPool(frame)");
                VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                command.commandPool = frame.commandPool;
                command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                command.commandBufferCount = 1;
                Check(vkAllocateCommandBuffers(m_impl.device, &command, &frame.commandBuffer), "vkAllocateCommandBuffers(frame)");
                std::array<VkDescriptorPoolSize, 2> sizes{{
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16384},
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384}}};
                VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                descriptors.maxSets = 32768;
                descriptors.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
                descriptors.pPoolSizes = sizes.data();
                Check(vkCreateDescriptorPool(m_impl.device, &descriptors, nullptr, &frame.descriptorPool),
                      "vkCreateDescriptorPool(frame)");
                VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                Check(vkCreateFence(m_impl.device, &fence, nullptr, &frame.fence), "vkCreateFence(frame)");
                VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
                queries.queryCount = MaxTimestampQueries;
                Check(vkCreateQueryPool(m_impl.device, &queries, nullptr, &frame.queryPool), "vkCreateQueryPool(frame)");
            }
        }

        ~VulkanCommandContext() override
        {
            for (auto &frame : m_frames)
            {
                if (frame.fence) vkDestroyFence(m_impl.device, frame.fence, nullptr);
                if (frame.queryPool) vkDestroyQueryPool(m_impl.device, frame.queryPool, nullptr);
                if (frame.descriptorPool) vkDestroyDescriptorPool(m_impl.device, frame.descriptorPool, nullptr);
                if (frame.commandPool) vkDestroyCommandPool(m_impl.device, frame.commandPool, nullptr);
            }
        }

        void BeginFrame() override
        {
            if (m_recording) throw std::logic_error("Vulkan frame is already recording");
            auto &frame = m_frames[m_frameIndex];
            const auto waitStart = std::chrono::steady_clock::now();
            Check(vkWaitForFences(m_impl.device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(frame)");
            m_impl.timingStats.frameFenceWaitMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - waitStart).count();
            ResolveTimestamps(frame);
            m_impl.timingStats.descriptorAllocationCalls = 0;
            m_impl.timingStats.descriptorSetsAllocated = 0;
            m_impl.timingStats.descriptorWrites = 0;
            m_impl.timingStats.uniformBytesUploaded = 0;
            m_impl.timingStats.descriptorCpuMs = 0.0f;
            m_impl.timingStats.uniformUploadCpuMs = 0.0f;
            Check(vkResetFences(m_impl.device, 1, &frame.fence), "vkResetFences(frame)");
            Check(vkResetCommandPool(m_impl.device, frame.commandPool, 0), "vkResetCommandPool(frame)");
            Check(vkResetDescriptorPool(m_impl.device, frame.descriptorPool, 0), "vkResetDescriptorPool(frame)");
            m_impl.BeginUniformFrame(m_frameIndex);
            m_emptyDescriptorSets.clear();
            m_descriptorSetCache.clear();
            m_lastDescriptorKeyValid.fill(false);
            m_boundDescriptorSets.fill(VK_NULL_HANDLE);
            m_boundDynamicOffsetCounts.fill(0);
            m_boundPipelineLayout = VK_NULL_HANDLE;
            m_descriptorBindingsDirty = true;
            m_boundVertexBuffer = {};
            m_boundIndexBuffer = {};
            m_boundVertexOffset = 0;
            m_boundIndexOffset = 0;
            m_pipeline = nullptr;
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(frame.commandBuffer, &begin), "vkBeginCommandBuffer(frame)");
            vkCmdResetQueryPool(frame.commandBuffer, frame.queryPool, 0, MaxTimestampQueries);
            vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queryPool, 0);
            frame.nextQuery = 2;
            frame.scopes.clear();
            frame.hasTimestamps = false;
            m_impl.activeFrameIndex = m_frameIndex;
            m_recording = true;
        }

        void BeginRendering(const RenderingInfo &info) override
        {
            if (m_rendering) throw std::logic_error("Vulkan rendering is already active");
            m_colors.clear();
            for (const auto handle : info.colorAttachments)
            {
                auto *color = m_impl.textures.Get(handle);
                if (!color) throw std::invalid_argument("Invalid Vulkan color attachment");
                m_colors.push_back(color);
            }
            m_depth = m_impl.textures.Get(info.depthAttachment);
            if ((m_colors.empty() && !m_depth) || (info.depthAttachment && !m_depth))
                throw std::invalid_argument("Invalid Vulkan render attachments");
            if (!m_recording) throw std::logic_error("Vulkan rendering requires BeginFrame");
            for (auto *color : m_colors)
                m_impl.Transition(CommandBuffer(), *color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            if (m_depth)
                m_impl.Transition(CommandBuffer(), *m_depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            std::vector<VkRenderingAttachmentInfo> colors(m_colors.size());
            for (std::size_t index = 0; index < m_colors.size(); ++index)
            {
                colors[index] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                colors[index].imageView = m_colors[index]->view;
                colors[index].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colors[index].loadOp = info.clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                colors[index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                std::memcpy(colors[index].clearValue.color.float32, info.clearColorValue, sizeof(info.clearColorValue));
            }
            VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depth.imageView = m_depth ? m_depth->view : VK_NULL_HANDLE;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.loadOp = info.clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue.depthStencil.depth = info.clearDepthValue;
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea.extent = {info.width, info.height};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
            rendering.pColorAttachments = colors.data();
            rendering.pDepthAttachment = m_depth ? &depth : nullptr;
            vkCmdBeginRendering(CommandBuffer(), &rendering);
            SetViewport({0, 0, static_cast<float>(info.width), static_cast<float>(info.height), 0, 1});
            SetScissor({0, 0, info.width, info.height});
            m_rendering = true;
        }

        void EndRendering() override
        {
            if (!m_rendering) throw std::logic_error("Vulkan rendering is not active");
            vkCmdEndRendering(CommandBuffer());
            m_rendering = false;
        }

        void BeginGpuScope(std::string_view name) override
        {
            auto &frame = m_frames[m_frameIndex];
            if (!m_recording || m_activeScope || frame.nextQuery + 1 >= MaxTimestampQueries) return;
            m_activeScope = true;
            m_activeScopeName.assign(name);
            m_activeScopeStart = frame.nextQuery++;
            m_activeScopeCpuStart = std::chrono::steady_clock::now();
            vkCmdWriteTimestamp(CommandBuffer(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queryPool, m_activeScopeStart);
        }

        void EndGpuScope() override
        {
            if (!m_activeScope) return;
            auto &frame = m_frames[m_frameIndex];
            const auto end = frame.nextQuery++;
            vkCmdWriteTimestamp(CommandBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queryPool, end);
            const float cpuMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - m_activeScopeCpuStart).count();
            frame.scopes.push_back({m_activeScopeName, m_activeScopeStart, end, cpuMs});
            m_activeScope = false;
        }

        void Submit() override
        {
            if (m_rendering) throw std::logic_error("Cannot submit while Vulkan rendering is active");
            if (!m_recording) return;
            auto &frame = m_frames[m_frameIndex];
            if (m_activeScope) EndGpuScope();
            m_impl.FlushUniformArena();
            vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queryPool, 1);
            frame.hasTimestamps = true;
            Check(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &frame.commandBuffer;
            Check(vkQueueSubmit(m_impl.queue, 1, &submit, frame.fence), "vkQueueSubmit(frame)");
            m_recording = false;
            m_frameIndex = (m_frameIndex + 1) % m_frames.size();
        }

        void SetViewport(const Viewport &v) override
        {
            VkViewport viewport{v.x, v.y + v.height, v.width, -v.height, v.minDepth, v.maxDepth};
            vkCmdSetViewport(CommandBuffer(), 0, 1, &viewport);
        }
        void SetScissor(const Scissor &s) override
        {
            VkRect2D scissor{{s.x, s.y}, {s.width, s.height}};
            vkCmdSetScissor(CommandBuffer(), 0, 1, &scissor);
        }
        void BindPipeline(PipelineHandle handle) override
        {
            auto *pipeline = m_impl.pipelines.Get(handle);
            if (!pipeline) throw std::invalid_argument("Invalid or stale Vulkan pipeline");
            if (m_pipeline != pipeline)
            {
                if (!m_pipeline || m_pipeline->layout != pipeline->layout)
                {
                    m_boundDescriptorSets.fill(VK_NULL_HANDLE);
                    m_boundDynamicOffsetCounts.fill(0);
                    m_boundPipelineLayout = pipeline->layout;
                }
                m_pipeline = pipeline;
                m_descriptorBindingsDirty = true;
                vkCmdBindPipeline(CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->pipeline);
            }
        }
        void BindVertexBuffer(BufferHandle handle, std::size_t offset) override
        {
            auto *resource = m_impl.buffers.Get(handle);
            if (!resource || resource->usage != BufferUsage::Vertex) throw std::invalid_argument("Invalid Vulkan vertex buffer");
            if (m_boundVertexBuffer == handle && m_boundVertexOffset == offset) return;
            const VkDeviceSize vkOffset = offset;
            vkCmdBindVertexBuffers(CommandBuffer(), 0, 1, &resource->buffer, &vkOffset);
            m_boundVertexBuffer = handle;
            m_boundVertexOffset = offset;
        }
        void BindIndexBuffer(BufferHandle handle, Format format, std::size_t offset) override
        {
            auto *resource = m_impl.buffers.Get(handle);
            if (!resource || resource->usage != BufferUsage::Index || format != Format::R32Uint) throw std::invalid_argument("Invalid Vulkan index buffer");
            if (m_boundIndexBuffer == handle && m_boundIndexOffset == offset) return;
            vkCmdBindIndexBuffer(CommandBuffer(), resource->buffer, offset, VK_INDEX_TYPE_UINT32);
            m_boundIndexBuffer = handle;
            m_boundIndexOffset = offset;
        }
        void BindUniformBuffer(std::uint32_t slot, BufferHandle handle) override
        {
            auto *resource = m_impl.buffers.Get(handle);
            if (!m_pipeline || slot >= MaxResourceSlots || !resource || resource->usage != BufferUsage::Uniform)
                throw std::invalid_argument("Invalid Vulkan uniform binding");
            auto *previous = m_impl.buffers.Get(m_uniformBuffers[slot]);
            // Dynamic offsets select the individual buffer contents. The descriptor itself only
            // changes when its range changes, so equally-sized per-draw buffers can share a set.
            if (!previous || previous->size != resource->size)
                m_descriptorBindingsDirty = true;
            m_uniformBuffers[slot] = handle;
        }
        void BindTexture(std::uint32_t slot, TextureHandle textureHandle, SamplerHandle samplerHandle) override
        {
            auto *texture = m_impl.textures.Get(textureHandle); auto *sampler = m_impl.samplers.Get(samplerHandle);
            if (!m_pipeline || slot >= MaxResourceSlots || !texture || !sampler)
                throw std::invalid_argument("Invalid Vulkan texture binding");
            if (texture->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                m_impl.Transition(CommandBuffer(), *texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
            if (m_sampledTextures[slot] != textureHandle || m_samplers[slot] != samplerHandle)
                m_descriptorBindingsDirty = true;
            m_sampledTextures[slot] = textureHandle;
            m_samplers[slot] = samplerHandle;
        }
        void Draw(std::uint32_t count, std::uint32_t firstVertex) override
        {
            PrepareDraw();
            vkCmdDraw(CommandBuffer(), count, 1, firstVertex, 0);
        }

        void DrawIndexed(std::uint32_t count, std::uint32_t firstIndex, std::int32_t vertexOffset) override
        {
            PrepareDraw();
            vkCmdDrawIndexed(CommandBuffer(), count, 1, firstIndex, vertexOffset, 0);
        }

    private:
        struct DescriptorSetKey
        {
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            std::array<std::uint64_t, 32> resources{};
            std::uint8_t resourceCount = 0;
            bool operator==(const DescriptorSetKey &) const = default;
        };
        struct DescriptorSetKeyHash
        {
            std::size_t operator()(const DescriptorSetKey &key) const noexcept
            {
                std::size_t hash = std::hash<VkDescriptorSetLayout>{}(key.layout);
                for (std::uint8_t index = 0; index < key.resourceCount; ++index)
                    hash ^= std::hash<std::uint64_t>{}(key.resources[index]) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
                return hash;
            }
        };
        template <typename Tag>
        static std::uint64_t EncodeHandle(Handle<Tag> handle) noexcept
        {
            return (static_cast<std::uint64_t>(handle.generation) << 32u) | handle.index;
        }

        void PrepareDraw()
        {
            const auto descriptorStart = std::chrono::steady_clock::now();
            if (!m_rendering || !m_pipeline) throw std::logic_error("Vulkan draw requires active rendering and pipeline");
            const auto setCount = m_pipeline->setLayouts.size();
            if (setCount > MaxDescriptorSets || m_pipeline->descriptor.resourceBindings.size() > MaxDescriptorBindings)
                throw std::logic_error("Vulkan pipeline exceeds fixed descriptor scratch capacity");
            if (!m_descriptorBindingsDirty)
            {
                BindPreparedDescriptorSets();
                m_impl.timingStats.descriptorCpuMs += std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - descriptorStart).count();
                return;
            }
            std::array<VkDescriptorSet, MaxDescriptorSets> sets{};
            std::array<bool, MaxDescriptorSets> newlyAllocated{};
            std::array<DescriptorSetKey, MaxDescriptorSets> keys{};
            std::array<std::uint32_t, MaxDescriptorBindings> dynamicOffsets{};
            std::array<std::size_t, MaxDescriptorSets> dynamicOffsetStarts{};
            std::array<std::size_t, MaxDescriptorSets> dynamicOffsetCounts{};
            std::size_t dynamicOffsetCount = 0;
            std::array<VkDescriptorSetLayout, MaxDescriptorSets> layoutsToAllocate{};
            std::array<std::size_t, MaxDescriptorSets> allocatedSetIndices{};
            std::size_t allocationCount = 0;
            for (std::size_t setIndex = 0; setIndex < setCount; ++setIndex)
            {
                const auto layout = m_pipeline->setLayouts[setIndex];
                auto &key = keys[setIndex];
                key.layout = layout;
                dynamicOffsetStarts[setIndex] = dynamicOffsetCount;
                for (const auto &binding : m_pipeline->bindingsBySet[setIndex])
                {
                    if (binding.type == ResourceBindingType::UniformBuffer)
                    {
                        auto *buffer = binding.slot < MaxResourceSlots
                            ? m_impl.buffers.Get(m_uniformBuffers[binding.slot]) : nullptr;
                        if (!buffer) throw std::logic_error("Vulkan draw has an incomplete uniform binding");
                        const auto offset = m_impl.EnsureUniformResident(*buffer);
                        if (offset > std::numeric_limits<std::uint32_t>::max())
                            throw std::runtime_error("Vulkan dynamic uniform offset exceeds uint32 range");
                        dynamicOffsets[dynamicOffsetCount++] = static_cast<std::uint32_t>(offset);
                        key.resources[key.resourceCount++] = buffer->size;
                    }
                    else
                    {
                        if (binding.slot >= MaxResourceSlots || !m_sampledTextures[binding.slot] || !m_samplers[binding.slot])
                            throw std::logic_error("Vulkan draw has an incomplete texture binding");
                        if (key.resourceCount + 2 > key.resources.size())
                            throw std::logic_error("Vulkan descriptor set exceeds fixed cache key capacity");
                        key.resources[key.resourceCount++] = EncodeHandle(m_sampledTextures[binding.slot]);
                        key.resources[key.resourceCount++] = EncodeHandle(m_samplers[binding.slot]);
                    }
                }
                dynamicOffsetCounts[setIndex] = dynamicOffsetCount - dynamicOffsetStarts[setIndex];
                if (m_lastDescriptorKeyValid[setIndex] && m_lastDescriptorKeys[setIndex] == key)
                {
                    sets[setIndex] = m_lastDescriptorSets[setIndex];
                    continue;
                }
                if (const auto found = m_descriptorSetCache.find(key); found != m_descriptorSetCache.end())
                {
                    sets[setIndex] = found->second;
                    m_lastDescriptorKeys[setIndex] = key;
                    m_lastDescriptorSets[setIndex] = found->second;
                    m_lastDescriptorKeyValid[setIndex] = true;
                    continue;
                }
                layoutsToAllocate[allocationCount] = layout;
                allocatedSetIndices[allocationCount++] = setIndex;
            }
            if (allocationCount != 0)
            {
                std::array<VkDescriptorSet, MaxDescriptorSets> allocatedSets{};
                VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocation.descriptorPool = DescriptorPool();
                allocation.descriptorSetCount = static_cast<std::uint32_t>(allocationCount);
                allocation.pSetLayouts = layoutsToAllocate.data();
                Check(vkAllocateDescriptorSets(m_impl.device, &allocation, allocatedSets.data()),
                      "vkAllocateDescriptorSets(draw)");
                ++m_impl.timingStats.descriptorAllocationCalls;
                m_impl.timingStats.descriptorSetsAllocated += allocationCount;
                for (std::size_t allocationIndex = 0; allocationIndex < allocationCount; ++allocationIndex)
                {
                    const auto setIndex = allocatedSetIndices[allocationIndex];
                    sets[setIndex] = allocatedSets[allocationIndex];
                    newlyAllocated[setIndex] = true;
                    m_descriptorSetCache.emplace(keys[setIndex], allocatedSets[allocationIndex]);
                    m_lastDescriptorKeys[setIndex] = keys[setIndex];
                    m_lastDescriptorSets[setIndex] = allocatedSets[allocationIndex];
                    m_lastDescriptorKeyValid[setIndex] = true;
                }
            }
            std::array<VkDescriptorBufferInfo, MaxDescriptorBindings> buffers{};
            std::array<VkDescriptorImageInfo, MaxDescriptorBindings> images{};
            std::array<VkWriteDescriptorSet, MaxDescriptorBindings> writes{};
            std::size_t bufferCount = 0, imageCount = 0, writeCount = 0;
            for (std::size_t setIndex = 0; setIndex < setCount; ++setIndex)
            {
                if (!newlyAllocated[setIndex]) continue;
                for (const auto &binding : m_pipeline->bindingsBySet[setIndex])
                {
                    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = sets[setIndex];
                    write.dstBinding = binding.binding;
                    write.descriptorCount = 1;
                    if (binding.type == ResourceBindingType::UniformBuffer)
                    {
                        auto *buffer = m_impl.buffers.Get(m_uniformBuffers[binding.slot]);
                        buffers[bufferCount] = {m_impl.uniformArenas[m_impl.activeFrameIndex].buffer, 0, buffer->size};
                        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                        write.pBufferInfo = &buffers[bufferCount++];
                    }
                    else
                    {
                        auto *texture = m_impl.textures.Get(m_sampledTextures[binding.slot]);
                        auto *sampler = m_impl.samplers.Get(m_samplers[binding.slot]);
                        images[imageCount] = {sampler->sampler, texture->view, texture->layout};
                        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        write.pImageInfo = &images[imageCount++];
                    }
                    writes[writeCount++] = write;
                }
            }
            vkUpdateDescriptorSets(m_impl.device, static_cast<std::uint32_t>(writeCount), writes.data(), 0, nullptr);
            m_impl.timingStats.descriptorWrites += writeCount;
            for (std::size_t setIndex = 0; setIndex < setCount; ++setIndex)
            {
                const auto offsetStart = dynamicOffsetStarts[setIndex];
                const auto offsetCount = dynamicOffsetCounts[setIndex];
                const bool offsetsMatch = m_boundDynamicOffsetCounts[setIndex] == offsetCount &&
                    std::equal(dynamicOffsets.begin() + offsetStart,
                               dynamicOffsets.begin() + offsetStart + offsetCount,
                               m_boundDynamicOffsets[setIndex].begin());
                if (m_boundPipelineLayout == m_pipeline->layout &&
                    m_boundDescriptorSets[setIndex] == sets[setIndex] && offsetsMatch)
                    continue;
                vkCmdBindDescriptorSets(CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout,
                                        static_cast<std::uint32_t>(setIndex), 1, &sets[setIndex],
                                        static_cast<std::uint32_t>(offsetCount), dynamicOffsets.data() + offsetStart);
                m_boundDescriptorSets[setIndex] = sets[setIndex];
                m_boundDynamicOffsetCounts[setIndex] = offsetCount;
                std::copy_n(dynamicOffsets.begin() + offsetStart, offsetCount,
                            m_boundDynamicOffsets[setIndex].begin());
            }
            m_boundPipelineLayout = m_pipeline->layout;
            std::copy_n(sets.begin(), setCount, m_preparedDescriptorSets.begin());
            m_descriptorBindingsDirty = false;
            m_impl.timingStats.descriptorCpuMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - descriptorStart).count();
        }

        void BindPreparedDescriptorSets()
        {
            std::array<std::uint32_t, MaxDescriptorBindings> dynamicOffsets{};
            std::size_t dynamicOffsetCount = 0;
            for (std::size_t setIndex = 0; setIndex < m_pipeline->setLayouts.size(); ++setIndex)
            {
                const auto offsetStart = dynamicOffsetCount;
                for (const auto &binding : m_pipeline->bindingsBySet[setIndex])
                {
                    if (binding.type != ResourceBindingType::UniformBuffer) continue;
                    auto *buffer = binding.slot < MaxResourceSlots
                        ? m_impl.buffers.Get(m_uniformBuffers[binding.slot]) : nullptr;
                    if (!buffer) throw std::logic_error("Vulkan draw has an incomplete uniform binding");
                    const auto offset = m_impl.EnsureUniformResident(*buffer);
                    if (offset > std::numeric_limits<std::uint32_t>::max())
                        throw std::runtime_error("Vulkan dynamic uniform offset exceeds uint32 range");
                    dynamicOffsets[dynamicOffsetCount++] = static_cast<std::uint32_t>(offset);
                }
                const auto offsetCount = dynamicOffsetCount - offsetStart;
                const bool offsetsMatch = m_boundDynamicOffsetCounts[setIndex] == offsetCount &&
                    std::equal(dynamicOffsets.begin() + offsetStart,
                               dynamicOffsets.begin() + dynamicOffsetCount,
                               m_boundDynamicOffsets[setIndex].begin());
                if (m_boundPipelineLayout == m_pipeline->layout &&
                    m_boundDescriptorSets[setIndex] == m_preparedDescriptorSets[setIndex] && offsetsMatch)
                    continue;
                vkCmdBindDescriptorSets(CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout,
                                        static_cast<std::uint32_t>(setIndex), 1, &m_preparedDescriptorSets[setIndex],
                                        static_cast<std::uint32_t>(offsetCount), dynamicOffsets.data() + offsetStart);
                m_boundDescriptorSets[setIndex] = m_preparedDescriptorSets[setIndex];
                m_boundDynamicOffsetCounts[setIndex] = offsetCount;
                std::copy_n(dynamicOffsets.begin() + offsetStart, offsetCount,
                            m_boundDynamicOffsets[setIndex].begin());
            }
            m_boundPipelineLayout = m_pipeline->layout;
        }

        VulkanDevice::Impl &m_impl;
        struct FrameResources
        {
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            VkQueryPool queryPool = VK_NULL_HANDLE;
            struct Scope { std::string name; std::uint32_t start = 0; std::uint32_t end = 0; float cpuMs = 0.0f; };
            std::vector<Scope> scopes;
            std::uint32_t nextQuery = 2;
            bool hasTimestamps = false;
        };
        static constexpr std::size_t FrameCount = 3;
        static constexpr std::size_t MaxDescriptorSets = 32;
        static constexpr std::size_t MaxDescriptorBindings = 32;
        static constexpr std::size_t MaxResourceSlots = 32;
        static constexpr std::uint32_t MaxTimestampQueries = 32;
        void ResolveTimestamps(FrameResources &frame)
        {
            if (!frame.hasTimestamps) return;
            std::array<std::uint64_t, MaxTimestampQueries> values{};
            Check(vkGetQueryPoolResults(m_impl.device, frame.queryPool, 0, frame.nextQuery,
                                        sizeof(values), values.data(), sizeof(std::uint64_t),
                                        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                  "vkGetQueryPoolResults(frame)");
            const auto milliseconds = [&](std::uint32_t start, std::uint32_t end)
            {
                return static_cast<float>(values[end] - values[start]) * m_impl.timestampPeriodNs * 1.0e-6f;
            };
            m_impl.timingStats.frameGpuMs = milliseconds(0, 1);
            m_impl.timingStats.gpuScopes.clear();
            for (const auto &scope : frame.scopes)
                m_impl.timingStats.gpuScopes.push_back({scope.name, milliseconds(scope.start, scope.end), scope.cpuMs});
            m_impl.timingStats.hasGpuResult = true;
        }
        [[nodiscard]] VkCommandBuffer CommandBuffer() const { return m_frames[m_frameIndex].commandBuffer; }
        [[nodiscard]] VkDescriptorPool DescriptorPool() const { return m_frames[m_frameIndex].descriptorPool; }
        std::array<FrameResources, FrameCount> m_frames;
        std::size_t m_frameIndex = 0;
        PipelineResource *m_pipeline = nullptr;
        std::vector<TextureResource *> m_colors;
        TextureResource *m_depth = nullptr;
        std::array<BufferHandle, MaxResourceSlots> m_uniformBuffers{};
        std::array<TextureHandle, MaxResourceSlots> m_sampledTextures{};
        std::array<SamplerHandle, MaxResourceSlots> m_samplers{};
        std::unordered_map<VkDescriptorSetLayout, VkDescriptorSet> m_emptyDescriptorSets;
        std::unordered_map<DescriptorSetKey, VkDescriptorSet, DescriptorSetKeyHash> m_descriptorSetCache;
        std::array<DescriptorSetKey, MaxDescriptorSets> m_lastDescriptorKeys{};
        std::array<VkDescriptorSet, MaxDescriptorSets> m_lastDescriptorSets{};
        std::array<bool, MaxDescriptorSets> m_lastDescriptorKeyValid{};
        std::array<VkDescriptorSet, MaxDescriptorSets> m_boundDescriptorSets{};
        std::array<VkDescriptorSet, MaxDescriptorSets> m_preparedDescriptorSets{};
        std::array<std::array<std::uint32_t, MaxDescriptorBindings>, MaxDescriptorSets> m_boundDynamicOffsets{};
        std::array<std::size_t, MaxDescriptorSets> m_boundDynamicOffsetCounts{};
        BufferHandle m_boundVertexBuffer{};
        BufferHandle m_boundIndexBuffer{};
        std::size_t m_boundVertexOffset = 0;
        std::size_t m_boundIndexOffset = 0;
        VkPipelineLayout m_boundPipelineLayout = VK_NULL_HANDLE;
        bool m_descriptorBindingsDirty = true;
        bool m_recording = false;
        bool m_rendering = false;
        bool m_activeScope = false;
        std::string m_activeScopeName;
        std::uint32_t m_activeScopeStart = 0;
        std::chrono::steady_clock::time_point m_activeScopeCpuStart{};
    };

    class VulkanSwapchain final : public ISwapchain
    {
    public:
        VulkanSwapchain(VulkanDevice::Impl &impl, const SwapchainDescriptor &descriptor)
            : m_impl(impl), m_width(descriptor.width), m_height(descriptor.height), m_vSync(descriptor.vSync)
        {
            if (!m_impl.surface)
                throw std::logic_error("Vulkan device was not created for presentation");
            for (auto &frame : m_frames)
            {
                VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                pool.queueFamilyIndex = m_impl.queueFamily;
                Check(vkCreateCommandPool(m_impl.device, &pool, nullptr, &frame.commandPool), "vkCreateCommandPool(presentation)");
                VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                allocation.commandPool = frame.commandPool; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount = 1;
                Check(vkAllocateCommandBuffers(m_impl.device, &allocation, &frame.commandBuffer), "vkAllocateCommandBuffers(presentation)");
                VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                Check(vkCreateSemaphore(m_impl.device, &semaphore, nullptr, &frame.imageAvailable), "vkCreateSemaphore(image available)");
                Check(vkCreateSemaphore(m_impl.device, &semaphore, nullptr, &frame.renderFinished), "vkCreateSemaphore(render finished)");
                VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                Check(vkCreateFence(m_impl.device, &fence, nullptr, &frame.fence), "vkCreateFence(presentation)");
            }
            Recreate();
        }

        ~VulkanSwapchain() override
        {
            vkDeviceWaitIdle(m_impl.device);
            DestroySwapchain();
            for (auto &frame : m_frames)
            {
                if (frame.fence) vkDestroyFence(m_impl.device, frame.fence, nullptr);
                if (frame.renderFinished) vkDestroySemaphore(m_impl.device, frame.renderFinished, nullptr);
                if (frame.imageAvailable) vkDestroySemaphore(m_impl.device, frame.imageAvailable, nullptr);
                if (frame.commandPool) vkDestroyCommandPool(m_impl.device, frame.commandPool, nullptr);
            }
        }

        [[nodiscard]] Format GetFormat() const noexcept override { return m_format; }
        [[nodiscard]] std::uint32_t GetWidth() const noexcept override { return m_width; }
        [[nodiscard]] std::uint32_t GetHeight() const noexcept override { return m_height; }

        bool Resize(std::uint32_t width, std::uint32_t height) override
        {
            if (width == 0 || height == 0)
                return false;
            m_width = width;
            m_height = height;
            Recreate();
            return true;
        }

        bool Present(TextureHandle sourceHandle) override
        {
            auto *source = m_impl.textures.Get(sourceHandle);
            if (!source || source->descriptor.usage != TextureUsage::ColorAttachment || !m_swapchain)
                return false;

            auto &frame = m_frames[m_frameIndex];
            Check(vkWaitForFences(m_impl.device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(presentation)");
            std::uint32_t imageIndex = 0;
            VkResult acquired = vkAcquireNextImageKHR(m_impl.device, m_swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
            if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
            {
                Recreate();
                return false;
            }
            if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
                Check(acquired, "vkAcquireNextImageKHR");

            Check(vkResetFences(m_impl.device, 1, &frame.fence), "vkResetFences(presentation)");
            Check(vkResetCommandPool(m_impl.device, frame.commandPool, 0), "vkResetCommandPool(presentation)");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(frame.commandBuffer, &begin), "vkBeginCommandBuffer(presentation)");
            m_impl.Transition(frame.commandBuffer, *source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

            VkImageMemoryBarrier destination{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            destination.oldLayout = m_presented[imageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
            destination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            destination.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            destination.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            destination.image = m_images[imageIndex];
            destination.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            destination.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(frame.commandBuffer,
                                 m_presented[imageIndex] ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &destination);

            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.srcOffsets[1] = {static_cast<std::int32_t>(source->descriptor.width), static_cast<std::int32_t>(source->descriptor.height), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstOffsets[1] = {static_cast<std::int32_t>(m_width), static_cast<std::int32_t>(m_height), 1};
            vkCmdBlitImage(frame.commandBuffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            destination.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            destination.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            destination.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            destination.dstAccessMask = 0;
            vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &destination);
            Check(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer(presentation)");

            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &frame.imageAvailable; submit.pWaitDstStageMask = &waitStage;
            submit.commandBufferCount = 1; submit.pCommandBuffers = &frame.commandBuffer;
            submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &frame.renderFinished;
            Check(vkQueueSubmit(m_impl.queue, 1, &submit, frame.fence), "vkQueueSubmit(presentation)");

            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present.waitSemaphoreCount = 1; present.pWaitSemaphores = &frame.renderFinished;
            present.swapchainCount = 1; present.pSwapchains = &m_swapchain; present.pImageIndices = &imageIndex;
            const VkResult result = vkQueuePresentKHR(m_impl.presentQueue, &present);
            m_presented[imageIndex] = true;
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            {
                Recreate();
                return true;
            }
            Check(result, "vkQueuePresentKHR");
            m_frameIndex = (m_frameIndex + 1) % m_frames.size();
            return true;
        }

    private:
        void DestroySwapchain()
        {
            if (m_swapchain) vkDestroySwapchainKHR(m_impl.device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
            m_images.clear();
            m_presented.clear();
        }

        void Recreate()
        {
            vkDeviceWaitIdle(m_impl.device);
            VkSurfaceCapabilitiesKHR capabilities{};
            Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_impl.physicalDevice, m_impl.surface, &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
            std::uint32_t formatCount = 0;
            Check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_impl.physicalDevice, m_impl.surface, &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
            if (!formatCount) throw std::runtime_error("Vulkan surface has no supported formats");
            std::vector<VkSurfaceFormatKHR> formats(formatCount);
            Check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_impl.physicalDevice, m_impl.surface, &formatCount, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
            if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
                throw std::runtime_error("Vulkan surface images do not support transfer destinations");
            auto selected = formats.front();
            for (const auto &candidate : formats)
                if (candidate.format == VK_FORMAT_R8G8B8A8_SRGB || candidate.format == VK_FORMAT_B8G8R8A8_SRGB) { selected = candidate; break; }
            m_format = selected.format == VK_FORMAT_R8G8B8A8_SRGB || selected.format == VK_FORMAT_B8G8R8A8_SRGB
                           ? Format::R8G8B8A8Srgb : Format::R8G8B8A8Unorm;

            VkExtent2D extent = capabilities.currentExtent;
            if (extent.width == UINT32_MAX)
            {
                extent.width = std::clamp(m_width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
                extent.height = std::clamp(m_height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            }
            m_width = extent.width; m_height = extent.height;
            std::uint32_t presentModeCount = 0;
            Check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_impl.physicalDevice, m_impl.surface, &presentModeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
            std::vector<VkPresentModeKHR> presentModes(presentModeCount);
            Check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_impl.physicalDevice, m_impl.surface, &presentModeCount, presentModes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
            VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
            if (!m_vSync)
            {
                if (std::ranges::find(presentModes, VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end())
                    presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                else if (std::ranges::find(presentModes, VK_PRESENT_MODE_IMMEDIATE_KHR) != presentModes.end())
                    presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
            VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            constexpr std::array compositeCandidates{
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
            for (const auto candidate : compositeCandidates)
                if (capabilities.supportedCompositeAlpha & candidate) { compositeAlpha = candidate; break; }
            const std::uint32_t imageCount = std::min(capabilities.minImageCount + 1,
                                                       capabilities.maxImageCount ? capabilities.maxImageCount : UINT32_MAX);
            VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
            info.surface = m_impl.surface; info.minImageCount = imageCount; info.imageFormat = selected.format; info.imageColorSpace = selected.colorSpace;
            info.imageExtent = extent; info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            const std::array queueFamilies{m_impl.queueFamily, m_impl.presentQueueFamily};
            if (queueFamilies[0] != queueFamilies[1]) { info.imageSharingMode = VK_SHARING_MODE_CONCURRENT; info.queueFamilyIndexCount = 2; info.pQueueFamilyIndices = queueFamilies.data(); }
            else info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.preTransform = capabilities.currentTransform; info.compositeAlpha = compositeAlpha;
            info.presentMode = presentMode;
            info.clipped = VK_TRUE; info.oldSwapchain = m_swapchain;
            VkSwapchainKHR replacement = VK_NULL_HANDLE;
            Check(vkCreateSwapchainKHR(m_impl.device, &info, nullptr, &replacement), "vkCreateSwapchainKHR");
            if (m_swapchain) vkDestroySwapchainKHR(m_impl.device, m_swapchain, nullptr);
            m_swapchain = replacement;
            std::uint32_t actualImageCount = 0;
            Check(vkGetSwapchainImagesKHR(m_impl.device, m_swapchain, &actualImageCount, nullptr), "vkGetSwapchainImagesKHR");
            m_images.resize(actualImageCount);
            Check(vkGetSwapchainImagesKHR(m_impl.device, m_swapchain, &actualImageCount, m_images.data()), "vkGetSwapchainImagesKHR");
            m_presented.assign(actualImageCount, false);
        }

        VulkanDevice::Impl &m_impl;
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        struct FrameResources
        {
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkSemaphore imageAvailable = VK_NULL_HANDLE;
            VkSemaphore renderFinished = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
        };
        std::array<FrameResources, 2> m_frames;
        std::size_t m_frameIndex = 0;
        std::vector<VkImage> m_images;
        std::vector<bool> m_presented;
        Format m_format = Format::R8G8B8A8Srgb;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
        bool m_vSync = true;
    };

    VulkanDevice::VulkanDevice() : VulkanDevice(SwapchainDescriptor{}) {}

    VulkanDevice::VulkanDevice(const SwapchainDescriptor &presentation) : m_impl(std::make_unique<Impl>())
    {
        Check(volkInitialize(), "volkInitialize");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "PlutoGE"; app.pEngineName = "PlutoGE"; app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instanceInfo.pApplicationInfo = &app;
        std::uint32_t instanceExtensionCount = 0;
        const char **instanceExtensions = nullptr;
        if (presentation.nativeWindow)
        {
            instanceExtensions = glfwGetRequiredInstanceExtensions(&instanceExtensionCount);
            if (!instanceExtensions || instanceExtensionCount == 0)
                throw std::runtime_error("GLFW did not provide Vulkan surface extensions");
            instanceInfo.enabledExtensionCount = instanceExtensionCount;
            instanceInfo.ppEnabledExtensionNames = instanceExtensions;
        }
        Check(vkCreateInstance(&instanceInfo, nullptr, &m_impl->instance), "vkCreateInstance");
        volkLoadInstance(m_impl->instance);
        if (presentation.nativeWindow)
            Check(glfwCreateWindowSurface(m_impl->instance, static_cast<GLFWwindow *>(presentation.nativeWindow), nullptr, &m_impl->surface), "glfwCreateWindowSurface");
        std::uint32_t count = 0; Check(vkEnumeratePhysicalDevices(m_impl->instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        if (!count) throw std::runtime_error("No Vulkan physical device is available");
        std::vector<VkPhysicalDevice> devices(count); Check(vkEnumeratePhysicalDevices(m_impl->instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
        for (auto device : devices)
        {
            std::uint32_t familyCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount); vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
            for (std::uint32_t graphicsFamily = 0; graphicsFamily < familyCount; ++graphicsFamily)
            {
                if (!(families[graphicsFamily].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                    continue;
                if (!m_impl->surface)
                {
                    m_impl->physicalDevice = device;
                    m_impl->queueFamily = graphicsFamily;
                    m_impl->presentQueueFamily = graphicsFamily;
                    break;
                }
                for (std::uint32_t presentFamily = 0; presentFamily < familyCount; ++presentFamily)
                {
                    VkBool32 supportsPresentation = VK_TRUE;
                    if (m_impl->surface)
                        Check(vkGetPhysicalDeviceSurfaceSupportKHR(device, presentFamily, m_impl->surface, &supportsPresentation), "vkGetPhysicalDeviceSurfaceSupportKHR");
                    if (!supportsPresentation)
                        continue;
                    m_impl->physicalDevice = device;
                    m_impl->queueFamily = graphicsFamily;
                    m_impl->presentQueueFamily = presentFamily;
                    break;
                }
                if (m_impl->physicalDevice) break;
            }
            if (m_impl->physicalDevice) break;
        }
        if (!m_impl->physicalDevice) throw std::runtime_error("No Vulkan graphics queue is available");
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(m_impl->physicalDevice, &properties); m_impl->deviceName = properties.deviceName; m_impl->timestampPeriodNs = properties.limits.timestampPeriod;
        const float priority = 1.0f;
        std::array<VkDeviceQueueCreateInfo, 2> queues{};
        queues[0] = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queues[0].queueFamilyIndex = m_impl->queueFamily; queues[0].queueCount = 1; queues[0].pQueuePriorities = &priority;
        std::uint32_t queueCount = 1;
        if (m_impl->surface && m_impl->presentQueueFamily != m_impl->queueFamily)
        {
            queues[1] = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queues[1].queueFamilyIndex = m_impl->presentQueueFamily; queues[1].queueCount = 1; queues[1].pQueuePriorities = &priority;
            queueCount = 2;
        }
        VkPhysicalDeviceDynamicRenderingFeatures dynamic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES}; dynamic.dynamicRendering = VK_TRUE;
        const char *swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pNext = &dynamic; deviceInfo.queueCreateInfoCount = queueCount; deviceInfo.pQueueCreateInfos = queues.data();
        if (m_impl->surface) { deviceInfo.enabledExtensionCount = 1; deviceInfo.ppEnabledExtensionNames = &swapchainExtension; }
        Check(vkCreateDevice(m_impl->physicalDevice, &deviceInfo, nullptr, &m_impl->device), "vkCreateDevice"); volkLoadDevice(m_impl->device); vkGetDeviceQueue(m_impl->device, m_impl->queueFamily, 0, &m_impl->queue); vkGetDeviceQueue(m_impl->device, m_impl->presentQueueFamily, 0, &m_impl->presentQueue);
        VmaVulkanFunctions functions{}; functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr; functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo allocator{}; allocator.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT; allocator.vulkanApiVersion = VK_API_VERSION_1_3; allocator.instance = m_impl->instance; allocator.physicalDevice = m_impl->physicalDevice; allocator.device = m_impl->device; allocator.pVulkanFunctions = &functions;
        Check(vmaCreateAllocator(&allocator, &m_impl->allocator), "vmaCreateAllocator");
        m_impl->uniformAlignment = std::max<VkDeviceSize>(properties.limits.minUniformBufferOffsetAlignment, 1);
        for (auto &arena : m_impl->uniformArenas)
        {
            VkBufferCreateInfo uniformBuffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            uniformBuffer.size = Impl::UniformArenaSize;
            uniformBuffer.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            VmaAllocationCreateInfo uniformAllocation{};
            uniformAllocation.usage = VMA_MEMORY_USAGE_AUTO;
            uniformAllocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo allocationInfo{};
            Check(vmaCreateBuffer(m_impl->allocator, &uniformBuffer, &uniformAllocation,
                                  &arena.buffer, &arena.allocation, &allocationInfo),
                  "vmaCreateBuffer(uniform arena)");
            arena.mapped = allocationInfo.pMappedData;
        }
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = m_impl->queueFamily; Check(vkCreateCommandPool(m_impl->device, &pool, nullptr, &m_impl->commandPool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; command.commandPool = m_impl->commandPool; command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command.commandBufferCount = 1; Check(vkAllocateCommandBuffers(m_impl->device, &command, &m_impl->commandBuffer), "vkAllocateCommandBuffers");
        std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16384}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384}}};
        VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; descriptors.maxSets = 32768; descriptors.poolSizeCount = sizes.size(); descriptors.pPoolSizes = sizes.data(); Check(vkCreateDescriptorPool(m_impl->device, &descriptors, nullptr, &m_impl->descriptorPool), "vkCreateDescriptorPool");
        m_impl->context = std::make_unique<VulkanCommandContext>(*m_impl);
    }

    VulkanDevice::~VulkanDevice()
    {
        if (!m_impl) return;
        if (m_impl->device) vkDeviceWaitIdle(m_impl->device);
        m_impl->context.reset();
        for (auto &slot : m_impl->readbacks)
        {
            // VMA_ALLOCATION_CREATE_MAPPED_BIT owns the persistent mapping.
            // No matching vmaMapMemory call was made, so destroying the
            // allocation is also responsible for releasing that mapping.
            if (slot.buffer) vmaDestroyBuffer(m_impl->allocator, slot.buffer, slot.allocation);
            if (slot.fence) vkDestroyFence(m_impl->device, slot.fence, nullptr);
            if (slot.commandPool) vkDestroyCommandPool(m_impl->device, slot.commandPool, nullptr);
        }
        m_impl->pipelines.ForEach([&](auto &r) { vkDestroyPipeline(m_impl->device, r.pipeline, nullptr); vkDestroyPipelineLayout(m_impl->device, r.layout, nullptr); for (auto l : r.setLayouts) vkDestroyDescriptorSetLayout(m_impl->device, l, nullptr); });
        m_impl->samplers.ForEach([&](auto &r) { vkDestroySampler(m_impl->device, r.sampler, nullptr); });
        m_impl->textures.ForEach([&](auto &r) { vkDestroyImageView(m_impl->device, r.view, nullptr); vmaDestroyImage(m_impl->allocator, r.image, r.allocation); });
        m_impl->buffers.ForEach([&](auto &r) {
            if (r.buffer)
                vmaDestroyBuffer(m_impl->allocator, r.buffer, r.allocation);
        });
        for (auto &arena : m_impl->uniformArenas)
            if (arena.buffer) vmaDestroyBuffer(m_impl->allocator, arena.buffer, arena.allocation);
        if (m_impl->descriptorPool) vkDestroyDescriptorPool(m_impl->device, m_impl->descriptorPool, nullptr);
        if (m_impl->commandPool) vkDestroyCommandPool(m_impl->device, m_impl->commandPool, nullptr);
        if (m_impl->allocator) vmaDestroyAllocator(m_impl->allocator);
        if (m_impl->device) vkDestroyDevice(m_impl->device, nullptr);
        if (m_impl->surface) vkDestroySurfaceKHR(m_impl->instance, m_impl->surface, nullptr);
        if (m_impl->instance) vkDestroyInstance(m_impl->instance, nullptr);
    }

    std::unique_ptr<ISwapchain> VulkanDevice::CreateSwapchain(const SwapchainDescriptor &descriptor)
    {
        return std::make_unique<VulkanSwapchain>(*m_impl, descriptor);
    }

    BufferHandle VulkanDevice::CreateBuffer(const BufferDescriptor &descriptor, std::span<const std::byte> data)
    {
        if (!descriptor.size || data.size() > descriptor.size) throw std::invalid_argument("Invalid Vulkan buffer size/data");
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = descriptor.size; info.usage = BufferUsageFlags(descriptor.usage) | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocation{}; allocation.usage = VMA_MEMORY_USAGE_AUTO; allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        BufferResource resource; resource.size = descriptor.size; resource.usage = descriptor.usage;
        if (descriptor.usage == BufferUsage::Uniform)
        {
            resource.uniformData.resize(descriptor.size);
            if (!data.empty()) std::memcpy(resource.uniformData.data(), data.data(), data.size());
            return m_impl->buffers.Insert(std::move(resource));
        }
        for (std::size_t frame = 0; frame < 1; ++frame)
        {
            VmaAllocationInfo allocationInfo{};
            VkBuffer *buffer = &resource.buffer;
            VmaAllocation *memory = &resource.allocation;
            Check(vmaCreateBuffer(m_impl->allocator, &info, &allocation, buffer, memory, &allocationInfo), "vmaCreateBuffer");
            if (!data.empty())
            {
                std::memcpy(allocationInfo.pMappedData, data.data(), data.size());
                vmaFlushAllocation(m_impl->allocator, *memory, 0, data.size());
            }
        }
        return m_impl->buffers.Insert(std::move(resource));
    }

    TextureHandle VulkanDevice::CreateTexture(const TextureDescriptor &descriptor, std::span<const std::byte> data)
    {
        if (!descriptor.width || !descriptor.height) throw std::invalid_argument("Invalid Vulkan texture dimensions");
        TextureResource resource; resource.descriptor = descriptor;
        resource.mipLevels = descriptor.usage == TextureUsage::Sampled
                                 ? 1u + static_cast<std::uint32_t>(std::floor(std::log2(static_cast<double>((std::max)(descriptor.width, descriptor.height)))))
                                 : 1u;
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; image.imageType = VK_IMAGE_TYPE_2D; image.extent = {descriptor.width, descriptor.height, 1}; image.mipLevels = resource.mipLevels; image.arrayLayers = 1; image.format = ToVkFormat(descriptor.format); image.tiling = VK_IMAGE_TILING_OPTIMAL; image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      (descriptor.usage == TextureUsage::Sampled ? VK_IMAGE_USAGE_SAMPLED_BIT :
                       descriptor.usage == TextureUsage::ColorAttachment ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT :
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        if (descriptor.sampled)
            image.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo allocation{}; allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        Check(vmaCreateImage(m_impl->allocator, &image, &allocation, &resource.image, &resource.allocation, nullptr), "vmaCreateImage");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = resource.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format; view.subresourceRange = {Aspect(resource), 0, resource.mipLevels, 0, 1};
        Check(vkCreateImageView(m_impl->device, &view, nullptr, &resource.view), "vkCreateImageView");
        const auto handle = m_impl->textures.Insert(std::move(resource));
        auto *stored = m_impl->textures.Get(handle);
        if (!data.empty())
        {
            std::vector<std::byte> mipData(data.begin(), data.end());
            std::vector<VkBufferImageCopy> copies;
            copies.reserve(stored->mipLevels);
            std::uint32_t mipWidth = descriptor.width;
            std::uint32_t mipHeight = descriptor.height;
            std::size_t levelOffset = 0;
            copies.push_back(VkBufferImageCopy{levelOffset, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {mipWidth, mipHeight, 1}});
            for (std::uint32_t level = 1; level < stored->mipLevels; ++level)
            {
                const std::uint32_t nextWidth = (std::max)(1u, mipWidth / 2u);
                const std::uint32_t nextHeight = (std::max)(1u, mipHeight / 2u);
                const std::size_t sourceOffset = levelOffset;
                levelOffset = mipData.size();
                mipData.resize(levelOffset + static_cast<std::size_t>(nextWidth) * nextHeight * 4);
                for (std::uint32_t y = 0; y < nextHeight; ++y)
                {
                    for (std::uint32_t x = 0; x < nextWidth; ++x)
                    {
                        for (std::uint32_t channel = 0; channel < 4; ++channel)
                        {
                            unsigned int sum = 0;
                            float linearSum = 0.0f;
                            for (std::uint32_t oy = 0; oy < 2; ++oy)
                                for (std::uint32_t ox = 0; ox < 2; ++ox)
                                {
                                    const auto sx = (std::min)(mipWidth - 1, x * 2 + ox);
                                    const auto sy = (std::min)(mipHeight - 1, y * 2 + oy);
                                    const auto sample = std::to_integer<std::uint8_t>(
                                        mipData[sourceOffset + (static_cast<std::size_t>(sy) * mipWidth + sx) * 4 + channel]);
                                    sum += sample;
                                    if (descriptor.format == Format::R8G8B8A8Srgb && channel < 3)
                                        linearSum += SrgbToLinear(sample);
                                }
                            mipData[levelOffset + (static_cast<std::size_t>(y) * nextWidth + x) * 4 + channel] =
                                descriptor.format == Format::R8G8B8A8Srgb && channel < 3
                                    ? LinearToSrgb(linearSum * 0.25f)
                                    : static_cast<std::byte>(sum / 4);
                        }
                    }
                }
                copies.push_back(VkBufferImageCopy{levelOffset, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1}, {0, 0, 0}, {nextWidth, nextHeight, 1}});
                mipWidth = nextWidth;
                mipHeight = nextHeight;
            }
            const BufferHandle stagingHandle = CreateBuffer({mipData.size(), BufferUsage::Vertex, "Texture staging"}, mipData);
            auto *staging = m_impl->buffers.Get(stagingHandle);
            m_impl->Immediate([&](VkCommandBuffer command) { m_impl->Transition(command, *stored, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT); vkCmdCopyBufferToImage(command, staging->buffer, stored->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(copies.size()), copies.data()); m_impl->Transition(command, *stored, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT); });
            DestroyBuffer(stagingHandle);
        }
        return handle;
    }

    SamplerHandle VulkanDevice::CreateSampler(const SamplerDescriptor &descriptor)
    {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; info.magFilter = info.minFilter = descriptor.linearFiltering ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; info.addressModeU = info.addressModeV = info.addressModeW = descriptor.repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; info.maxLod = VK_LOD_CLAMP_NONE;
        SamplerResource resource; Check(vkCreateSampler(m_impl->device, &info, nullptr, &resource.sampler), "vkCreateSampler"); return m_impl->samplers.Insert(resource);
    }

    PipelineHandle VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDescriptor &descriptor)
    {
        if (descriptor.vertexShader.spirv.empty() || descriptor.fragmentShader.spirv.empty()) throw std::invalid_argument("Vulkan pipeline requires SPIR-V shaders");
        auto module = [&](const auto &code) { VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize = code.size() * sizeof(std::uint32_t); info.pCode = code.data(); VkShaderModule result{}; Check(vkCreateShaderModule(m_impl->device, &info, nullptr, &result), "vkCreateShaderModule"); return result; };
        VkShaderModule vertex = module(descriptor.vertexShader.spirv), fragment = module(descriptor.fragmentShader.spirv);
        PipelineResource resource; resource.descriptor = descriptor;
        if (descriptor.resourceBindings.empty())
            throw std::invalid_argument("Vulkan pipeline requires declared shader resource bindings");
        std::uint32_t maximumSet = 0;
        for (const auto &binding : descriptor.resourceBindings)
            maximumSet = (std::max)(maximumSet, binding.set);
        std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindingsBySet(maximumSet + 1);
        for (const auto &binding : descriptor.resourceBindings)
        {
            const VkShaderStageFlags stages =
                binding.stages == ShaderStageMask::Vertex ? VK_SHADER_STAGE_VERTEX_BIT :
                binding.stages == ShaderStageMask::Fragment ? VK_SHADER_STAGE_FRAGMENT_BIT :
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            bindingsBySet[binding.set].push_back({
                binding.binding,
                binding.type == ResourceBindingType::UniformBuffer
                    ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                    : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                1, stages, nullptr});
        }
        resource.setLayouts.resize(bindingsBySet.size());
        resource.populatedSets.resize(bindingsBySet.size());
        resource.bindingsBySet.resize(bindingsBySet.size());
        for (const auto &binding : descriptor.resourceBindings)
            resource.bindingsBySet[binding.set].push_back(binding);
        for (std::size_t set = 0; set < bindingsBySet.size(); ++set)
        {
            std::ranges::sort(resource.bindingsBySet[set], {}, &GraphicsPipelineDescriptor::ResourceBinding::binding);
            resource.populatedSets[set] = !bindingsBySet[set].empty();
            VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            setInfo.bindingCount = static_cast<std::uint32_t>(bindingsBySet[set].size());
            setInfo.pBindings = bindingsBySet[set].data();
            Check(vkCreateDescriptorSetLayout(m_impl->device, &setInfo, nullptr, &resource.setLayouts[set]),
                  "vkCreateDescriptorSetLayout");
        }
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = static_cast<std::uint32_t>(resource.setLayouts.size());
        layout.pSetLayouts = resource.setLayouts.data();
        Check(vkCreatePipelineLayout(m_impl->device, &layout, nullptr, &resource.layout), "vkCreatePipelineLayout");
        // Slang emits the selected entry point as SPIR-V's canonical `main`.
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main"}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main"}}};
        std::vector<VkVertexInputAttributeDescription> attributes; for (const auto &a : descriptor.vertexLayout.attributes) attributes.push_back({a.location, 0, ToVkFormat(a.format), a.offset});
        VkVertexInputBindingDescription binding{0, descriptor.vertexLayout.stride, VK_VERTEX_INPUT_RATE_VERTEX}; VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vertexInput.vertexBindingDescriptionCount = attributes.empty() ? 0u : 1u; vertexInput.pVertexBindingDescriptions = attributes.empty() ? nullptr : &binding; vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()); vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = 1; viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = descriptor.cullMode == CullMode::None ? VK_CULL_MODE_NONE : descriptor.cullMode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT; raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; depth.depthTestEnable = descriptor.depthTest; depth.depthWriteEnable = descriptor.depthWrite; depth.depthCompareOp = ToVkCompare(descriptor.depthCompare);
        const std::size_t colorAttachmentCount = descriptor.colorFormats.empty() ? 1 : descriptor.colorFormats.size();
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(colorAttachmentCount); for (auto &blend : blendAttachments) blend.colorWriteMask = 0xf; VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blending.attachmentCount = static_cast<std::uint32_t>(blendAttachments.size()); blending.pAttachments = blendAttachments.data();
        std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamicState.dynamicStateCount = dynamicStates.size(); dynamicState.pDynamicStates = dynamicStates.data();
        std::vector<VkFormat> colorFormats; if (descriptor.colorFormats.empty()) colorFormats.push_back(ToVkFormat(descriptor.colorFormat)); else for (const auto format : descriptor.colorFormats) colorFormats.push_back(ToVkFormat(format)); VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; rendering.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size()); rendering.pColorAttachmentFormats = colorFormats.data(); rendering.depthAttachmentFormat = descriptor.depthFormat == Format::Undefined ? VK_FORMAT_UNDEFINED : ToVkFormat(descriptor.depthFormat);
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.pNext = &rendering; info.stageCount = stages.size(); info.pStages = stages.data(); info.pVertexInputState = &vertexInput; info.pInputAssemblyState = &assembly; info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &multisample; info.pDepthStencilState = &depth; info.pColorBlendState = &blending; info.pDynamicState = &dynamicState; info.layout = resource.layout;
        const VkResult result = vkCreateGraphicsPipelines(m_impl->device, VK_NULL_HANDLE, 1, &info, nullptr, &resource.pipeline); vkDestroyShaderModule(m_impl->device, vertex, nullptr); vkDestroyShaderModule(m_impl->device, fragment, nullptr); Check(result, "vkCreateGraphicsPipelines");
        return m_impl->pipelines.Insert(std::move(resource));
    }

    void VulkanDevice::UpdateBuffer(BufferHandle handle, std::size_t offset, std::span<const std::byte> data)
    {
        auto *resource = m_impl->buffers.Get(handle);
        if (!resource || offset > resource->size || data.size() > resource->size - offset)
            throw std::invalid_argument("Invalid Vulkan buffer update");
        if (resource->usage == BufferUsage::Uniform)
        {
            const auto uploadStart = std::chrono::steady_clock::now();
            auto &arena = m_impl->uniformArenas[m_impl->activeFrameIndex];
            const bool wasResident = resource->uniformEpochs[m_impl->activeFrameIndex] == arena.epoch;
            std::memcpy(resource->uniformData.data() + offset, data.data(), data.size());
            const auto arenaOffset = m_impl->EnsureUniformResident(*resource);
            if (wasResident && !data.empty())
            {
                const auto dirtyStart = arenaOffset + offset;
                std::memcpy(static_cast<std::byte *>(arena.mapped) + dirtyStart, data.data(), data.size());
                arena.dirtyStart = std::min(arena.dirtyStart, dirtyStart);
                arena.dirtyEnd = std::max(arena.dirtyEnd, dirtyStart + data.size());
                m_impl->timingStats.uniformBytesUploaded += data.size();
            }
            m_impl->timingStats.uniformUploadCpuMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - uploadStart).count();
            return;
        }
        const auto allocation = resource->allocation;
        void *mapped = nullptr;
        Check(vmaMapMemory(m_impl->allocator, allocation, &mapped), "vmaMapMemory");
        std::memcpy(static_cast<std::byte *>(mapped) + offset, data.data(), data.size());
        vmaFlushAllocation(m_impl->allocator, allocation, offset, data.size());
        vmaUnmapMemory(m_impl->allocator, allocation);
    }
    void VulkanDevice::DestroyBuffer(BufferHandle h)
    {
        if (auto r = m_impl->buffers.Remove(h))
        {
            Check(vkDeviceWaitIdle(m_impl->device), "vkDeviceWaitIdle(buffer destruction)");
            if (r->buffer)
                vmaDestroyBuffer(m_impl->allocator, r->buffer, r->allocation);
        }
    }
    void VulkanDevice::DestroyTexture(TextureHandle h)
    {
        // Resizing an editor target may retire an image while its buffered
        // readback is still queued. Wait only for slots that reference this
        // image rather than idling the whole device.
        for (auto &slot : m_impl->readbacks)
            if (slot.pending && slot.source == h)
            {
                Check(vkWaitForFences(m_impl->device, 1, &slot.fence, VK_TRUE, UINT64_MAX),
                      "vkWaitForFences(readback destruction)");
                slot.pending = false;
            }
        if (auto r = m_impl->textures.Remove(h))
        {
            Check(vkDeviceWaitIdle(m_impl->device), "vkDeviceWaitIdle(texture destruction)");
            vkDestroyImageView(m_impl->device, r->view, nullptr);
            vmaDestroyImage(m_impl->allocator, r->image, r->allocation);
        }
    }
    void VulkanDevice::DestroySampler(SamplerHandle h) { if (auto r = m_impl->samplers.Remove(h)) { Check(vkDeviceWaitIdle(m_impl->device), "vkDeviceWaitIdle(sampler destruction)"); vkDestroySampler(m_impl->device, r->sampler, nullptr); } }
    void VulkanDevice::DestroyPipeline(PipelineHandle h) { if (auto r = m_impl->pipelines.Remove(h)) { Check(vkDeviceWaitIdle(m_impl->device), "vkDeviceWaitIdle(pipeline destruction)"); vkDestroyPipeline(m_impl->device, r->pipeline, nullptr); vkDestroyPipelineLayout(m_impl->device, r->layout, nullptr); for (auto l : r->setLayouts) vkDestroyDescriptorSetLayout(m_impl->device, l, nullptr); } }
    ICommandContext &VulkanDevice::GetImmediateContext() { return *m_impl->context; }
    RenderDeviceTimingStats VulkanDevice::GetTimingStats() const { return m_impl->timingStats; }
    const std::string &VulkanDevice::GetDeviceName() const noexcept { return m_impl->deviceName; }

    std::vector<std::byte> VulkanDevice::ReadTextureRgba8(TextureHandle handle)
    {
        auto *texture = m_impl->textures.Get(handle); if (!texture || texture->descriptor.format == Format::D32Float) throw std::invalid_argument("Invalid Vulkan color texture readback");
        const std::size_t byteCount = static_cast<std::size_t>(texture->descriptor.width) * texture->descriptor.height * 4;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = byteCount; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocation{}; allocation.usage = VMA_MEMORY_USAGE_AUTO; allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        VkBuffer buffer{}; VmaAllocation memory{}; Check(vmaCreateBuffer(m_impl->allocator, &info, &allocation, &buffer, &memory, nullptr), "vmaCreateBuffer(readback)");
        m_impl->Immediate([&](VkCommandBuffer command) { m_impl->Transition(command, *texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT); VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {texture->descriptor.width, texture->descriptor.height, 1}; vkCmdCopyImageToBuffer(command, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy); });
        std::vector<std::byte> pixels(byteCount); void *mapped = nullptr; Check(vmaMapMemory(m_impl->allocator, memory, &mapped), "vmaMapMemory(readback)"); vmaInvalidateAllocation(m_impl->allocator, memory, 0, byteCount); std::memcpy(pixels.data(), mapped, byteCount); vmaUnmapMemory(m_impl->allocator, memory); vmaDestroyBuffer(m_impl->allocator, buffer, memory); return pixels;
    }

    std::optional<std::vector<std::byte>> VulkanDevice::ReadTextureRgba8Buffered(TextureHandle handle)
    {
        auto *texture = m_impl->textures.Get(handle);
        if (!texture || texture->descriptor.format == Format::D32Float)
            throw std::invalid_argument("Invalid Vulkan color texture readback");

        const std::size_t byteCount = static_cast<std::size_t>(texture->descriptor.width) *
                                      texture->descriptor.height * 4;
        std::optional<std::vector<std::byte>> completed;
        std::uint64_t completedSequence = 0;
        for (auto &slot : m_impl->readbacks)
        {
            if (!slot.pending || vkGetFenceStatus(m_impl->device, slot.fence) != VK_SUCCESS)
                continue;
            vmaInvalidateAllocation(m_impl->allocator, slot.allocation, 0, slot.byteCount);
            if (slot.byteCount == byteCount && slot.sequence >= completedSequence)
            {
                completed.emplace(slot.byteCount);
                std::memcpy(completed->data(), slot.mapped, slot.byteCount);
                completedSequence = slot.sequence;
            }
            slot.pending = false;
        }

        auto available = std::find_if(m_impl->readbacks.begin(), m_impl->readbacks.end(),
                                      [](const auto &slot) { return !slot.pending; });
        if (available == m_impl->readbacks.end())
            return completed;

        auto &slot = *available;
        if (!slot.commandPool)
        {
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = m_impl->queueFamily;
            Check(vkCreateCommandPool(m_impl->device, &pool, nullptr, &slot.commandPool),
                  "vkCreateCommandPool(readback)");
            VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            command.commandPool = slot.commandPool;
            command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command.commandBufferCount = 1;
            Check(vkAllocateCommandBuffers(m_impl->device, &command, &slot.commandBuffer),
                  "vkAllocateCommandBuffers(readback)");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            Check(vkCreateFence(m_impl->device, &fence, nullptr, &slot.fence), "vkCreateFence(readback)");
        }
        if (slot.byteCount != byteCount)
        {
            if (slot.buffer) vmaDestroyBuffer(m_impl->allocator, slot.buffer, slot.allocation);
            slot.buffer = VK_NULL_HANDLE;
            slot.allocation = VK_NULL_HANDLE;
            slot.mapped = nullptr;
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = byteCount;
            info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo allocation{};
            allocation.usage = VMA_MEMORY_USAGE_AUTO;
            allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo allocationInfo{};
            Check(vmaCreateBuffer(m_impl->allocator, &info, &allocation, &slot.buffer,
                                  &slot.allocation, &allocationInfo), "vmaCreateBuffer(buffered readback)");
            slot.mapped = allocationInfo.pMappedData;
            slot.byteCount = byteCount;
        }

        Check(vkResetFences(m_impl->device, 1, &slot.fence), "vkResetFences(readback)");
        Check(vkResetCommandPool(m_impl->device, slot.commandPool, 0), "vkResetCommandPool(readback)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(slot.commandBuffer, &begin), "vkBeginCommandBuffer(readback)");
        m_impl->Transition(slot.commandBuffer, *texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {texture->descriptor.width, texture->descriptor.height, 1};
        vkCmdCopyImageToBuffer(slot.commandBuffer, texture->image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.buffer, 1, &copy);
        Check(vkEndCommandBuffer(slot.commandBuffer), "vkEndCommandBuffer(readback)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &slot.commandBuffer;
        Check(vkQueueSubmit(m_impl->queue, 1, &submit, slot.fence), "vkQueueSubmit(readback)");
        slot.pending = true;
        slot.sequence = m_impl->nextReadbackSequence++;
        slot.source = handle;
        return completed;
    }
}
