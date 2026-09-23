#pragma once
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementText.h>

namespace PlutoGE::render
{
    enum class RmlTextUpdate { NotApplicable, Unchanged, Changed };

    // Preserve the parser for markup, entities and data-binding expressions.
    // Plain text needs no subtree replacement or element-lookup invalidation.
    inline RmlTextUpdate UpdatePlainRmlText(Rml::Element &element, const Rml::String &text)
    {
        if (element.GetDataModel() || text.empty() || text.find_first_of("<>&{}\r\n\t") != Rml::String::npos ||
            element.GetNumChildren() != 1)
            return RmlTextUpdate::NotApplicable;
        auto *node = dynamic_cast<Rml::ElementText *>(element.GetChild(0));
        if (!node) return RmlTextUpdate::NotApplicable;
        if (node->GetText() == text) return RmlTextUpdate::Unchanged;
        node->SetText(text);
        return RmlTextUpdate::Changed;
    }
}
