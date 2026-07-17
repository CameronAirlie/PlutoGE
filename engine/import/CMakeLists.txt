pluto_add_engine_module(PlutoGEImport PlutoGE::Import PLUTO_IMPORT_TARGET
    src/MeshImporter.cpp
)

target_include_directories(${PLUTO_IMPORT_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../render/include
)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(${PLUTO_IMPORT_TARGET} PUBLIC
        PlutoGE::glad
        PlutoGE::glm
        PlutoGE::tinygltf
        PlutoGE::assimp
        PlutoGE::meshoptimizer
    )
endif()
