add_executable(PlutoGEEditorHost
    src/main.cpp
    src/EditorSession.cpp
    src/EditorViewportInteraction.cpp
)

target_link_libraries(PlutoGEEditorHost PRIVATE
    PlutoGE::engine
    PlutoGE::EditorUI
    PlutoGE::glm
)

if (WIN32)
    target_link_libraries(PlutoGEEditorHost PRIVATE user32 opengl32 dbghelp)
endif()

install(TARGETS PlutoGEEditorHost
    RUNTIME DESTINATION bin
)
