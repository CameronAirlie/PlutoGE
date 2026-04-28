add_executable(PlutoGEEditor
    src/main.cpp
)

add_subdirectory(ui)

target_include_directories(PlutoGEEditor
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(PlutoGEEditor
    PRIVATE 
        PlutoGE::Platform
        PlutoGE::Render
        PlutoGE::Assets
        PlutoGE::Core
        PlutoGE::Scene
        PlutoGE::glm
        PlutoGE::imgui
        PlutoGE::EditorUI
)

add_dependencies(PlutoGEEditor PlutoGE::Platform PlutoGE::Render PlutoGE::Assets PlutoGE::Core)