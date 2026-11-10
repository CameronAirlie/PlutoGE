#pragma once

#include <algorithm>
#include <cstddef>

namespace PlutoGE::render
{
    // Whether a frame lights local lights in screen space (DeferredLocalLights), and which draws join it. Pure, so the
    // decision can be tested and changed apart from the pass.
    struct DeferredLocalLightPolicy
    {
        struct Frame
        {
            bool deviceSupported = false;  // the pass initialised (Vulkan, shaders present)
            bool specialised = false;      // pipelines take specialisation constants
            bool diagnosticOutput = false; // a debug view or diagnostic layout needs the forward path's outputs
            std::size_t localLights = 0;   // point, spot and rectangle lights this frame
            bool surfaceTargets = false;   // the geometry layout already writes normal, material and albedo
        };

        // Off restores forward local lighting everywhere (for comparisons).
        bool enabled = true;
        // Fewer local lights than this keep the forward path: each costs the forward shader little, while the pass
        // costs a full-screen read of the G-buffer and depth.
        std::size_t minimumLocalLights = 1;
        // Whether the geometry pass may add the normal, material and albedo targets just for this pass. Without it
        // the pass runs only when other effects already need them (SSAO, SSR, SSGI, decals).
        bool addSurfaceTargets = true;

        [[nodiscard]] bool Active(const Frame &frame) const noexcept
        {
            return enabled && frame.deviceSupported && frame.specialised && !frame.diagnosticOutput &&
                   frame.localLights >= std::max<std::size_t>(minimumLocalLights, 1) &&
                   (frame.surfaceTargets || addSurfaceTargets);
        }
    };
}
