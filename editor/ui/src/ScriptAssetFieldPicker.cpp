#include "PlutoGE/ui/ScriptAssetFieldPicker.h"

#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/ListEditor.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scripting/ScriptEngine.h"

#include <algorithm>
#include <fstream>
#include <string_view>

namespace PlutoGE::ui
{
    namespace
    {
        std::string ReadScriptableObjectType(const assets::Project *project, const std::string &reference)
        {
            if (!project)
            {
                return {};
            }
            std::ifstream input(project->ResolveAssetReference(reference));
            std::string header;
            std::getline(input, header);
            constexpr std::string_view prefix = "SCRIPTABLE\t";
            return header.rfind(prefix, 0) == 0 ? header.substr(prefix.size()) : std::string{};
        }

        bool IsAssignableScriptableObject(const assets::Project *project, const std::string &requiredTypeName,
                                          const AssetReferenceOption &option)
        {
            if (requiredTypeName.empty())
            {
                return true;
            }
            const auto actualType = ReadScriptableObjectType(project, option.reference);
            if (actualType == requiredTypeName)
            {
                return true;
            }
            const auto *actualDefinition = core::Engine::GetInstance().GetScriptEngine().FindClass(actualType);
            return actualDefinition && std::find(actualDefinition->assignableTypeNames.begin(),
                                                 actualDefinition->assignableTypeNames.end(),
                                                 requiredTypeName) != actualDefinition->assignableTypeNames.end();
        }
    }

    bool RenderScriptAssetField(const char *label, const assets::Project *project,
                                const scripting::ScriptFieldDefinition &field, std::string &reference)
    {
        const auto assetType = scripting::GetFieldAssetType(field);
        if (!assetType)
        {
            return false;
        }

        ProjectAssetPickerOptions options{.noneLabel = "<None>", .includeEngineAssets = true};
        if (*assetType == assets::ProjectAssetType::ScriptableObject)
        {
            options.filter = [project, &field](const AssetReferenceOption &option)
            { return IsAssignableScriptableObject(project, field.referenceTypeName, option); };
        }
        return RenderProjectAssetPicker(label, project, *assetType, reference, options);
    }

    bool RenderScriptAssetArrayField(const char *label, const assets::Project *project,
                                     const scripting::ScriptFieldDefinition &field, scripting::ScriptReferenceArray &references)
    {
        return RenderVectorEditor<std::string>(label, references, [&](std::size_t, std::string &reference)
        { return RenderScriptAssetField("##element", project, field, reference); });
    }
}
