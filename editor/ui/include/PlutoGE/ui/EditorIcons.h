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
    inline constexpr const char *Search = "\xef\x80\x82"; // magnifying-glass
    inline constexpr const char *Check = "\xef\x80\x8c"; // check
    inline constexpr const char *Close = "\xef\x80\x8d"; // xmark
    inline constexpr const char *Settings = "\xef\x80\x93"; // gear
    inline constexpr const char *Font = "\xef\x80\xb1"; // font
    inline constexpr const char *Contrast = "\xef\x81\x82"; // circle-half-stroke
    inline constexpr const char *Plus = "\xef\x81\xa7"; // plus
    inline constexpr const char *Copy = "\xef\x83\x85"; // copy
    inline constexpr const char *Save = "\xef\x83\x87"; // floppy-disk
    inline constexpr const char *Paste = "\xef\x83\xaa"; // clipboard
    inline constexpr const char *Trash = "\xef\x87\xb8"; // trash
    inline constexpr const char *WindowMaximize = "\xef\x8b\x90"; // window-maximize
    inline constexpr const char *WindowMinimize = "\xef\x8b\x91"; // window-minimize
    inline constexpr const char *WindowRestore = "\xef\x8b\x92"; // window-restore
    inline constexpr const char *Revert = "\xef\x8b\xaa"; // rotate-left
    inline constexpr const char *Palette = "\xef\x94\xbf"; // palette
    inline constexpr const char *Swatches = "\xef\x97\x83"; // swatchbook
    inline constexpr const char *Folder = "\xef\x81\xbb"; // folder
    inline constexpr const char *File = "\xef\x85\x9b"; // file
    inline constexpr const char *SceneAsset = "\xee\x94\xaf"; // mountain-sun
    inline constexpr const char *PrefabAsset = "\xef\x86\xb3"; // cubes
    inline constexpr const char *ScriptAsset = "\xef\x87\x89"; // file-code
    inline constexpr const char *MeshAsset = "\xef\x86\xb2"; // cube
    inline constexpr const char *AnimationAsset = "\xef\x9c\x8c"; // person-running
    inline constexpr const char *AnimationClipAsset = "\xef\x80\x88"; // film
    inline constexpr const char *ModelAsset = "\xef\x98\x9f"; // shapes
    inline constexpr const char *MaterialAsset = "\xef\x95\xb6"; // fill-drip
    inline constexpr const char *ShaderGraphAsset = "\xef\x95\x82"; // diagram-project
    inline constexpr const char *AnimationGraphAsset = "\xef\x83\xa8"; // sitemap
    inline constexpr const char *ParticleSystemAsset = "\xee\x8b\x8a"; // wand-magic-sparkles
    inline constexpr const char *AudioAsset = "\xef\x80\xa8"; // volume-high
    inline constexpr const char *TextureAsset = "\xef\x80\xbe"; // image
    inline constexpr const char *AssemblyAsset = "\xef\x84\xae"; // puzzle-piece
    inline constexpr const char *ScriptableObjectAsset = "\xef\x87\x80"; // database
    inline constexpr const char *RmlDocumentAsset = "\xee\x85\xa3"; // display
    inline constexpr const char *InputMappingAsset = "\xef\x84\x9b"; // gamepad
    inline constexpr const char *SurfaceResponseAsset = "\xef\x95\x8b"; // shoe-prints
    inline constexpr const char *LoadingScreenAsset = "\xef\x89\x92"; // hourglass-half
    inline constexpr ImWchar Ranges[] = {
        0xe163, 0xe163,
        0xe2ca, 0xe2ca,
        0xe52f, 0xe52f,
        0xf002, 0xf002,
        0xf008, 0xf008,
        0xf00c, 0xf00d,
        0xf013, 0xf013,
        0xf028, 0xf028,
        0xf031, 0xf031,
        0xf03e, 0xf03e,
        0xf042, 0xf042,
        0xf047, 0xf047,
        0xf065, 0xf065,
        0xf067, 0xf067,
        0xf06e, 0xf06e,
        0xf076, 0xf076,
        0xf078, 0xf078,
        0xf07b, 0xf07b,
        0xf0ac, 0xf0ac,
        0xf0c5, 0xf0c5,
        0xf0c7, 0xf0c7,
        0xf0e8, 0xf0e8,
        0xf0ea, 0xf0ea,
        0xf11b, 0xf11b,
        0xf12e, 0xf12e,
        0xf15b, 0xf15b,
        0xf1b2, 0xf1b3,
        0xf1c0, 0xf1c0,
        0xf1c9, 0xf1c9,
        0xf1de, 0xf1de,
        0xf1f8, 0xf1f8,
        0xf1fc, 0xf1fc,
        0xf252, 0xf252,
        0xf2d0, 0xf2d2,
        0xf2ea, 0xf2ea,
        0xf2f9, 0xf2f9,
        0xf424, 0xf424,
        0xf53f, 0xf53f,
        0xf542, 0xf542,
        0xf54b, 0xf54b,
        0xf576, 0xf576,
        0xf58d, 0xf58d,
        0xf5c3, 0xf5c3,
        0xf61f, 0xf61f,
        0xf70c, 0xf70c,
        0xf84c, 0xf84c,
        0
    };
}
