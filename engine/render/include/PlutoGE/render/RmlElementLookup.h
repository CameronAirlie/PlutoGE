#pragma once

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ObserverPtr.h>
#include <string>
#include <unordered_map>

namespace PlutoGE::render
{
    // Non-owning ID lookup. Mutation owners invalidate on structural changes;
    // observer handles additionally prevent dangling pointers after destruction.
    class RmlElementLookup
    {
    public:
        Rml::Element *Find(Rml::ElementDocument *document, const std::string &id)
        {
            if (!document) return nullptr;
            auto &entry = m_documents[document];
            if (entry.document.get() != document)
            {
                entry.elements.clear();
                entry.document = document->GetObserverPtr();
            }
            auto &handle = entry.elements[id];
            if (auto *element = handle.get(); element && element->GetId() == id &&
                element->GetOwnerDocument() == document)
                return element;
            auto *element = document->GetElementById(id);
            handle = element ? element->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>{};
            return element;
        }
        void Invalidate(Rml::ElementDocument *document) { m_documents.erase(document); }
        void Clear() { m_documents.clear(); }
    private:
        struct Entry
        {
            Rml::ObserverPtr<Rml::Element> document;
            std::unordered_map<std::string, Rml::ObserverPtr<Rml::Element>> elements;
        };
        std::unordered_map<Rml::ElementDocument *, Entry> m_documents;
    };
}
