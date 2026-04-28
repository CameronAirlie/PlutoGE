add_library(PlutoGEEditorUI
    src/EditorShell.cpp
    src/panels/Panel.cpp
    src/panels/ViewportPanel.cpp
    src/PanelManager.cpp
)
add_library(PlutoGE::EditorUI ALIAS PlutoGEEditorUI)

target_include_directories(PlutoGEEditorUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGEEditorUI PUBLIC
    PlutoGE::Core
    PlutoGE::Scene
    PlutoGE::glm
    PlutoGE::imgui
    PlutoGE::glfw
)