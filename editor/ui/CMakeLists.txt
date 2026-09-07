add_library(PlutoGEEditorUI
    src/MultiEntityEdit.cpp
    src/EditorShell.cpp
    src/SceneRecovery.cpp
    src/SceneRecoveryUI.cpp
    src/ProjectValidationUI.cpp
    src/DebugDrawUI.cpp
    src/PlayModeChanges.cpp
    src/PlayModeChangesUI.cpp
    src/ViewportBookmarks.cpp
    src/ViewportBookmarksUI.cpp
    src/GroundPlacement.cpp
    src/GroundPlacementUI.cpp
    src/AssetReferenceSearchPanel.cpp
    src/EditorProfiler.cpp
    src/EditorCompositor.cpp
    src/EditorSceneRenderService.cpp
    src/panels/Panel.cpp
    src/panels/ConsolePanel.cpp
    src/panels/ContentBrowserPanel.cpp
    src/panels/AnimationGraphEditorPanel.cpp
    src/panels/AnimationClipEditorPanel.cpp
    src/panels/MaterialEditorPanel.cpp
    src/panels/MeshEditorPanel.cpp
    src/panels/ParticleSystemEditorPanel.cpp
    src/panels/InputMappingEditorPanel.cpp
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

target_compile_definitions(PlutoGEEditorUI PRIVATE
    PLUTO_RHI_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
