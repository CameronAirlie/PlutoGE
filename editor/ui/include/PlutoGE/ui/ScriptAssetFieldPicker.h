#pragma once

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scripting/ScriptTypes.h"

#include <string>

namespace PlutoGE::ui
{
    // Dropdown for a managed asset field (scripting::GetFieldAssetType), shared by
    // every editor that edits script fields so all asset types behave the same.
    // ScriptableObject fields only offer assets whose class is assignable to the
    // field's managed type. Returns true when `reference` changed.
    bool RenderScriptAssetField(const char *label, const assets::Project *project,
                                const scripting::ScriptFieldDefinition &field, std::string &reference);

    // An array field (field.isArray): a reorderable list whose rows are RenderScriptAssetField
    // pickers for the element type. Returns true when the array or any element changed.
    bool RenderScriptAssetArrayField(const char *label, const assets::Project *project,
                                     const scripting::ScriptFieldDefinition &field, scripting::ScriptReferenceArray &references);
}
