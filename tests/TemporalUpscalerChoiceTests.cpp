#include "PlutoGE/render/rhi/Types.h"

#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace PlutoGE::render::rhi;

    int failures = 0;
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    }

    void ChoiceReplacesOnlyWhatAPlayerChooses()
    {
        TemporalUpscalerOptions host;
        host.technology = TemporalUpscaler::Dlss;
        host.quality = UpscalerQuality::Balanced;
        host.hdr = false;
        host.autoExposure = false;
        host.sharpness = 0.1f;
        const TemporalUpscalerChoice choice{TemporalUpscaler::Fsr2, UpscalerQuality::Performance, 0.6f};
        const auto applied = choice.ApplyTo(host);
        Require(applied.technology == TemporalUpscaler::Fsr2 && applied.quality == UpscalerQuality::Performance &&
                    applied.sharpness == 0.6f,
                "The choice sets the technology, quality and sharpness");
        Require(!applied.hdr && !applied.autoExposure, "The host's frame description (HDR, exposure) is kept");
        Require(TemporalUpscalerChoice::From(applied) == choice, "From reads back what ApplyTo wrote");
    }

    void ValidationRejectsOutOfRangeValues()
    {
        Require(TemporalUpscalerChoice{}.IsValid(), "The default choice (no upscaler) is valid");
        Require(TemporalUpscalerChoice{TemporalUpscaler::Dlss, UpscalerQuality::Dlaa, 1.0f}.IsValid(),
                "DLAA at full sharpness is valid");
        Require(!TemporalUpscalerChoice{static_cast<TemporalUpscaler>(3), UpscalerQuality::Quality, 0.25f}.IsValid(),
                "An unknown technology is rejected");
        Require(!TemporalUpscalerChoice{TemporalUpscaler::Fsr2, static_cast<UpscalerQuality>(5), 0.25f}.IsValid(),
                "An unknown quality is rejected");
        Require(!TemporalUpscalerChoice{TemporalUpscaler::Fsr2, UpscalerQuality::Quality, -0.01f}.IsValid() &&
                    !TemporalUpscalerChoice{TemporalUpscaler::Fsr2, UpscalerQuality::Quality, 1.01f}.IsValid(),
                "Sharpness outside 0-1 is rejected");
        Require(!TemporalUpscalerChoice{TemporalUpscaler::Fsr2, UpscalerQuality::Quality,
                                        std::numeric_limits<float>::quiet_NaN()}.IsValid(),
                "A NaN sharpness is rejected");
    }
}

int main()
{
    ChoiceReplacesOnlyWhatAPlayerChooses();
    ValidationRejectsOutOfRangeValues();
    if (failures)
        return 1;
    std::cout << "TemporalUpscalerChoice tests passed\n";
    return 0;
}
