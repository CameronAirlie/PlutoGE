#include "PointShadowLayers.h"
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

int main()
try
{
    using PlutoGE::render::PointShadowLayers;
    const auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    constexpr auto settle = PointShadowLayers::StationaryFrames;

    // Twenty unchanging casters and one whose signature changes every frame.
    PointShadowLayers layers;
    std::vector<std::uint64_t> signatures(21);
    std::iota(signatures.begin(), signatures.end(), 100u);
    for (unsigned frame = 0; frame < settle; ++frame)
    {
        signatures[20] = 1000 + frame;
        layers.Observe(signatures);
        require(!layers.IsStationary(0), "Caster promoted before it settled");
    }
    signatures[20] = 5000;
    layers.Observe(signatures);
    require(layers.IsStationary(0) && layers.IsStationary(19), "Settled casters not stationary");
    require(!layers.IsStationary(20), "Moving caster stationary");

    std::vector<std::size_t> all(21), stationary, moving;
    std::iota(all.begin(), all.end(), std::size_t{0});
    require(layers.Partition(all, stationary, moving) && stationary.size() == 20 && moving == std::vector<std::size_t>{20},
            "Face was not split into its stationary and moving casters");

    // Draws shift position when something earlier in the list appears: the
    // casters keep their signatures, so they must stay stationary.
    signatures.insert(signatures.begin(), 7777);
    layers.Observe(signatures);
    require(!layers.IsStationary(0) && layers.IsStationary(1) && layers.IsStationary(20),
            "Reordered draws lost their stationary history");

    // One change demotes a caster at once.
    signatures[5] = 9999;
    layers.Observe(signatures);
    require(!layers.IsStationary(5), "Changed caster stayed stationary");

    // Not worth a static copy: too few stationary casters, or too many moving.
    std::vector<std::size_t> few{1, 2, 3};
    require(!layers.Partition(few, stationary, moving), "Split a face with too few casters");
    std::vector<std::size_t> busy;
    for (std::size_t i = 1; i <= 20; ++i) busy.push_back(i);
    PointShadowLayers fresh;
    fresh.Observe(signatures);
    require(!fresh.Partition(busy, stationary, moving) && moving.size() == busy.size(),
            "Split a face whose casters have no history");

    std::cout << "PASS: point shadow stationary layers\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
