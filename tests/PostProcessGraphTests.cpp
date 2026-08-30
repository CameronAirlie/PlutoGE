#include "PlutoGE/render/PostProcessGraph.h"

#include <stdexcept>

int main()
{
    using namespace PlutoGE::render;
    PostProcessGraph graph;
    const auto scene = graph.AddResource({.name = "Scene", .lifetime = PostProcessResourceLifetime::External});
    const auto bright = graph.AddResource({.name = "Bloom bright", .widthScale = 0.5f, .heightScale = 0.5f});
    const auto blurred = graph.AddResource({.name = "Bloom blur", .widthScale = 0.25f, .heightScale = 0.25f});
    const auto output = graph.AddResource({.name = "Output"});
    const auto composite = graph.AddPass({.name = "Composite", .reads = {scene, blurred}, .writes = {output}});
    const auto downsample = graph.AddPass({.name = "Downsample", .reads = {bright}, .writes = {blurred}});
    const auto prefilter = graph.AddPass({.name = "Prefilter", .reads = {scene}, .writes = {bright}});
    const auto compiled = graph.Compile();
    if (compiled.passOrder.size() != 3 || compiled.passOrder[0] != prefilter ||
        compiled.passOrder[1] != downsample || compiled.passOrder[2] != composite)
        return 1;

    PostProcessGraph invalid;
    const auto orphan = invalid.AddResource({.name = "Orphan"});
    const auto invalidOutput = invalid.AddResource({.name = "Invalid output"});
    invalid.AddPass({.name = "Invalid", .reads = {orphan}, .writes = {invalidOutput}});
    try { (void)invalid.Compile(); }
    catch (const std::logic_error &) { return 0; }
    return 2;
}
