add_executable(PlutoGEEditorHost
    src/main.cpp
    src/EditorSession.cpp
    src/EditorViewportInteraction.cpp
    src/SharedTexturePublisher.cpp
)

target_link_libraries(PlutoGEEditorHost PRIVATE
    PlutoGE::engine
    PlutoGE::EditorUI
    PlutoGE::glm
)

if (WIN32)
    target_link_libraries(PlutoGEEditorHost PRIVATE user32 opengl32 dbghelp d3d11 dxgi)
endif()

install(TARGETS PlutoGEEditorHost
    RUNTIME DESTINATION bin
)
