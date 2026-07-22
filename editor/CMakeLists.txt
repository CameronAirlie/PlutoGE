add_executable(PlutoGEEditor
    src/main.cpp
)

add_subdirectory(ui)
add_subdirectory(native)

find_program(PLUTO_DOTNET_EXECUTABLE dotnet)
if (PLUTO_DOTNET_EXECUTABLE)
    add_custom_target(PlutoGEAvaloniaEditor
        COMMAND ${PLUTO_DOTNET_EXECUTABLE} build
            ${CMAKE_CURRENT_SOURCE_DIR}/avalonia/PlutoGE.Editor.Avalonia.csproj
            --configuration $<IF:$<CONFIG:Debug>,Debug,Release>
            /p:PlutoNativeBuildDir=$<TARGET_FILE_DIR:PlutoGEEditorNative>
        DEPENDS PlutoGEEditorNative
        USES_TERMINAL
        VERBATIM
    )
endif()

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

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL>
            $<TARGET_FILE_DIR:PlutoGEEditor>/$<TARGET_FILE_NAME:OpenAL>
    )
endif()

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
