#include "UpscalerScriptApi.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/rhi/Types.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace PlutoGE::scripting
{
    namespace
    {
        using render::rhi::TemporalUpscaler;
        using render::rhi::TemporalUpscalerChoice;
        using render::rhi::UpscalerQuality;

        void CopyReason(NativeUpscalerRequest &request, std::string_view text)
        {
            const auto length = (std::min)(text.size(), sizeof(request.reason) - 1);
            std::memcpy(request.reason, text.data(), length);
            request.reason[length] = '\0';
        }

        bool IsTechnology(std::int32_t value)
        {
            return value >= 0 && value <= static_cast<std::int32_t>(TemporalUpscaler::Fsr2);
        }

        bool IsQuality(std::int32_t value)
        {
            return value >= 0 && value <= static_cast<std::int32_t>(UpscalerQuality::Dlaa);
        }

        void WriteChoice(NativeUpscalerRequest &request, const TemporalUpscalerChoice &choice)
        {
            request.technology = static_cast<std::int32_t>(choice.technology);
            request.quality = static_cast<std::int32_t>(choice.quality);
            request.sharpness = choice.sharpness;
        }
    }

    std::int32_t PLUTO_UPSCALER_API_CALL ControlUpscaler(NativeUpscalerRequest *request)
    {
        if (!request)
            return 0;
        request->result = 0;
        request->reason[0] = '\0';
        // Never unwind C++ through a managed callback (renderer reconfiguration can throw).
        try
        {
            auto &engine = core::Engine::GetInstance();
            switch (static_cast<UpscalerScriptOperation>(request->operation))
            {
            case UpscalerScriptOperation::Get:
                WriteChoice(*request, TemporalUpscalerChoice::From(engine.GetTemporalUpscalerOptions()));
                request->overridden = engine.GetRuntimeTemporalUpscaler().has_value() ? 1 : 0;
                request->result = 1;
                break;
            case UpscalerScriptOperation::Set:
            {
                if (!IsTechnology(request->technology) || !IsQuality(request->quality))
                    return 0;
                const TemporalUpscalerChoice choice{static_cast<TemporalUpscaler>(request->technology),
                                                    static_cast<UpscalerQuality>(request->quality), request->sharpness};
                request->result = engine.SetRuntimeTemporalUpscaler(choice) ? 1 : 0;
                break;
            }
            case UpscalerScriptOperation::Reset:
                request->result = engine.SetRuntimeTemporalUpscaler(std::nullopt) ? 1 : 0;
                break;
            case UpscalerScriptOperation::GetSupport:
            {
                if (!IsTechnology(request->technology))
                    return 0;
                const auto support = engine.GetTemporalUpscalerSupport(static_cast<TemporalUpscaler>(request->technology));
                request->supported = support.supported ? 1 : 0;
                CopyReason(*request, support.reason);
                request->result = 1;
                break;
            }
            default:
                return 0;
            }
        }
        catch (...)
        {
            return 0;
        }
        return request->result;
    }
}
