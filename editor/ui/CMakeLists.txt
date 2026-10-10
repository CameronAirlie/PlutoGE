add_library(PlutoGEEditorUI
    src/BuiltinAuthoring.cpp
    src/AuthoringUI.cpp
    src/ScriptSourceWatch.cpp
    src/RmlSourceTools.cpp
    src/RmlVisualAuthoringUI.cpp
    src/EntityTransformEditing.cpp
    src/MultiEntityEdit.cpp
    src/IKInspector.cpp
    src/RmlDocumentEditSession.cpp
    src/SourceTextEditor.cpp
    src/RmlDocumentPreview.cpp
    src/panels/RmlDocumentEditorPanel.cpp
    src/EditorShell.cpp
    src/EditorModelInstances.cpp
    src/ModelInstanceRefresh.cpp
    src/EditorAssetImports.cpp
    src/SceneRecovery.cpp
    src/SceneRecoveryUI.cpp
    src/ProjectValidationUI.cpp
    src/DebugDrawUI.cpp
    src/PlayModeChanges.cpp
    src/PlayModeChangesUI.cpp
    src/ViewportOverlayLayout.cpp
    src/ViewportBookmarks.cpp
    src/ViewportBookmarksUI.cpp
    src/StaticModelHierarchyPlacement.cpp
    src/StaticModelHierarchyDialog.cpp
    src/ModelHierarchyView.cpp
    src/ModelImportDependenciesInspector.cpp
    src/ModelHierarchyInspector.cpp
    src/SurfacePlacement.cpp
    src/GroundPlacement.cpp
    src/GroundPlacementUI.cpp
    src/AssetReferenceSearchPanel.cpp
    src/EditorProfiler.cpp
    src/EditorCompositor.cpp
    src/EditorViewportOverlay.cpp
    src/EditorLoadingPresenter.cpp
    src/AssetReferencePicker.cpp
    src/EditorSceneRenderService.cpp
    src/panels/Panel.cpp
    src/panels/ConsolePanel.cpp
    src/panels/ContentBrowserPanel.cpp
    src/panels/AnimationGraphEditorPanel.cpp
    src/panels/AnimationClipEditorPanel.cpp
    src/panels/SequencerEditorPanel.cpp
    src/panels/MaterialEditorPanel.cpp
    src/panels/MeshEditorPanel.cpp
    src/panels/ParticleSystemEditorPanel.cpp
    src/panels/InputMappingEditorPanel.cpp
    src/panels/LoadingScreenEditorPanel.cpp
    src/panels/ShaderGraphEditorPanel.cpp
    src/panels/ProfilerTimeline.cpp
    src/panels/ProfilerPanel.cpp
    src/panels/ViewportPanel.cpp
    src/panels/CanvasEditorPanel.cpp
    src/panels/SceneHierarchyPanel.cpp
    src/panels/InspectorPanel.cpp
    src/PanelManager.cpp
)
add_library(PlutoGE::EditorUI ALIAS PlutoGEEditorUI)
if (WIN32)
    target_link_libraries(PlutoGEEditorUI PRIVATE psapi)
endif()

target_include_directories(PlutoGEEditorUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (NOT WIN32)
    target_compile_options(PlutoGEEditorUI PRIVATE
        -include PlutoGE/ui/CrtCompatibility.h
    )
endif()

target_link_libraries(PlutoGEEditorUI PUBLIC
    PlutoGE::AssetImport
    PlutoGE::Core
    PlutoGE::Scene
    PlutoGE::glm
    PlutoGE::imgui
    PlutoGE::imguizmo
    PlutoGE::glfw
)

get_target_property(PLUTO_EDITOR_RMLUI_TYPE RmlUi::Core TYPE)
if (PLUTO_EDITOR_RMLUI_TYPE STREQUAL "STATIC_LIBRARY")
    set_source_files_properties(src/RmlDocumentPreview.cpp PROPERTIES COMPILE_DEFINITIONS RMLUI_STATIC_LIB)
endif()

target_compile_definitions(PlutoGEEditorUI PRIVATE
    PLUTO_RHI_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
