pluto_add_engine_module(PlutoGEAssets PlutoGE::Assets PLUTO_ASSETS_TARGET
    src/AssetDatabase.cpp
    src/ModelAsset.cpp
    src/AssetManager.cpp
    src/ParticleSystemAsset.cpp
    src/PostProcessPresetAsset.cpp
    src/Project.cpp
)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(${PLUTO_ASSETS_TARGET} PUBLIC
        PlutoGE::Platform
        PlutoGE::Render
        PlutoGE::glm
    )
endif()

target_include_directories(${PLUTO_ASSETS_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
