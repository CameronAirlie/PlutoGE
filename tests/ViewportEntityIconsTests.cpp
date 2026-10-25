#include "PlutoGE/ui/ViewportEntityIcons.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
try
{
    using namespace PlutoGE::ui;
    const auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };

    require(ViewportEntityIconAlpha(10.0f, 60.0f, 150.0f) == 1.0f, "Near icon faded");
    require(std::abs(ViewportEntityIconAlpha(105.0f, 60.0f, 150.0f) - 0.5f) < 1e-5f, "Fade is not linear");
    require(ViewportEntityIconAlpha(150.0f, 60.0f, 150.0f) == 0.0f, "Icon visible at the fade end");
    require(ViewportEntityIconAlpha(500.0f, 60.0f, 60.0f) == 0.0f, "Degenerate fade range kept a far icon");

    const std::vector<ViewportEntityIconHit> hits{
        {1, {100.0f, 100.0f}, 10.0f, 30.0f},
        {2, {105.0f, 100.0f}, 10.0f, 5.0f},   // Overlaps 1, nearer: drawn on top.
        {3, {300.0f, 100.0f}, 10.0f, 1.0f},
    };
    require(PickViewportEntityIcon(hits, {104.0f, 100.0f}) == 2u, "Overlapping icons did not pick the nearest");
    require(PickViewportEntityIcon(hits, {91.0f, 100.0f}) == 1u, "Icon edge not clickable");
    require(PickViewportEntityIcon(hits, {300.0f, 111.0f}) == std::nullopt, "Click outside an icon picked it");
    require(PickViewportEntityIcon({}, {0.0f, 0.0f}) == std::nullopt, "Empty icon list picked something");

    std::cout << "PASS: viewport entity icon fading and picking\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
