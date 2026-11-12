#pragma once

#include <cstdint>
#include <memory>

namespace PlutoGE::platform
{
    // Unaccelerated relative mouse motion for a locked cursor, gathered away from the window's event queue.
    //
    // High polling-rate mice (1-8 kHz) deliver a raw input message per report. Read through the window's own queue,
    // every report is a message the frame's event poll must dispatch, which can cost milliseconds a frame while the
    // mouse moves. A source drains those reports elsewhere in batches and hands the main thread one accumulated delta.
    class RelativeMouseInput
    {
    public:
        struct Motion
        {
            // Device counts since the last Take: the unaccelerated distance the mouse moved.
            double x = 0.0;
            double y = 0.0;
            // Reports accumulated into this motion (for diagnostics).
            std::uint32_t reports = 0;
        };

        // The platform's source, or null where there is none; callers then fall back to the windowing library's
        // raw motion.
        [[nodiscard]] static std::unique_ptr<RelativeMouseInput> Create();

        virtual ~RelativeMouseInput() = default;
        // Motion accumulates only while capturing; reports arriving otherwise are discarded. Sources may receive
        // reports while the application is in the background, so capture only while the window has focus.
        virtual void SetCapturing(bool capturing) = 0;
        [[nodiscard]] virtual bool IsCapturing() const = 0;
        // Returns the motion accumulated since the previous call and resets it.
        virtual Motion Take() = 0;
    };
}
