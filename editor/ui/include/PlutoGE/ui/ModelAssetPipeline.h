#pragma once

#include <string>

namespace PlutoGE::assets { class Project; }

namespace PlutoGE::ui
{
    bool ImportModelAssetThroughPipeline(assets::Project &project,
                                         const std::string &sourceReference,
                                         std::string *errorMessage = nullptr);
}
