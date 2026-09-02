#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "../HandleRegistry.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace PlutoGE::render::rhi::opengl
{
    namespace
    {
        struct BufferResource
        {
            GLuint name = 0;
            std::size_t size = 0;
            BufferUsage usage{};
        };
        struct TextureResource
        {
            GLuint name = 0;
            TextureDescriptor descriptor;
        };
        struct SamplerResource
        {
            GLuint name = 0;
        };
        struct PipelineResource
        {
            GLuint program = 0;
            GLuint vertexArray = 0;
            GraphicsPipelineDescriptor descriptor;
            bool compute = false;
        };

        GLenum BufferTarget(BufferUsage usage)
        {
            switch (usage)
            {
            case BufferUsage::Vertex:
                return GL_ARRAY_BUFFER;
            case BufferUsage::Index:
                return GL_ELEMENT_ARRAY_BUFFER;
            case BufferUsage::Uniform:
                return GL_UNIFORM_BUFFER;
            }
            throw std::invalid_argument("Unsupported RHI buffer usage");
        }

        struct TextureFormat
        {
            GLint internalFormat;
            GLenum format;
            GLenum type;
        };

        TextureFormat ToTextureFormat(Format format)
        {
            switch (format)
            {
            case Format::R8G8B8A8Unorm:
                return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
            case Format::R8G8B8A8Srgb:
                return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
            case Format::R16G16B16A16Float:
                return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
            case Format::R32Float:
                return {GL_R32F, GL_RED, GL_FLOAT};
            case Format::R32Uint:
                return {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT};
            case Format::R32G32Float:
                return {GL_RG32F, GL_RG, GL_FLOAT};
            case Format::R32G32B32A32Float:
                return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
            case Format::D32Float:
                return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
            default:
                throw std::invalid_argument("Unsupported RHI texture format");
            }
        }

        GLint AttributeComponents(Format format)
        {
            switch (format)
            {
            case Format::R32G32Float:
                return 2;
            case Format::R32G32B32Float:
                return 3;
            case Format::R32G32B32A32Float:
                return 4;
            default:
                throw std::invalid_argument("Unsupported RHI vertex attribute format");
            }
        }

        GLuint CompileShader(GLenum stage, const std::string &source, const std::string &debugName)
        {
            if (source.empty())
                throw std::invalid_argument(debugName + " has an empty shader stage");
            const GLuint shader = glCreateShader(stage);
            const char *text = source.c_str();
            glShaderSource(shader, 1, &text, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_TRUE)
                return shader;

            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetShaderInfoLog(shader, length, nullptr, log.data());
            glDeleteShader(shader);
            throw std::runtime_error(debugName + " shader compilation failed: " + log);
        }

        void LabelObject(GLenum type, GLuint name, const std::string &label)
        {
            if (glad_glObjectLabel && !label.empty())
                glObjectLabel(type, name, static_cast<GLsizei>(label.size()), label.c_str());
        }
    }

    struct OpenGLDevice::Impl
    {
        detail::HandleRegistry<BufferHandle, BufferResource> buffers;
        detail::HandleRegistry<TextureHandle, TextureResource> textures;
        detail::HandleRegistry<SamplerHandle, SamplerResource> samplers;
        detail::HandleRegistry<PipelineHandle, PipelineResource> pipelines;
        std::unique_ptr<OpenGLCommandContext> context;
        GLuint framebuffer = 0;
    };

    class OpenGLCommandContext final : public ICommandContext
    {
    public:
        explicit OpenGLCommandContext(OpenGLDevice::Impl &impl) : m_impl(impl) {}

        void BeginRendering(const RenderingInfo &info) override
        {
            if (m_rendering)
                throw std::logic_error("OpenGL RHI rendering is already active");
            m_rendering = true;

            if (info.colorAttachments.empty() && !info.depthAttachment && !info.attachmentless)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            else
            {
                glBindFramebuffer(GL_FRAMEBUFFER, m_impl.framebuffer);
                const auto *depth = m_impl.textures.Get(info.depthAttachment);
                if (info.depthAttachment && !depth)
                    throw std::invalid_argument("Invalid or stale RHI depth attachment");
                std::vector<GLenum> drawBuffers;
                drawBuffers.reserve(info.colorAttachments.size());
                for (std::size_t index = info.colorAttachments.size(); index < m_activeColorAttachmentCount; ++index)
                    glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + index), GL_TEXTURE_2D, 0, 0);
                for (std::size_t index = 0; index < info.colorAttachments.size(); ++index)
                {
                    const auto *color = m_impl.textures.Get(info.colorAttachments[index]);
                    if (!color)
                        throw std::invalid_argument("Invalid or stale RHI color attachment");
                    const auto attachment = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + index);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, color->name, 0);
                    drawBuffers.push_back(attachment);
                }
                m_activeColorAttachmentCount = info.colorAttachments.size();
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth ? depth->name : 0, 0);
                if (drawBuffers.empty())
                    glDrawBuffer(GL_NONE);
                else
                    glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
                glReadBuffer(drawBuffers.empty() ? GL_NONE : drawBuffers.front());
                if (info.attachmentless)
                {
                    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH,
                                            static_cast<GLint>(info.width));
                    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT,
                                            static_cast<GLint>(info.height));
                }
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    throw std::runtime_error("RHI render target framebuffer is incomplete");
            }

            glViewport(0, 0, static_cast<GLsizei>(info.width), static_cast<GLsizei>(info.height));
            // The editor shares this context with ImGui and the legacy renderer.
            // Never inherit a UI clip rectangle into an RHI render pass.
            glDisable(GL_SCISSOR_TEST);
            GLbitfield clearMask = 0;
            if (info.clearColor)
            {
                if (!info.colorAttachments.empty() && !info.clearColorValues.empty())
                {
                    for (std::size_t index = 0; index < info.colorAttachments.size(); ++index)
                    {
                        const float *clearValue = index < info.clearColorValues.size()
                                                      ? info.clearColorValues[index].data()
                                                      : info.clearColorValue;
                        glClearBufferfv(GL_COLOR, static_cast<GLint>(index), clearValue);
                    }
                }
                else
                {
                    glClearColor(info.clearColorValue[0], info.clearColorValue[1],
                                 info.clearColorValue[2], info.clearColorValue[3]);
                    clearMask |= GL_COLOR_BUFFER_BIT;
                }
            }
            if (info.clearDepth)
            {
                // glClear respects GL_DEPTH_WRITEMASK. Fullscreen/post-process
                // pipelines deliberately leave depth writes disabled, and the
                // clear happens before the next pipeline is bound. Without
                // forcing the mask here, the following frame retains stale
                // reverse-Z depth, causing sky pixels to be treated as geometry
                // and clipping atmospheric volumes along old silhouettes.
                GLboolean previousDepthMask = GL_TRUE;
                glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
                glDepthMask(GL_TRUE);
                glClearDepth(info.clearDepthValue);
                clearMask |= GL_DEPTH_BUFFER_BIT;
                if (clearMask)
                    glClear(clearMask);
                glDepthMask(previousDepthMask);
                clearMask = 0;
            }
            if (clearMask)
                glClear(clearMask);
        }

        void EndRendering() override
        {
            if (!m_rendering)
                throw std::logic_error("OpenGL RHI rendering is not active");
            m_rendering = false;
        }

        void SetViewport(const Viewport &v) override
        {
            glViewport(static_cast<GLint>(v.x), static_cast<GLint>(v.y), static_cast<GLsizei>(v.width), static_cast<GLsizei>(v.height));
            glDepthRange(v.minDepth, v.maxDepth);
        }
        void SetScissor(const Scissor &s) override
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(s.x, s.y, static_cast<GLsizei>(s.width), static_cast<GLsizei>(s.height));
        }

        void BindPipeline(PipelineHandle handle) override
        {
            auto *pipeline = m_impl.pipelines.Get(handle);
            if (!pipeline)
                throw std::invalid_argument("Invalid or stale RHI pipeline");
            m_pipeline = pipeline;
            glUseProgram(pipeline->program);
            glBindVertexArray(pipeline->vertexArray);
            if (pipeline->compute)
                return;
            pipeline->descriptor.depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
            glDepthMask(pipeline->descriptor.depthWrite ? GL_TRUE : GL_FALSE);
            pipeline->descriptor.blend.enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
            if (pipeline->descriptor.blend.enabled)
            {
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }
            const auto compare = pipeline->descriptor.depthCompare == CompareOperation::GreaterOrEqual ? GL_GEQUAL : GL_LESS;
            glDepthFunc(compare);
            if (pipeline->descriptor.cullMode == CullMode::None)
                glDisable(GL_CULL_FACE);
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(pipeline->descriptor.cullMode == CullMode::Front ? GL_FRONT : GL_BACK);
            }
        }

        void BindVertexBuffer(BufferHandle handle, std::size_t offset) override
        {
            auto *buffer = m_impl.buffers.Get(handle);
            if (!buffer || buffer->usage != BufferUsage::Vertex || !m_pipeline)
                throw std::invalid_argument("Invalid RHI vertex buffer or no bound pipeline");
            glBindBuffer(GL_ARRAY_BUFFER, buffer->name);
            for (const auto &attribute : m_pipeline->descriptor.vertexLayout.attributes)
            {
                glEnableVertexAttribArray(attribute.location);
                glVertexAttribPointer(attribute.location, AttributeComponents(attribute.format), GL_FLOAT, GL_FALSE,
                                      static_cast<GLsizei>(m_pipeline->descriptor.vertexLayout.stride),
                                      reinterpret_cast<const void *>(offset + attribute.offset));
            }
        }

        void BindIndexBuffer(BufferHandle handle, Format format, std::size_t offset) override
        {
            auto *buffer = m_impl.buffers.Get(handle);
            if (!buffer || buffer->usage != BufferUsage::Index || format != Format::R32Uint)
                throw std::invalid_argument("Invalid RHI index buffer or format");
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->name);
            m_indexOffset = offset;
        }

        void BindUniformBuffer(std::uint32_t slot, BufferHandle handle) override
        {
            auto *buffer = m_impl.buffers.Get(handle);
            if (!buffer || buffer->usage != BufferUsage::Uniform)
                throw std::invalid_argument("Invalid RHI uniform buffer");
            glBindBufferBase(GL_UNIFORM_BUFFER, slot, buffer->name);
        }

        void BindTexture(std::uint32_t slot, TextureHandle textureHandle, SamplerHandle samplerHandle) override
        {
            auto *texture = m_impl.textures.Get(textureHandle);
            auto *sampler = m_impl.samplers.Get(samplerHandle);
            if (!texture || !sampler)
                throw std::invalid_argument("Invalid RHI texture or sampler");
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(texture->descriptor.depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D, texture->name);
            glBindSampler(slot, sampler->name);
        }

        void BindStorageImage(std::uint32_t slot, TextureHandle textureHandle, std::uint32_t mipLevel) override
        {
            auto *texture = m_impl.textures.Get(textureHandle);
            if (!texture)
                throw std::invalid_argument("Invalid OpenGL storage image");
            const auto format = ToTextureFormat(texture->descriptor.format);
            glBindImageTexture(slot, texture->name, static_cast<GLint>(mipLevel),
                               texture->descriptor.depth > 1 ? GL_TRUE : GL_FALSE,
                               0, GL_READ_WRITE, format.internalFormat);
        }

        void Draw(std::uint32_t count, std::uint32_t firstVertex) override
        {
            if (!m_rendering || !m_pipeline)
                throw std::logic_error("RHI draw requires active rendering and a pipeline");
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(firstVertex), static_cast<GLsizei>(count));
        }

        void DrawIndexed(std::uint32_t count, std::uint32_t firstIndex, std::int32_t vertexOffset) override
        {
            if (!m_rendering || !m_pipeline)
                throw std::logic_error("RHI indexed draw requires active rendering and a pipeline");
            if (vertexOffset != 0)
                glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT,
                                         reinterpret_cast<const void *>(m_indexOffset + firstIndex * sizeof(std::uint32_t)), vertexOffset);
            else
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT,
                               reinterpret_cast<const void *>(m_indexOffset + firstIndex * sizeof(std::uint32_t)));
        }

        void DrawIndexedInstanced(std::uint32_t count, std::uint32_t instanceCount,
                                  std::uint32_t firstIndex, std::int32_t vertexOffset) override
        {
            if (!m_rendering || !m_pipeline)
                throw std::logic_error("RHI instanced indexed draw requires active rendering and a pipeline");
            const auto indices = reinterpret_cast<const void *>(m_indexOffset + firstIndex * sizeof(std::uint32_t));
            if (vertexOffset != 0)
                glDrawElementsInstancedBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT,
                                                  indices, static_cast<GLsizei>(instanceCount), vertexOffset);
            else
                glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT,
                                        indices, static_cast<GLsizei>(instanceCount));
        }

        void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override
        {
            if (m_rendering || !m_pipeline || !m_pipeline->compute)
                throw std::logic_error("OpenGL compute dispatch requires a compute pipeline outside rendering");
            glDispatchCompute(x, y, z);
        }

        void ShaderMemoryBarrier() override
        {
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }
        void ClearStorageImageUint(TextureHandle textureHandle, std::uint32_t value) override
        {
            auto *texture = m_impl.textures.Get(textureHandle);
            if (!texture || !texture->descriptor.storage || texture->descriptor.format != Format::R32Uint)
                throw std::invalid_argument("OpenGL uint clear requires an R32Uint storage image");
            if (glClearTexImage)
            {
                glClearTexImage(texture->name, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &value);
                return;
            }
            // glClearTexImage is OpenGL 4.4 while compute/image load-store are
            // available in 4.3. Keep the RHI path functional on 4.3 contexts.
            const auto texelCount = static_cast<std::size_t>(texture->descriptor.width) *
                                    texture->descriptor.height * texture->descriptor.depth;
            std::vector<std::uint32_t> clearData(texelCount, value);
            const GLenum target = texture->descriptor.depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
            glBindTexture(target, texture->name);
            if (texture->descriptor.depth > 1)
                glTexSubImage3D(target, 0, 0, 0, 0,
                                static_cast<GLsizei>(texture->descriptor.width),
                                static_cast<GLsizei>(texture->descriptor.height),
                                static_cast<GLsizei>(texture->descriptor.depth),
                                GL_RED_INTEGER, GL_UNSIGNED_INT, clearData.data());
            else
                glTexSubImage2D(target, 0, 0, 0,
                                static_cast<GLsizei>(texture->descriptor.width),
                                static_cast<GLsizei>(texture->descriptor.height),
                                GL_RED_INTEGER, GL_UNSIGNED_INT, clearData.data());
        }

        void Submit() override {}

    private:
        OpenGLDevice::Impl &m_impl;
        PipelineResource *m_pipeline = nullptr;
        std::size_t m_indexOffset = 0;
        bool m_rendering = false;
        std::size_t m_activeColorAttachmentCount = 0;
    };

    class OpenGLSwapchain final : public ISwapchain
    {
    public:
        OpenGLSwapchain(OpenGLDevice::Impl &impl, const SwapchainDescriptor &descriptor)
            : m_impl(impl), m_window(static_cast<GLFWwindow *>(descriptor.nativeWindow)),
              m_width(descriptor.width), m_height(descriptor.height)
        {
            if (!m_window)
                throw std::invalid_argument("OpenGL swapchain requires a GLFW window");
            glfwSwapInterval(descriptor.vSync ? 1 : 0);
        }

        [[nodiscard]] Format GetFormat() const noexcept override { return Format::R8G8B8A8Srgb; }
        [[nodiscard]] std::uint32_t GetWidth() const noexcept override { return m_width; }
        [[nodiscard]] std::uint32_t GetHeight() const noexcept override { return m_height; }

        bool Resize(std::uint32_t width, std::uint32_t height) override
        {
            m_width = width;
            m_height = height;
            return width != 0 && height != 0;
        }

        bool Present(TextureHandle sourceHandle) override
        {
            const auto *source = m_impl.textures.Get(sourceHandle);
            if (!source || source->descriptor.usage != TextureUsage::ColorAttachment || m_width == 0 || m_height == 0)
                return false;

            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_impl.framebuffer);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source->name, 0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                return false;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0,
                              static_cast<GLint>(source->descriptor.width), static_cast<GLint>(source->descriptor.height),
                              0, 0, static_cast<GLint>(m_width), static_cast<GLint>(m_height),
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glfwSwapBuffers(m_window);
            return glGetError() == GL_NO_ERROR;
        }

    private:
        OpenGLDevice::Impl &m_impl;
        GLFWwindow *m_window = nullptr;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
    };

    OpenGLDevice::OpenGLDevice() : m_impl(std::make_unique<Impl>())
    {
        if (!glad_glGenBuffers)
            throw std::runtime_error("OpenGL RHI requires a current, initialized OpenGL context");
        glGenFramebuffers(1, &m_impl->framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_impl->framebuffer);
        LabelObject(GL_FRAMEBUFFER, m_impl->framebuffer, "RHI attachment framebuffer");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // The function can be exposed by ARB_clip_control on a 4.3 context, so
        // capability-test the entry point instead of requiring core 4.5.
        if (glad_glClipControl)
            glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
        m_impl->context = std::make_unique<OpenGLCommandContext>(*m_impl);
    }

    bool OpenGLDevice::UsesZeroToOneClipDepth() const noexcept
    {
        return glad_glClipControl != nullptr;
    }

    OpenGLDevice::~OpenGLDevice()
    {
        m_impl->pipelines.ForEach([](auto &r)
                                  { glDeleteVertexArrays(1, &r.vertexArray); glDeleteProgram(r.program); });
        m_impl->samplers.ForEach([](auto &r)
                                 { glDeleteSamplers(1, &r.name); });
        m_impl->textures.ForEach([](auto &r)
                                 { glDeleteTextures(1, &r.name); });
        m_impl->buffers.ForEach([](auto &r)
                                { glDeleteBuffers(1, &r.name); });
        if (m_impl->framebuffer)
            glDeleteFramebuffers(1, &m_impl->framebuffer);
    }

    std::unique_ptr<ISwapchain> OpenGLDevice::CreateSwapchain(const SwapchainDescriptor &descriptor)
    {
        return std::make_unique<OpenGLSwapchain>(*m_impl, descriptor);
    }

    BufferHandle OpenGLDevice::CreateBuffer(const BufferDescriptor &descriptor, std::span<const std::byte> data)
    {
        if (descriptor.size == 0 || data.size() > descriptor.size)
            throw std::invalid_argument("RHI buffer has an invalid size or initial data");
        GLuint name = 0;
        glGenBuffers(1, &name);
        glBindBuffer(BufferTarget(descriptor.usage), name);
        glBufferData(BufferTarget(descriptor.usage), static_cast<GLsizeiptr>(descriptor.size), data.empty() ? nullptr : data.data(), GL_DYNAMIC_DRAW);
        LabelObject(GL_BUFFER, name, descriptor.debugName);
        return m_impl->buffers.Insert(BufferResource{name, descriptor.size, descriptor.usage});
    }

    TextureHandle OpenGLDevice::CreateTexture(const TextureDescriptor &descriptor, std::span<const std::byte> data)
    {
        if (descriptor.width == 0 || descriptor.height == 0 || descriptor.depth == 0)
            throw std::invalid_argument("RHI texture dimensions must be non-zero");
        const auto format = ToTextureFormat(descriptor.format);
        GLuint name = 0;
        glGenTextures(1, &name);
        const GLenum target = descriptor.depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
        glBindTexture(target, name);
        const auto maximumDimension = std::max({descriptor.width, descriptor.height, descriptor.depth});
        const std::uint32_t fullMipCount = 1u + static_cast<std::uint32_t>(std::floor(std::log2(maximumDimension)));
        const std::uint32_t mipCount = descriptor.mipLevels != 0 ? descriptor.mipLevels
                                      : descriptor.usage == TextureUsage::Sampled ? fullMipCount : 1u;
        if (descriptor.depth > 1)
            glTexStorage3D(target, static_cast<GLsizei>(mipCount), format.internalFormat,
                           static_cast<GLsizei>(descriptor.width), static_cast<GLsizei>(descriptor.height),
                           static_cast<GLsizei>(descriptor.depth));
        else
            glTexStorage2D(target, static_cast<GLsizei>(mipCount), format.internalFormat,
                           static_cast<GLsizei>(descriptor.width), static_cast<GLsizei>(descriptor.height));
        if (!data.empty())
        {
            if (descriptor.depth > 1)
                glTexSubImage3D(target, 0, 0, 0, 0, static_cast<GLsizei>(descriptor.width),
                                static_cast<GLsizei>(descriptor.height), static_cast<GLsizei>(descriptor.depth),
                                format.format, format.type, data.data());
            else
                glTexSubImage2D(target, 0, 0, 0, static_cast<GLsizei>(descriptor.width),
                                static_cast<GLsizei>(descriptor.height), format.format, format.type, data.data());
            if (mipCount > 1)
                glGenerateMipmap(target);
        }
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        LabelObject(GL_TEXTURE, name, descriptor.debugName);
        return m_impl->textures.Insert(TextureResource{name, descriptor});
    }

    SamplerHandle OpenGLDevice::CreateSampler(const SamplerDescriptor &descriptor)
    {
        GLuint name = 0;
        glGenSamplers(1, &name);
        glSamplerParameteri(name, GL_TEXTURE_MIN_FILTER,
                            descriptor.mipFiltering
                                ? (descriptor.linearFiltering ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
                                : (descriptor.linearFiltering ? GL_LINEAR : GL_NEAREST));
        glSamplerParameteri(name, GL_TEXTURE_MAG_FILTER, descriptor.linearFiltering ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(name, GL_TEXTURE_WRAP_S, descriptor.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glSamplerParameteri(name, GL_TEXTURE_WRAP_T, descriptor.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        LabelObject(GL_SAMPLER, name, descriptor.debugName);
        return m_impl->samplers.Insert(SamplerResource{name});
    }

    PipelineHandle OpenGLDevice::CreateGraphicsPipeline(const GraphicsPipelineDescriptor &descriptor)
    {
        const GLuint vertex = CompileShader(GL_VERTEX_SHADER, descriptor.vertexShader.glsl, descriptor.debugName);
        GLuint fragment = 0;
        GLuint geometry = 0;
        GLuint program = 0;
        try
        {
            fragment = CompileShader(GL_FRAGMENT_SHADER, descriptor.fragmentShader.glsl, descriptor.debugName);
            if (!descriptor.geometryShader.glsl.empty())
                geometry = CompileShader(GL_GEOMETRY_SHADER, descriptor.geometryShader.glsl, descriptor.debugName);
            program = glCreateProgram();
            glAttachShader(program, vertex);
            if (geometry)
                glAttachShader(program, geometry);
            glAttachShader(program, fragment);
            glLinkProgram(program);
            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE)
            {
                GLint length = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
                glGetProgramInfoLog(program, length, nullptr, log.data());
                throw std::runtime_error(descriptor.debugName + " pipeline link failed: " + log);
            }
        }
        catch (...)
        {
            if (program)
                glDeleteProgram(program);
            if (fragment)
                glDeleteShader(fragment);
            if (geometry)
                glDeleteShader(geometry);
            glDeleteShader(vertex);
            throw;
        }
        glDeleteShader(fragment);
        if (geometry)
            glDeleteShader(geometry);
        glDeleteShader(vertex);
        GLuint vertexArray = 0;
        glGenVertexArrays(1, &vertexArray);
        glBindVertexArray(vertexArray);
        LabelObject(GL_PROGRAM, program, descriptor.debugName);
        LabelObject(GL_VERTEX_ARRAY, vertexArray, descriptor.debugName + " VAO");
        glBindVertexArray(0);
        return m_impl->pipelines.Insert(PipelineResource{program, vertexArray, descriptor, false});
    }

    PipelineHandle OpenGLDevice::CreateComputePipeline(const ComputePipelineDescriptor &descriptor)
    {
        const GLuint shader = CompileShader(GL_COMPUTE_SHADER, descriptor.computeShader.glsl, descriptor.debugName);
        GLuint program = glCreateProgram();
        glAttachShader(program, shader);
        glLinkProgram(program);
        glDeleteShader(shader);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE)
        {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            glDeleteProgram(program);
            throw std::runtime_error(descriptor.debugName + " pipeline link failed: " + log);
        }
        GLuint vertexArray = 0;
        glGenVertexArrays(1, &vertexArray);
        GraphicsPipelineDescriptor compatibility;
        compatibility.debugName = descriptor.debugName;
        return m_impl->pipelines.Insert(PipelineResource{program, vertexArray, std::move(compatibility), true});
    }

    void OpenGLDevice::UpdateBuffer(BufferHandle handle, std::size_t offset, std::span<const std::byte> data)
    {
        auto *buffer = m_impl->buffers.Get(handle);
        if (!buffer || offset > buffer->size || data.size() > buffer->size - offset)
            throw std::invalid_argument("Invalid, stale, or out-of-bounds RHI buffer update");
        glBindBuffer(BufferTarget(buffer->usage), buffer->name);
        glBufferSubData(BufferTarget(buffer->usage), static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(data.size()), data.data());
    }

    void OpenGLDevice::DestroyBuffer(BufferHandle h)
    {
        if (auto r = m_impl->buffers.Remove(h))
            glDeleteBuffers(1, &r->name);
    }
    void OpenGLDevice::DestroyTexture(TextureHandle h)
    {
        if (auto r = m_impl->textures.Remove(h))
            glDeleteTextures(1, &r->name);
    }
    void OpenGLDevice::DestroySampler(SamplerHandle h)
    {
        if (auto r = m_impl->samplers.Remove(h))
            glDeleteSamplers(1, &r->name);
    }
    void OpenGLDevice::DestroyPipeline(PipelineHandle h)
    {
        if (auto r = m_impl->pipelines.Remove(h))
        {
            glDeleteVertexArrays(1, &r->vertexArray);
            glDeleteProgram(r->program);
        }
    }
    ICommandContext &OpenGLDevice::GetImmediateContext() { return *m_impl->context; }
    std::uint64_t OpenGLDevice::GetTextureNativeHandle(TextureHandle handle) const noexcept
    {
        const auto *texture = m_impl->textures.Get(handle);
        return texture ? texture->name : 0;
    }
}
