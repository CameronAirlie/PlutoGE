#pragma once
#include "PlutoGE/assets/ModelGenerationRetention.h"

// Compatibility facade: authored snapshot persistence belongs to Assets, not
// import orchestration. Scene and runtime consumers use the Assets API directly.
namespace PlutoGE::assetimport
{
    using assets::RetainModelGeneration;
    using assets::RetainStaticModelInstance;
}
