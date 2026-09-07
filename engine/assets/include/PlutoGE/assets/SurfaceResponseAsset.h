#pragma once

#include <iosfwd>
#include <string>

namespace PlutoGE::assets
{
    enum class SurfaceEvent { Footstep, Impact };

    struct SurfaceResponse
    {
        std::string sound;
        std::string particles;
        std::string decalMaterial;
    };

    // An empty response intentionally produces no effect. Unknown surfaces use
    // these same defaults, so missing content never prevents gameplay queries.
    struct SurfaceResponseAsset
    {
        float friction = 0.5f;
        SurfaceResponse footstep;
        SurfaceResponse impact;

        const SurfaceResponse &GetResponse(SurfaceEvent event) const
        { return event == SurfaceEvent::Footstep ? footstep : impact; }
    };

    bool ReadSurfaceResponseAsset(std::istream &input, SurfaceResponseAsset &asset);
    bool WriteSurfaceResponseAsset(std::ostream &output, const SurfaceResponseAsset &asset);
}
