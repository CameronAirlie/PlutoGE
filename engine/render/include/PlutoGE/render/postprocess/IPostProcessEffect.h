#pragma once

#include <string>
#include <vector>

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
    };

    class IPostProcessEffect
    {
    public:
        virtual ~IPostProcessEffect() = default;

        virtual void Initialize() = 0;
        virtual void Apply(const PostProcessContext &context) = 0;

        virtual std::string GetTypeName() const = 0;
        virtual std::string GetDisplayName() const { return GetTypeName(); }
        virtual std::vector<PostProcessParameter> GetParameters() const { return {}; }
        virtual void SetParameters(const std::vector<PostProcessParameter> &parameters) {}

        bool IsEnabled() const { return m_isEnabled; }
        void SetEnabled(bool enabled) { m_isEnabled = enabled; }

    private:
        bool m_isEnabled = true;
    };
}