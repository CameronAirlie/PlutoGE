#pragma once

#include <cstdint>

#ifdef _WIN32
#define PLUTO_UPSCALER_API_CALL __cdecl
#else
#define PLUTO_UPSCALER_API_CALL
#endif

namespace PlutoGE::scripting
{
    // The native side of the managed GraphicsSettings upscaler API (ScriptBridge.Upscaler.cs, RegisterUpscalerApi): one
    // entry point taking a request whose layout both sides check by size. Technology and quality carry the values of
    // render::rhi::TemporalUpscaler and render::rhi::UpscalerQuality. The game's choice is held by the engine
    // (Engine::SetRuntimeTemporalUpscaler) and ends when the runtime stops.
    enum class UpscalerScriptOperation : std::int32_t
    {
        // -> technology, quality, sharpness the frames render with; overridden = 1 when the game chose them.
        Get = 0,
        // technology, quality, sharpness -> result 1 when valid and applied (the choice lasts until the runtime stops).
        Set = 1,
        // -> result 1: back to the host's (the project's) upscaler.
        Reset = 2,
        // technology -> supported, and the reason in `reason` when not.
        GetSupport = 3,
    };

    struct NativeUpscalerRequest
    {
        std::int32_t operation = 0;
        std::int32_t technology = 0;
        std::int32_t quality = 0;
        float sharpness = 0.0f;
        std::int32_t result = 0;
        std::int32_t supported = 0;
        std::int32_t overridden = 0;
        char reason[256]{};
    };
    static_assert(sizeof(NativeUpscalerRequest) == 284, "Keep in step with ScriptBridge.Upscaler.cs");

    // Returns 0 when the request is malformed or invalid (nothing changes).
    std::int32_t PLUTO_UPSCALER_API_CALL ControlUpscaler(NativeUpscalerRequest *request);
}
