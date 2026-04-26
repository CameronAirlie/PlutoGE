add_library(PlutoGEAssets
    src/AssetManager.cpp
)

add_library(PlutoGE::Assets ALIAS PlutoGEAssets)

target_link_libraries(PlutoGEAssets PUBLIC
    PlutoGE::Platform
    PlutoGE::Render
    PlutoGE::glm
)

target_include_directories(PlutoGEAssets PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)