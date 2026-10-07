#pragma once
#include <imgui.h>

// Font Awesome Free 6.7.2, solid. Font and license: editor/resources/fonts/.
namespace PlutoGE::ui::icons
{
    inline constexpr const char *Move = "\xef\x81\x87"; // arrows-up-down-left-right
    inline constexpr const char *Rotate = "\xef\x8b\xb9"; // rotate-right
    inline constexpr const char *Scale = "\xef\x90\xa4"; // up-right-and-down-left-from-center
    inline constexpr const char *Grid = "\xef\xa1\x8c"; // border-all
    inline constexpr const char *Snap = "\xef\x81\xb6"; // magnet
    inline constexpr const char *Paint = "\xef\x87\xbc"; // paintbrush
    inline constexpr const char *View = "\xef\x81\xae"; // eye
    inline constexpr const char *Quality = "\xef\x87\x9e"; // sliders
    inline constexpr const char *Space = "\xef\x82\xac"; // globe
    inline constexpr const char *Frame = "\xef\x81\xa5"; // expand
    inline constexpr const char *Grip = "\xef\x96\x8d"; // grip
    inline constexpr const char *Chevron = "\xef\x81\xb8"; // chevron-down
    inline constexpr ImWchar Ranges[] = {
        0xf047, 0xf047,
        0xf065, 0xf065,
        0xf06e, 0xf06e,
        0xf076, 0xf076,
        0xf078, 0xf078,
        0xf0ac, 0xf0ac,
        0xf1de, 0xf1de,
        0xf1fc, 0xf1fc,
        0xf2f9, 0xf2f9,
        0xf424, 0xf424,
        0xf58d, 0xf58d,
        0xf84c, 0xf84c,
        0
    };
}
