#include "PlutoGE/assets/Json.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/MapBake.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/ui/InputMappingDocument.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;

namespace
{
    void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

    scene::Entity *Box(scene::Scene &world, glm::vec3 position, glm::vec3 size, float yaw = 0, bool trigger = false)
    {
        auto *entity = world.AddEntity(std::make_unique<scene::Entity>());
        entity->SetPosition(position);
        entity->SetRotation({0, yaw, 0});
        entity->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.size = size, .isTrigger = trigger});
        return entity;
    }

    int Alpha(const scene::MapBakeResult &map, float worldX, float worldZ)
    {
        const int x = static_cast<int>(((worldX - map.centreX) / map.worldSize + .5f) * map.size);
        const int y = static_cast<int>(((worldZ - map.centreZ) / map.worldSize + .5f) * map.size);
        return map.rgba[(static_cast<std::size_t>(std::clamp(y, 0, map.size - 1)) * map.size + std::clamp(x, 0, map.size - 1)) * 4 + 3];
    }

    void Json()
    {
        assets::JsonValue value;
        std::string error;
        Check(assets::ParseJson(R"({"b": [1, -2.5e2, true, null], "a": "x\"\u00e9\ud83d\ude00", "keep": {"z": 0}})", value, &error), "Valid JSON rejected");
        Check(value.members[0].first == "b" && value.members[1].first == "a", "Member order was not preserved");
        Check(value.Find("b")->items[1].number == -250 && value.Find("b")->items[2].boolean, "Number or bool parsed wrongly");
        Check(value.GetString("a") == "x\"\xC3\xA9\xF0\x9F\x98\x80", "Escapes or surrogate pairs decoded wrongly");
        value.Set("a", assets::JsonValue::MakeNumber(0.15));
        Check(value.members[1].first == "a", "Set moved an existing member");
        assets::JsonValue again;
        Check(assets::ParseJson(assets::WriteJson(value), again) && again.GetNumber("a") == 0.15 && again.Find("keep"), "Round trip lost data");
        for (const char *bad : {"{", "[1,]", "{\"a\":1,}", "01", "\"\\x\"", "1 2", "{'a':1}", "-", "NaN"})
            Check(!assets::ParseJson(bad, again), bad);
        std::cout << "PASS: JSON order, escapes, numbers, round trip and strict rejection\n";
    }

    void Bake()
    {
        scene::Scene world;
        Box(world, {0, -.5f, 0}, {400, 1, 400});            // Ground plane: ignored.
        Box(world, {0, 1.5f, -10}, {20, 3, .3f});            // North wall.
        Box(world, {0, 1.5f, 10}, {20, 3, .3f});             // South wall.
        Box(world, {5, 1, 0}, {2, 2, 2}, 45);                // Rotated crate.
        Box(world, {-5, 1, 0}, {2, 2, 2}, 0, true);          // Trigger: not solid.
        Box(world, {-5, .05f, 5}, {4, .1f, 4});              // Floor slab.
        auto *ignored = Box(world, {-5, 1, -5}, {2, 2, 2});
        ignored->AddTag("MapIgnore");
        auto *nav = world.AddEntity(std::make_unique<scene::Entity>());
        scene::NavigationBakeSettings bounds;
        bounds.boundsMin = {-15, -1, -15};
        bounds.boundsMax = {15, 5, 15};
        nav->CreateComponent<scene::NavigationMeshComponent>(bounds);

        scene::MapBakeSettings settings;
        settings.size = 256;
        settings.padding = 2;
        const auto map = scene::BakeTopDownMap(world, settings);
        Check(map.success && map.size == 256 && map.rgba.size() == 256u * 256u * 4u, map.message.c_str());
        Check(map.obstacleCount == 3 && map.floorCount == 1, "Obstacle/floor classification is wrong");
        Check(std::abs(map.centreZ) < .01f && std::abs(map.worldSize - 24.3f) < .2f, "Bounds were not cropped and padded");
        Check(Alpha(map, 0, -10) > 200 && Alpha(map, 0, 10) > 200, "Walls were not drawn");
        Check(Alpha(map, 5, 0) > 200 && Alpha(map, 5 + 1.3f, 0) > 0, "Rotated crate footprint missing its corner");
        Check(Alpha(map, -5, 0) == 0, "Trigger was drawn");
        Check(Alpha(map, -5, -5) == 0, "MapIgnore entity was drawn");
        Check(Alpha(map, -5, 5) > 0 && Alpha(map, -5, 5) < 200, "Floor slab not drawn faintly");
        Check(Alpha(map, 0, 0) == 0, "Open ground was filled");
        // North-up: the north wall is in the top half of the image.
        Check(Alpha(map, 0, -10) > 0 && (-10 - map.centreZ) / map.worldSize + .5f < .5f, "Map is not north-up");

        const auto json = scene::MakeMapLayoutJson("Test", "project://Maps/Test.tga", map.centreX, map.centreZ, map.worldSize);
        assets::JsonValue layout;
        Check(assets::ParseJson(json, layout) && layout.GetString("Image") == "project://Maps/Test.tga" &&
              std::abs(layout.GetNumber("WorldSize") - map.worldSize) < .001, "Layout JSON is wrong");

        const auto path = std::filesystem::temp_directory_path() / "plutoge-map-bake-test.tga";
        std::string error;
        Check(scene::WriteTga(path, map.size, map.size, map.rgba, &error), error.c_str());
        std::ifstream tga(path, std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(tga), {}};
        Check(bytes.size() == 18 + map.rgba.size() && bytes[2] == 2 && static_cast<unsigned char>(bytes[17]) == 0x28 &&
              static_cast<unsigned char>(bytes[18 + 2]) == map.rgba[0], "TGA header or BGRA order is wrong");
        tga.close();
        std::filesystem::remove(path);

        scene::Scene empty;
        Check(!scene::BakeTopDownMap(empty).success, "Empty scene baked");
        std::cout << "PASS: map bake classification, triggers/tags, rotation, cropping, north-up, layout JSON and TGA\n";
    }

    void InputMapping()
    {
        const char *source = R"({
  "Version": 2,
  "Actions": [
    {
      "Name": "Sprint",
      "Bindings": [
        { "Kind": "Key", "Key": "LeftShift", "Scale": 1, "DeadZone": 0 },
        { "Kind": "GamepadButton", "Button": "LeftStick", "Scale": 1, "DeadZone": 0.15, "Note": "keep" }
      ]
    },
    { "Name": "Jump", "Bindings": [ { "Kind": "GamepadButton", "Button": "A", "Scale": 1, "DeadZone": 0.15 } ] },
    { "Name": "MenuAccept", "Bindings": [ { "Kind": "GamepadButton", "Button": "A", "Scale": 1, "DeadZone": 0.15 },
                                          { "Kind": "Key", "Key": "F13Custom", "Scale": 1, "DeadZone": 0 } ] }
  ]
})";
        ui::InputMappingDocument document;
        std::string error;
        Check(document.Parse(source, &error), error.c_str());
        Check(document.Actions()[0].bindings[0].value == "LeftShift", "Listed key lost");
        Check(document.Actions()[2].bindings[1].value == "F13Custom", "Unlisted key not preserved");
        assets::JsonValue saved;
        Check(assets::ParseJson(document.Serialize(), saved), "Serialized mapping is invalid");
        Check(saved.GetNumber("Version") == 2, "Top-level field lost");
        const auto &sprint = saved.Find("Actions")->items[0].Find("Bindings")->items;
        Check(sprint[0].GetString("Key") == "LeftShift" && !sprint[0].Find("Button"), "Key binding rewritten");
        Check(sprint[1].GetString("Note") == "keep" && sprint[1].GetNumber("DeadZone") == .15, "Unknown field or dead zone lost");
        Check(saved.Find("Actions")->items[2].Find("Bindings")->items[1].GetString("Key") == "F13Custom", "Unlisted key rewritten");

        document.Actions()[1].bindings[0].kind = ui::InputBindingKind::Key;
        document.Actions()[1].bindings[0].value = "Space";
        Check(ui::InputMappingDocument().Parse(document.Serialize()), "Kind change produced invalid JSON");
        assets::JsonValue changed;
        assets::ParseJson(document.Serialize(), changed);
        const auto &jump = changed.Find("Actions")->items[1].Find("Bindings")->items[0];
        Check(jump.GetString("Kind") == "Key" && jump.GetString("Key") == "Space" && !jump.Find("Button"), "Stale field kept after kind change");

        ui::InputMappingDocument shared;
        shared.Parse(source);
        const auto inputs = shared.FindSharedInputs();
        Check(inputs.size() == 1 && inputs[0].actions.size() == 2, "Shared gamepad A not reported");
        const auto unused = shared.FindUnusedGamepadButtons();
        Check(std::find(unused.begin(), unused.end(), "RightStick") != unused.end() &&
              std::find(unused.begin(), unused.end(), "A") == unused.end(), "Unused button report is wrong");
        shared.Actions()[1].name = "Sprint";
        Check(shared.FindInvalidActionNames() == std::vector<std::string>{"Sprint"}, "Duplicate action name not reported");
        Check(!ui::InputMappingDocument().Parse("{\"Actions\": 3}"), "Malformed mapping accepted");
        std::cout << "PASS: input mapping preservation, unlisted keys, kind changes, shared/unused inputs and name validation\n";
    }

    void InputMappingTriggers()
    {
        // Roll and Crouch share pad B (double tap vs exclusive single tap); Walk holds a key that Roll taps.
        const char *source = R"({
  "Actions": [
    { "Name": "Roll", "Bindings": [
        { "Kind": "Key", "Key": "LeftAlt", "Scale": 1, "DeadZone": 0, "Trigger": { "Kind": "Tap", "Duration": 0.2 } },
        { "Kind": "GamepadButton", "Button": "B", "Scale": 1, "DeadZone": 0.15,
          "Trigger": { "Kind": "MultiTap", "Count": 2, "Window": 0.25, "Note": "keep" } } ] },
    { "Name": "Walk", "Bindings": [
        { "Kind": "Key", "Key": "LeftAlt", "Scale": 1, "DeadZone": 0, "Trigger": { "Kind": "Hold", "Duration": 0.2 } } ] },
    { "Name": "Crouch", "Bindings": [
        { "Kind": "GamepadButton", "Button": "B", "Scale": 1, "DeadZone": 0.15,
          "Trigger": { "Window": 0.25, "Kind": "MultiTap", "Count": 1, "Exclusive": true } },
        { "Kind": "Key", "Key": "C", "Scale": 1, "DeadZone": 0 } ] },
    { "Name": "Jump", "Bindings": [ { "Kind": "GamepadButton", "Button": "A", "Scale": 1, "DeadZone": 0.15 } ] },
    { "Name": "Accept", "Bindings": [ { "Kind": "GamepadButton", "Button": "A", "Scale": 1, "DeadZone": 0.15 } ] }
  ]
})";
        ui::InputMappingDocument document;
        std::string error;
        Check(document.Parse(source, &error), error.c_str());
        const auto &roll = document.Actions()[0].bindings;
        const auto &crouch = document.Actions()[2].bindings;
        Check(roll[0].trigger.kind == ui::InputTriggerKind::Tap && std::abs(roll[0].trigger.duration - .2f) < 1e-5f, "Tap trigger not parsed");
        Check(roll[1].trigger.kind == ui::InputTriggerKind::MultiTap && roll[1].trigger.count == 2, "Multi-tap trigger not parsed");
        Check(crouch[0].trigger.exclusive && crouch[1].trigger.IsDefault(), "Exclusive flag lost or a missing trigger invented");
        Check(roll[1].Label() == "Pad 0 B (double tap)" && crouch[0].Label() == "Pad 0 B (single tap (exclusive))" &&
              crouch[1].Label() == "Key C", "Trigger labels are wrong");

        assets::JsonValue before, after;
        Check(assets::ParseJson(source, before) && assets::ParseJson(document.Serialize(), after) &&
              assets::WriteJson(before) == assets::WriteJson(after), "Unedited triggers did not save unchanged");

        // Changing the kind keeps unknown fields but drops settings the new kind does not read; Press removes the trigger.
        document.Actions()[0].bindings[1].trigger.kind = ui::InputTriggerKind::Hold;
        document.Actions()[0].bindings[0].trigger.kind = ui::InputTriggerKind::Press;
        assets::JsonValue changed;
        Check(assets::ParseJson(document.Serialize(), changed), "Changed triggers produced invalid JSON");
        const auto &bindings = changed.Find("Actions")->items[0].Find("Bindings")->items;
        const auto *hold = bindings[1].Find("Trigger");
        Check(hold && hold->GetString("Kind") == "Hold" && hold->Find("Duration") && !hold->Find("Count") && !hold->Find("Window") &&
              hold->GetString("Note") == "keep", "Kind change kept stale settings or lost unknown fields");
        Check(!bindings[0].Find("Trigger"), "Press trigger was written");

        // LeftAlt and B are told apart by their triggers; A (Jump and Accept, both plain presses) is not.
        ui::InputMappingDocument fresh;
        fresh.Parse(source);
        const auto sharedInputs = fresh.FindSharedInputs();
        Check(sharedInputs.size() == 3, "Shared inputs not reported");
        for (const auto &input : sharedInputs)
            Check(input.disambiguatedByTriggers == (input.inputId != "GamepadButton:A@0"),
                  "Trigger disambiguation reported wrongly");
        std::cout << "PASS: input mapping triggers parse, label, round-trip unchanged, drop stale settings and disambiguate shared inputs\n";
    }
}

// Optional: PlutoGEMapBakeTests <mapping.plutoinput> checks a real project mapping survives an unedited save.
int main(int argc, char **argv) try
{
    Json();
    Bake();
    InputMapping();
    InputMappingTriggers();
    if (argc > 1)
    {
        std::ifstream file(argv[1], std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(file), {}};
        ui::InputMappingDocument document;
        std::string error;
        Check(document.Parse(text, &error), error.c_str());
        assets::JsonValue before, after;
        Check(assets::ParseJson(text, before) && assets::ParseJson(document.Serialize(), after), "Mapping did not re-parse");
        Check(assets::WriteJson(before) == assets::WriteJson(after), "Unedited save changed the mapping");
        std::cout << "PASS: " << document.Actions().size() << " actions in " << argv[1] << " round-trip unchanged" << std::endl;
    }
    return 0;
}
catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
