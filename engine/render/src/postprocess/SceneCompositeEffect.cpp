#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"

#include <array>
#include <string>

namespace PlutoGE::render
{
    namespace
    {
        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }

        constexpr std::array kIndirectDebugViews{
            IndirectDebugView::None,
            IndirectDebugView::SsgiOnly,
            IndirectDebugView::CombinedIndirect,
        };
        constexpr std::array kIndirectDebugViewNames{"None", "SSGI Only", "Combined Indirect"};

        int IndirectDebugViewIndex(IndirectDebugView view)
        {
            for (std::size_t index = 0; index < kIndirectDebugViews.size(); ++index)
            {
                if (kIndirectDebugViews[index] == view)
                    return static_cast<int>(index);
            }
            return 0;
        }

        IndirectDebugView FindIndirectDebugView(const std::string &name)
        {
            for (std::size_t index = 0; index < kIndirectDebugViewNames.size(); ++index)
            {
                if (name == kIndirectDebugViewNames[index])
                    return kIndirectDebugViews[index];
            }
            return IndirectDebugView::None;
        }

        // Accepts an option name, or an index into the options the value was saved with (older saves list views that
        // no longer exist, so their indices differ). Anything unknown reads as None.
        IndirectDebugView ParseIndirectDebugView(const PostProcessParameter &parameter)
        {
            std::size_t parsed = 0;
            int index = -1;
            try
            {
                index = std::stoi(parameter.value, &parsed);
            }
            catch (...)
            {
                return FindIndirectDebugView(parameter.value);
            }
            if (parsed != parameter.value.size() || index < 0)
                return FindIndirectDebugView(parameter.value);
            const auto &savedOptions = parameter.enumOptions;
            if (!savedOptions.empty())
                return static_cast<std::size_t>(index) < savedOptions.size()
                           ? FindIndirectDebugView(savedOptions[static_cast<std::size_t>(index)])
                           : IndirectDebugView::None;
            return static_cast<std::size_t>(index) < kIndirectDebugViews.size()
                       ? kIndirectDebugViews[static_cast<std::size_t>(index)]
                       : IndirectDebugView::None;
        }
    }

    std::vector<PostProcessParameter> SceneCompositeEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Enable SSGI",
                .type = PostProcessParameterType::Bool,
                .value = m_enableSsgi ? "true" : "false",
            },
            PostProcessParameter{
                .name = "Indirect Debug View",
                .type = PostProcessParameterType::Enum,
                .value = std::to_string(IndirectDebugViewIndex(m_indirectDebugView)),
                .enumOptions = std::vector<std::string>(kIndirectDebugViewNames.begin(), kIndirectDebugViewNames.end()),
            },
        };
    }

    void SceneCompositeEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Enable SSGI")
            {
                m_enableSsgi = ParseBool(parameter.value);
            }
            else if (parameter.name == "Indirect Debug View")
            {
                m_indirectDebugView = ParseIndirectDebugView(parameter);
            }
        }
    }
}