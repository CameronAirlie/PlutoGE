#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "../HandleRegistry.h"

#include <volk.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <array>
#include <cstring>
#include <stdexcept>
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

        struct BufferResource
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
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
        };

        struct SamplerResource { VkSampler sampler = VK_NULL_HANDLE; };

        struct PipelineResource
        {
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
            std::array<VkDescriptorSetLayout, 3> setLayouts{};
            std::array<VkDescriptorSet, 3> sets{};
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
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::string deviceName;
        detail::HandleRegistry<BufferHandle, BufferResource> buffers;
        detail::HandleRegistry<TextureHandle, TextureResource> textures;
        detail::HandleRegistry<SamplerHandle, SamplerResource> samplers;
        detail::HandleRegistry<PipelineHandle, PipelineResource> pipelines;
        std::unique_ptr<VulkanCommandContext> context;

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
            barrier.subresourceRange = {Aspect(texture), 0, 1, 0, 1};
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
        explicit VulkanCommandContext(VulkanDevice::Impl &impl) : m_impl(impl) {}

        void BeginRendering(const RenderingInfo &info) override
        {
            if (m_rendering) throw std::logic_error("Vulkan rendering is already active");
            m_color = m_impl.textures.Get(info.colorAttachment);
            m_depth = m_impl.textures.Get(info.depthAttachment);
            if (!m_color || !m_depth) throw std::invalid_argument("Invalid Vulkan render attachments");
            Check(vkResetCommandBuffer(m_impl.commandBuffer, 0), "vkResetCommandBuffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(m_impl.commandBuffer, &begin), "vkBeginCommandBuffer");
            m_impl.Transition(m_impl.commandBuffer, *m_color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            m_impl.Transition(m_impl.commandBuffer, *m_depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            color.imageView = m_color->view;
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = info.clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            std::memcpy(color.clearValue.color.float32, info.clearColorValue, sizeof(info.clearColorValue));
            VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depth.imageView = m_depth->view;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.loadOp = info.clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue.depthStencil.depth = info.clearDepthValue;
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea.extent = {info.width, info.height};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            rendering.pDepthAttachment = &depth;
            vkCmdBeginRendering(m_impl.commandBuffer, &rendering);
            SetViewport({0, 0, static_cast<float>(info.width), static_cast<float>(info.height), 0, 1});
            SetScissor({0, 0, info.width, info.height});
            m_rendering = true;
        }

        void EndRendering() override
        {
            if (!m_rendering) throw std::logic_error("Vulkan rendering is not active");
            vkCmdEndRendering(m_impl.commandBuffer);
            Check(vkEndCommandBuffer(m_impl.commandBuffer), "vkEndCommandBuffer");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &m_impl.commandBuffer;
            Check(vkQueueSubmit(m_impl.queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
            Check(vkQueueWaitIdle(m_impl.queue), "vkQueueWaitIdle");
            m_rendering = false;
        }

        void SetViewport(const Viewport &v) override
        {
            VkViewport viewport{v.x, v.y + v.height, v.width, -v.height, v.minDepth, v.maxDepth};
            vkCmdSetViewport(m_impl.commandBuffer, 0, 1, &viewport);
        }
        void SetScissor(const Scissor &s) override
        {
            VkRect2D scissor{{s.x, s.y}, {s.width, s.height}};
            vkCmdSetScissor(m_impl.commandBuffer, 0, 1, &scissor);
        }
        void BindPipeline(PipelineHandle handle) override
        {
            m_pipeline = m_impl.pipelines.Get(handle);
            if (!m_pipeline) throw std::invalid_argument("Invalid or stale Vulkan pipeline");
            vkCmdBindPipeline(m_impl.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->pipeline);
        }
        void BindVertexBuffer(BufferHandle handle, std::size_t offset) override
        {
            auto *resource = m_impl.buffers.Get(handle);
            if (!resource || resource->usage != BufferUsage::Vertex) throw std::invalid_argument("Invalid Vulkan vertex buffer");
            const VkDeviceSize vkOffset = offset;
            vkCmdBindVertexBuffers(m_impl.commandBuffer, 0, 1, &resource->buffer, &vkOffset);
        }
        void BindIndexBuffer(BufferHandle handle, Format format, std::size_t offset) override
        {
            auto *resource = m_impl.buffers.Get(handle);
            if (!resource || resource->usage != BufferUsage::Index || format != Format::R32Uint) throw std::invalid_argument("Invalid Vulkan index buffer");
            vkCmdBindIndexBuffer(m_impl.commandBuffer, resource->buffer, offset, VK_INDEX_TYPE_UINT32);
        }
        void BindUniformBuffer(std::uint32_t slot, BufferHandle handle) override
        {
            auto *resource = m_impl.buffers.Get(handle);
            if (!m_pipeline || !resource || resource->usage != BufferUsage::Uniform) throw std::invalid_argument("Invalid Vulkan uniform binding");
            const std::uint32_t setIndex = slot == 0 ? 0 : slot == 16 ? 2 : 99;
            if (setIndex > 2) throw std::invalid_argument("Unsupported Vulkan uniform slot");
            VkDescriptorBufferInfo buffer{resource->buffer, 0, resource->size};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = m_pipeline->sets[setIndex]; write.dstBinding = 0; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &buffer;
            vkUpdateDescriptorSets(m_impl.device, 1, &write, 0, nullptr);
        }
        void BindTexture(std::uint32_t slot, TextureHandle textureHandle, SamplerHandle samplerHandle) override
        {
            auto *texture = m_impl.textures.Get(textureHandle); auto *sampler = m_impl.samplers.Get(samplerHandle);
            if (!m_pipeline || slot != 8 || !texture || !sampler) throw std::invalid_argument("Invalid Vulkan texture binding");
            if (texture->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                throw std::logic_error("Vulkan sampled texture has an invalid layout");
            VkDescriptorImageInfo image{sampler->sampler, texture->view, texture->layout};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = m_pipeline->sets[1]; write.dstBinding = 0; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &image;
            vkUpdateDescriptorSets(m_impl.device, 1, &write, 0, nullptr);
        }
        void DrawIndexed(std::uint32_t count, std::uint32_t firstIndex, std::int32_t vertexOffset) override
        {
            if (!m_rendering || !m_pipeline) throw std::logic_error("Vulkan draw requires active rendering and pipeline");
            vkCmdBindDescriptorSets(m_impl.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout, 0, 3, m_pipeline->sets.data(), 0, nullptr);
            vkCmdDrawIndexed(m_impl.commandBuffer, count, 1, firstIndex, vertexOffset, 0);
        }

    private:
        VulkanDevice::Impl &m_impl;
        PipelineResource *m_pipeline = nullptr;
        TextureResource *m_color = nullptr;
        TextureResource *m_depth = nullptr;
        bool m_rendering = false;
    };

    VulkanDevice::VulkanDevice() : m_impl(std::make_unique<Impl>())
    {
        Check(volkInitialize(), "volkInitialize");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "PlutoGE"; app.pEngineName = "PlutoGE"; app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instanceInfo.pApplicationInfo = &app;
        Check(vkCreateInstance(&instanceInfo, nullptr, &m_impl->instance), "vkCreateInstance");
        volkLoadInstance(m_impl->instance);
        std::uint32_t count = 0; Check(vkEnumeratePhysicalDevices(m_impl->instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        if (!count) throw std::runtime_error("No Vulkan physical device is available");
        std::vector<VkPhysicalDevice> devices(count); Check(vkEnumeratePhysicalDevices(m_impl->instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
        for (auto device : devices)
        {
            std::uint32_t familyCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount); vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
            for (std::uint32_t i = 0; i < familyCount; ++i) if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { m_impl->physicalDevice = device; m_impl->queueFamily = i; break; }
            if (m_impl->physicalDevice) break;
        }
        if (!m_impl->physicalDevice) throw std::runtime_error("No Vulkan graphics queue is available");
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(m_impl->physicalDevice, &properties); m_impl->deviceName = properties.deviceName;
        const float priority = 1.0f; VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queue.queueFamilyIndex = m_impl->queueFamily; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        VkPhysicalDeviceDynamicRenderingFeatures dynamic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES}; dynamic.dynamicRendering = VK_TRUE;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pNext = &dynamic; deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queue;
        Check(vkCreateDevice(m_impl->physicalDevice, &deviceInfo, nullptr, &m_impl->device), "vkCreateDevice"); volkLoadDevice(m_impl->device); vkGetDeviceQueue(m_impl->device, m_impl->queueFamily, 0, &m_impl->queue);
        VmaVulkanFunctions functions{}; functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr; functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo allocator{}; allocator.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT; allocator.vulkanApiVersion = VK_API_VERSION_1_3; allocator.instance = m_impl->instance; allocator.physicalDevice = m_impl->physicalDevice; allocator.device = m_impl->device; allocator.pVulkanFunctions = &functions;
        Check(vmaCreateAllocator(&allocator, &m_impl->allocator), "vmaCreateAllocator");
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = m_impl->queueFamily; Check(vkCreateCommandPool(m_impl->device, &pool, nullptr, &m_impl->commandPool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; command.commandPool = m_impl->commandPool; command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command.commandBufferCount = 1; Check(vkAllocateCommandBuffers(m_impl->device, &command, &m_impl->commandBuffer), "vkAllocateCommandBuffers");
        std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128}}};
        VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; descriptors.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; descriptors.maxSets = 384; descriptors.poolSizeCount = sizes.size(); descriptors.pPoolSizes = sizes.data(); Check(vkCreateDescriptorPool(m_impl->device, &descriptors, nullptr, &m_impl->descriptorPool), "vkCreateDescriptorPool");
        m_impl->context = std::make_unique<VulkanCommandContext>(*m_impl);
    }

    VulkanDevice::~VulkanDevice()
    {
        if (!m_impl) return;
        if (m_impl->device) vkDeviceWaitIdle(m_impl->device);
        m_impl->pipelines.ForEach([&](auto &r) { vkDestroyPipeline(m_impl->device, r.pipeline, nullptr); vkDestroyPipelineLayout(m_impl->device, r.layout, nullptr); for (auto l : r.setLayouts) vkDestroyDescriptorSetLayout(m_impl->device, l, nullptr); });
        m_impl->samplers.ForEach([&](auto &r) { vkDestroySampler(m_impl->device, r.sampler, nullptr); });
        m_impl->textures.ForEach([&](auto &r) { vkDestroyImageView(m_impl->device, r.view, nullptr); vmaDestroyImage(m_impl->allocator, r.image, r.allocation); });
        m_impl->buffers.ForEach([&](auto &r) { vmaDestroyBuffer(m_impl->allocator, r.buffer, r.allocation); });
        if (m_impl->descriptorPool) vkDestroyDescriptorPool(m_impl->device, m_impl->descriptorPool, nullptr);
        if (m_impl->commandPool) vkDestroyCommandPool(m_impl->device, m_impl->commandPool, nullptr);
        if (m_impl->allocator) vmaDestroyAllocator(m_impl->allocator);
        if (m_impl->device) vkDestroyDevice(m_impl->device, nullptr);
        if (m_impl->instance) vkDestroyInstance(m_impl->instance, nullptr);
    }

    BufferHandle VulkanDevice::CreateBuffer(const BufferDescriptor &descriptor, std::span<const std::byte> data)
    {
        if (!descriptor.size || data.size() > descriptor.size) throw std::invalid_argument("Invalid Vulkan buffer size/data");
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = descriptor.size; info.usage = BufferUsageFlags(descriptor.usage) | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocation{}; allocation.usage = VMA_MEMORY_USAGE_AUTO; allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocationInfo{}; BufferResource resource; resource.size = descriptor.size; resource.usage = descriptor.usage;
        Check(vmaCreateBuffer(m_impl->allocator, &info, &allocation, &resource.buffer, &resource.allocation, &allocationInfo), "vmaCreateBuffer");
        if (!data.empty()) { std::memcpy(allocationInfo.pMappedData, data.data(), data.size()); vmaFlushAllocation(m_impl->allocator, resource.allocation, 0, data.size()); }
        return m_impl->buffers.Insert(std::move(resource));
    }

    TextureHandle VulkanDevice::CreateTexture(const TextureDescriptor &descriptor, std::span<const std::byte> data)
    {
        if (!descriptor.width || !descriptor.height) throw std::invalid_argument("Invalid Vulkan texture dimensions");
        TextureResource resource; resource.descriptor = descriptor;
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; image.imageType = VK_IMAGE_TYPE_2D; image.extent = {descriptor.width, descriptor.height, 1}; image.mipLevels = 1; image.arrayLayers = 1; image.format = ToVkFormat(descriptor.format); image.tiling = VK_IMAGE_TILING_OPTIMAL; image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (descriptor.usage == TextureUsage::Sampled ? VK_IMAGE_USAGE_SAMPLED_BIT : descriptor.usage == TextureUsage::ColorAttachment ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        VmaAllocationCreateInfo allocation{}; allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        Check(vmaCreateImage(m_impl->allocator, &image, &allocation, &resource.image, &resource.allocation, nullptr), "vmaCreateImage");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = resource.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format; view.subresourceRange = {Aspect(resource), 0, 1, 0, 1};
        Check(vkCreateImageView(m_impl->device, &view, nullptr, &resource.view), "vkCreateImageView");
        const auto handle = m_impl->textures.Insert(std::move(resource));
        auto *stored = m_impl->textures.Get(handle);
        if (!data.empty())
        {
            const BufferHandle stagingHandle = CreateBuffer({data.size(), BufferUsage::Vertex, "Texture staging"}, data);
            auto *staging = m_impl->buffers.Get(stagingHandle);
            m_impl->Immediate([&](VkCommandBuffer command) { m_impl->Transition(command, *stored, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT); VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {descriptor.width, descriptor.height, 1}; vkCmdCopyBufferToImage(command, staging->buffer, stored->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy); m_impl->Transition(command, *stored, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT); });
            DestroyBuffer(stagingHandle);
        }
        return handle;
    }

    SamplerHandle VulkanDevice::CreateSampler(const SamplerDescriptor &descriptor)
    {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; info.magFilter = info.minFilter = descriptor.linearFiltering ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; info.addressModeU = info.addressModeV = info.addressModeW = descriptor.repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; info.maxLod = 0;
        SamplerResource resource; Check(vkCreateSampler(m_impl->device, &info, nullptr, &resource.sampler), "vkCreateSampler"); return m_impl->samplers.Insert(resource);
    }

    PipelineHandle VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDescriptor &descriptor)
    {
        if (descriptor.vertexShader.spirv.empty() || descriptor.fragmentShader.spirv.empty()) throw std::invalid_argument("Vulkan pipeline requires SPIR-V shaders");
        auto module = [&](const auto &code) { VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize = code.size() * sizeof(std::uint32_t); info.pCode = code.data(); VkShaderModule result{}; Check(vkCreateShaderModule(m_impl->device, &info, nullptr, &result), "vkCreateShaderModule"); return result; };
        VkShaderModule vertex = module(descriptor.vertexShader.spirv), fragment = module(descriptor.fragmentShader.spirv);
        PipelineResource resource; resource.descriptor = descriptor;
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{{{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}, {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}}};
        for (std::size_t i = 0; i < 3; ++i) { VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; info.bindingCount = 1; info.pBindings = &bindings[i]; Check(vkCreateDescriptorSetLayout(m_impl->device, &info, nullptr, &resource.setLayouts[i]), "vkCreateDescriptorSetLayout"); }
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; layout.setLayoutCount = 3; layout.pSetLayouts = resource.setLayouts.data(); Check(vkCreatePipelineLayout(m_impl->device, &layout, nullptr, &resource.layout), "vkCreatePipelineLayout");
        VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; sets.descriptorPool = m_impl->descriptorPool; sets.descriptorSetCount = 3; sets.pSetLayouts = resource.setLayouts.data(); Check(vkAllocateDescriptorSets(m_impl->device, &sets, resource.sets.data()), "vkAllocateDescriptorSets");
        // Slang emits the selected entry point as SPIR-V's canonical `main`.
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main"}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main"}}};
        std::vector<VkVertexInputAttributeDescription> attributes; for (const auto &a : descriptor.vertexLayout.attributes) attributes.push_back({a.location, 0, ToVkFormat(a.format), a.offset});
        VkVertexInputBindingDescription binding{0, descriptor.vertexLayout.stride, VK_VERTEX_INPUT_RATE_VERTEX}; VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vertexInput.vertexBindingDescriptionCount = 1; vertexInput.pVertexBindingDescriptions = &binding; vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()); vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = 1; viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = descriptor.cullMode == CullMode::None ? VK_CULL_MODE_NONE : descriptor.cullMode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT; raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; depth.depthTestEnable = descriptor.depthTest; depth.depthWriteEnable = descriptor.depthWrite; depth.depthCompareOp = ToVkCompare(descriptor.depthCompare);
        VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 0xf; VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blending.attachmentCount = 1; blending.pAttachments = &blend;
        std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamicState.dynamicStateCount = dynamicStates.size(); dynamicState.pDynamicStates = dynamicStates.data();
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_SRGB; rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &colorFormat; rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.pNext = &rendering; info.stageCount = stages.size(); info.pStages = stages.data(); info.pVertexInputState = &vertexInput; info.pInputAssemblyState = &assembly; info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &multisample; info.pDepthStencilState = &depth; info.pColorBlendState = &blending; info.pDynamicState = &dynamicState; info.layout = resource.layout;
        const VkResult result = vkCreateGraphicsPipelines(m_impl->device, VK_NULL_HANDLE, 1, &info, nullptr, &resource.pipeline); vkDestroyShaderModule(m_impl->device, vertex, nullptr); vkDestroyShaderModule(m_impl->device, fragment, nullptr); Check(result, "vkCreateGraphicsPipelines");
        return m_impl->pipelines.Insert(std::move(resource));
    }

    void VulkanDevice::UpdateBuffer(BufferHandle handle, std::size_t offset, std::span<const std::byte> data)
    {
        auto *resource = m_impl->buffers.Get(handle); if (!resource || offset > resource->size || data.size() > resource->size - offset) throw std::invalid_argument("Invalid Vulkan buffer update"); void *mapped = nullptr; Check(vmaMapMemory(m_impl->allocator, resource->allocation, &mapped), "vmaMapMemory"); std::memcpy(static_cast<std::byte *>(mapped) + offset, data.data(), data.size()); vmaFlushAllocation(m_impl->allocator, resource->allocation, offset, data.size()); vmaUnmapMemory(m_impl->allocator, resource->allocation);
    }
    void VulkanDevice::DestroyBuffer(BufferHandle h) { if (auto r = m_impl->buffers.Remove(h)) vmaDestroyBuffer(m_impl->allocator, r->buffer, r->allocation); }
    void VulkanDevice::DestroyTexture(TextureHandle h) { if (auto r = m_impl->textures.Remove(h)) { vkDestroyImageView(m_impl->device, r->view, nullptr); vmaDestroyImage(m_impl->allocator, r->image, r->allocation); } }
    void VulkanDevice::DestroySampler(SamplerHandle h) { if (auto r = m_impl->samplers.Remove(h)) vkDestroySampler(m_impl->device, r->sampler, nullptr); }
    void VulkanDevice::DestroyPipeline(PipelineHandle h) { if (auto r = m_impl->pipelines.Remove(h)) { vkDestroyPipeline(m_impl->device, r->pipeline, nullptr); vkDestroyPipelineLayout(m_impl->device, r->layout, nullptr); for (auto l : r->setLayouts) vkDestroyDescriptorSetLayout(m_impl->device, l, nullptr); } }
    ICommandContext &VulkanDevice::GetImmediateContext() { return *m_impl->context; }
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
}
