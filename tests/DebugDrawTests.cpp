#include "PlutoGE/render/DebugDraw.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
    void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
}
int main()
{
    try
    {
        using namespace PlutoGE::render;
        DebugDraw draw;
        DebugDrawCommand line;
        line.end = {1,2,3};
        line.category = "Physics";
        Require(draw.Add(line), "Line rejected");
        auto sphere = line;
        sphere.primitive = DebugPrimitive::Sphere; sphere.duration = 1;
        Require(draw.Add(sphere), "Sphere rejected");
        auto label = line;
        label.primitive = DebugPrimitive::Label; label.text = "Player"; label.duration = 2;
        label.category = "AI";
        Require(draw.Add(label), "Label rejected");
        Require(draw.Snapshot().size() == 3, "Commands missing");
        draw.Advance(0);
        Require(draw.Snapshot().size() == 3, "Pause expired commands");
        draw.Advance(0.25f);
        Require(draw.Snapshot().size() == 2, "One-frame command did not expire");
        draw.SetCategoryVisible("Physics", false);
        Require(draw.Snapshot().size() == 1, "Category filtering failed");
        draw.Advance(0.75f);
        draw.SetCategoryVisible("Physics", true);
        Require(draw.Snapshot().size() == 1, "Hidden duration did not expire");
        draw.SetEnabled(false);
        Require(draw.Snapshot().empty(), "Global visibility failed");
        draw.SetEnabled(true);
        Require(draw.Snapshot().size() == 1, "Visibility removed commands");
        draw.Advance(1);
        Require(draw.Snapshot().empty(), "Timed label did not expire");
        auto invalid = line;
        invalid.start.x = std::numeric_limits<float>::quiet_NaN();
        Require(!draw.Add(invalid), "Non-finite position accepted");
        invalid = sphere; invalid.radius = -1;
        Require(!draw.Add(invalid), "Negative radius accepted");
        invalid = line; invalid.duration = std::numeric_limits<float>::infinity();
        Require(!draw.Add(invalid), "Infinite lifetime accepted");
        invalid = label; invalid.text = std::string(257, 'x');
        Require(!draw.Add(invalid), "Oversized label accepted");
        invalid = line; invalid.category = std::string(65, 'x');
        Require(!draw.Add(invalid), "Oversized category accepted");
        Require(draw.Dropped() == 5, "Rejection counter incorrect");
        draw.Clear();
        Require(draw.Categories().empty() && draw.Dropped() == 0, "Clear did not reset category/counter state");
        for (std::size_t i = 0; i < DebugDraw::MaxCategories; ++i)
        {
            line.category = std::to_string(i);
            Require(draw.Add(line), "Category budget rejected too early");
        }
        line.category = "overflow";
        Require(!draw.Add(line), "Category budget exceeded");
        draw.Clear(); line.category = "Default";
        for (std::size_t i = 0; i < DebugDraw::MaxCommands; ++i) Require(draw.Add(line), "Command budget rejected too early");
        Require(!draw.Add(line), "Command budget exceeded");
        draw.Advance(1);
        Require(draw.Add(line), "Expired capacity not reusable");
        draw.Clear();
        std::jthread producer([&] { for (int i = 0; i < 1000; ++i) draw.Add(line); });
        for (int i = 0; i < 100; ++i) { draw.Snapshot(); draw.SetEnabled(i % 2 == 0); }
        producer.join(); draw.SetEnabled(true);
        Require(draw.Snapshot().size() == 1000, "Concurrent producers/reads lost commands");
        auto copy = draw.Snapshot(); copy[0].text = "changed";
        Require(draw.Snapshot()[0].text.empty(), "Snapshot shares mutable storage");
        std::cout << "PASS: debug drawing lifetime, pause, filtering, validation, bounds, reset and concurrent snapshots\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
