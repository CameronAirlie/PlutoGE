add_library(PlutoGEImport
    src/MeshImporter.cpp
)

add_library(PlutoGE::Import ALIAS PlutoGEImport)

target_include_directories(PlutoGEImport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../render/include
)

target_link_libraries(PlutoGEImport PUBLIC
    PlutoGE::glad
    PlutoGE::glm
    PlutoGE::tinygltf
)