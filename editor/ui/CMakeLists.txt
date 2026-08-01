add_library(PlutoGEEditorUI
    src/EditorShell.cpp
    src/EditorProfiler.cpp
    src/panels/Panel.cpp
    src/panels/ConsolePanel.cpp
    src/panels/ContentBrowserPanel.cpp
    src/panels/AnimationGraphEditorPanel.cpp
    src/panels/AnimationClipEditorPanel.cpp
    src/panels/MaterialEditorPanel.cpp
    src/panels/MeshEditorPanel.cpp
    src/panels/ParticleSystemEditorPanel.cpp
    src/panels/ShaderGraphEditorPanel.cpp
    src/panels/ProfilerPanel.cpp
    src/panels/ViewportPanel.cpp
    src/panels/CanvasEditorPanel.cpp
    src/panels/SceneHierarchyPanel.cpp
    src/panels/InspectorPanel.cpp
    src/PanelManager.cpp
)
add_library(PlutoGE::EditorUI ALIAS PlutoGEEditorUI)

target_include_directories(PlutoGEEditorUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (NOT WIN32)
    target_compile_options(PlutoGEEditorUI PRIVATE
        -include PlutoGE/ui/CrtCompatibility.h
    )
endif()

target_link_libraries(PlutoGEEditorUI PUBLIC
    PlutoGE::Core
    PlutoGE::Scene
    PlutoGE::glm
    PlutoGE::imgui
    PlutoGE::imguizmo
    PlutoGE::glfw
)
