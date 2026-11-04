#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace PlutoGE::render
{
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

        bool operator==(const PostProcessParameter &) const = default;
    };

    class IPostProcessEffect
    {
    public:
        virtual ~IPostProcessEffect() = default;

        virtual std::string GetTypeName() const = 0;
        virtual std::string GetDisplayName() const { return GetTypeName(); }
        virtual std::vector<PostProcessParameter> GetParameters() const { return {}; }
        virtual void SetParameters(const std::vector<PostProcessParameter> &parameters) {}
        // Read-only facts editors show beneath the parameters, such as the cost and memory the current settings
        // imply, or caveats. Not serialized.
        virtual std::vector<std::string> GetSummary() const { return {}; }

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
        std::uint64_t m_configurationRevision = 0;
    };
}
