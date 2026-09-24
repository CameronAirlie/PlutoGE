#pragma once
#include <iosfwd>
#include <string>

namespace PlutoGE::assets
{
    // Project content, independent of the scene being replaced and of its host.
    struct LoadingScreenAsset
    {
        std::string document;
        std::string controller;
    };
    bool ReadLoadingScreenAsset(std::istream &input, LoadingScreenAsset &asset);
    bool WriteLoadingScreenAsset(std::ostream &output, const LoadingScreenAsset &asset);
}
