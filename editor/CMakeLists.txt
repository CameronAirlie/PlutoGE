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

set(PLUTO_EDITOR_RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
if (EXISTS ${PLUTO_EDITOR_RESOURCES_DIR})
    add_custom_command(TARGET PlutoGEEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PLUTO_EDITOR_RESOURCES_DIR}
            $<TARGET_FILE_DIR:PlutoGEEditor>/resources
    )

    install(DIRECTORY ${PLUTO_EDITOR_RESOURCES_DIR}/
        DESTINATION resources
    )
endif()

install(TARGETS PlutoGEEditor
    RUNTIME DESTINATION .
)
