#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace PlutoGE::render
{
    struct RenderContext;
    class RenderTarget;

    enum class PostProcessParameterType
    {
        Float,
        Int,
        String,
        Bool,
        Enum,
        Color,
    };

    struct PostProcessParameter
    {
        std::string name;
        PostProcessParameterType type = PostProcessParameterType::String;
        std::string value;
        std::vector<std::string> enumOptions;
    };

    struct PostProcessContext
    {
        const RenderContext &renderContext;
        RenderTarget *sourceRenderTarget = nullptr;
        RenderTarget *destinationRenderTarget = nullptr;
        // Standalone effects propagate depth to their output. The post-process
        // chain prepares its reused targets once and disables the per-effect
        // copy to avoid repeatedly moving the same full-resolution buffer.
        bool copyDepthToDestination = true;
    };

    class IPostProcessEffect
    {
    public:
        virtual ~IPostProcessEffect() = default;

        virtual void Initialize() = 0;
        virtual void Apply(const PostProcessContext &context) = 0;

        void EnsureInitialized()
        {
            if (m_isInitialized)
            {
                return;
            }

            Initialize();
            m_isInitialized = true;
        }

        bool IsInitialized() const { return m_isInitialized; }

        virtual std::string GetTypeName() const = 0;
        virtual std::string GetDisplayName() const { return GetTypeName(); }
        virtual std::vector<PostProcessParameter> GetParameters() const { return {}; }
        virtual void SetParameters(const std::vector<PostProcessParameter> &parameters) {}

        // Route external configuration changes through this wrapper so render
        // passes can detect them without rebuilding and serializing every
        // effect's parameter list on every frame.
        void ApplyParameters(const std::vector<PostProcessParameter> &parameters)
        {
            SetParameters(parameters);
            ++m_configurationRevision;
        }

        std::uint64_t GetConfigurationRevision() const { return m_configurationRevision; }

        bool IsEnabled() const { return m_isEnabled; }
        void SetEnabled(bool enabled)
        {
            if (m_isEnabled == enabled)
                return;
            m_isEnabled = enabled;
            ++m_configurationRevision;
        }

    private:
        bool m_isEnabled = true;
        bool m_isInitialized = false;
        std::uint64_t m_configurationRevision = 0;
    };
}
