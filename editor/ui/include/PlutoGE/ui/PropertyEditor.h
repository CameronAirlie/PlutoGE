#pragma once

#include "PlutoGE/scene/components/Component.h"

namespace PlutoGE::ui
{
    // The generic editor control for one serialized property, chosen by its
    // type (drag fields, colour picker, enum combo, entity picker...). Edits
    // property.value and returns true when it changed.
    bool RenderPropertyEditor(scene::Property &property);
}
