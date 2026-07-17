add_executable(PlutoGEEditorHost
    src/main.cpp
)

target_link_libraries(PlutoGEEditorHost PRIVATE
    PlutoGE::engine
    PlutoGE::glm
)

if (WIN32)
    target_link_libraries(PlutoGEEditorHost PRIVATE user32)
endif()

install(TARGETS PlutoGEEditorHost
    RUNTIME DESTINATION bin
)
