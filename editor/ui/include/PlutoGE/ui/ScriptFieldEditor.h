#pragma once

#include "PlutoGE/scripting/ScriptTypes.h"

#include <cstdint>
#include <functional>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::ui
{
    struct ScriptFieldEditorContext
    {
        const assets::Project *project = nullptr;
        // Draws a field that references a scene object (GameObject or a component). Editors without a scene, such
        // as the scriptable object editor, leave it empty and those fields show as unavailable.
        std::function<bool(const char *label, const scripting::ScriptFieldDefinition &field, uint32_t &entityId)> drawEntityReference;
    };

    // Draws one serialized script field with the control for its type: checkbox, number (a slider with [Range]),
    // text, enum dropdown, colour picker, vectors, quaternion as Euler degrees, asset picker, scene reference or
    // a list for arrays. Shows the [Header] above and the [Tooltip] on hover. `value` must already have the field's
    // shape (see scripting::NormalizeFieldValue). Returns true when the user changed it.
    bool DrawScriptField(const scripting::ScriptFieldDefinition &field, scripting::ScriptFieldValue &value,
                         const ScriptFieldEditorContext &context);
}
