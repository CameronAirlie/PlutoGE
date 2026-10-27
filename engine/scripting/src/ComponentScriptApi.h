#pragma once

#include <cstdint>

#ifdef _WIN32
#define PLUTO_COMPONENT_API_CALL __cdecl
#else
#define PLUTO_COMPONENT_API_CALL
#endif

namespace PlutoGE::scripting
{
    // The native side of the managed RegisteredComponent and UI classes
    // (ScriptBridge.Components.cs, RegisterComponentPropertyApi). Components are
    // named by their ComponentRegistry type name and properties by their
    // serialized names, so any registered component is scriptable without
    // new bridge code.
    enum class ComponentScriptOperation : std::int32_t
    {
        HasComponent = 0,     // result 1 when `entity` has `typeName`
        AddComponent = 1,     // result 1 when added (or already present)
        RemoveComponent = 2,  // result 1 when removed
        GetProperty = 3,      // resultText = value; result 1 when the property exists
        SetProperty = 4,      // result 1 when applied
        GetUIFocus = 5,       // resultEntity = focused UI element (0 for none)
        SetUIFocus = 6,       // focuses `entity` (0 clears)
        IsPointerOverUI = 7,  // result 1 while interactive UI has the pointer
        IsKeyboardCaptured = 8, // result 1 while a text field is being edited
        PollUIEvents = 9,     // copies the latest UI frame's events; result = count, sequence = UI update number
        CreateEntity = 10,    // a new entity named `value` under `entity` (0 = scene root); resultEntity = its id
        GetParent = 11,       // resultEntity = the parent of `entity` (0 for a root)
        SetParent = 12,       // moves `entity` under argumentEntity (0 = scene root) at sibling `argument` (-1 = last)
        GetChildren = 13,     // copies child ids into `entities` (up to entityCapacity); result = child count
    };

    struct NativeUIEventRecord
    {
        std::int32_t type = 0; // scene::UIEventType
        std::uint32_t entity = 0;
    };

    struct NativeComponentRequest
    {
        std::int32_t operation = 0;
        std::uint32_t entity = 0;
        const char *typeName = nullptr;
        const char *property = nullptr;
        const char *value = nullptr;
        std::int32_t result = 0;
        std::uint32_t resultEntity = 0;
        const char *resultText = nullptr; // valid until the next request on this thread
        NativeUIEventRecord *events = nullptr;
        std::int32_t eventCapacity = 0;
        std::uint64_t sequence = 0;
        std::uint32_t *entities = nullptr;
        std::int32_t entityCapacity = 0;
        std::uint32_t argumentEntity = 0;
        std::int32_t argument = 0;
    };
    static_assert(sizeof(NativeComponentRequest) == 96, "Keep in step with ScriptBridge.Components.cs");
    static_assert(sizeof(NativeUIEventRecord) == 8, "Keep in step with ScriptBridge.Components.cs");

    void PLUTO_COMPONENT_API_CALL ControlComponents(NativeComponentRequest *request);
}
