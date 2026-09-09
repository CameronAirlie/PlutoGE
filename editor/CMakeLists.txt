add_executable(PlutoGEEditor
    src/main.cpp
)

if (WIN32)
    target_sources(PlutoGEEditor PRIVATE resources/PlutoGEEditor.rc)

    if (PLUTO_EDITOR_WINDOWED)
        set_target_properties(PlutoGEEditor PROPERTIES WIN32_EXECUTABLE TRUE)
        target_compile_definitions(PlutoGEEditor PRIVATE PLUTO_EDITOR_WINDOWED=1)
    endif()
endif()

add_subdirectory(ui)

target_include_directories(PlutoGEEditor
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEEditor PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Platform,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Import,PlutoGE::Scripting>"
    )
else()
    target_link_libraries(PlutoGEEditor PRIVATE
        PlutoGE::Platform PlutoGE::Render PlutoGE::Assets PlutoGE::Core
        PlutoGE::Scene PlutoGE::Import PlutoGE::Scripting
    )
endif()

target_link_libraries(PlutoGEEditor PRIVATE
    PlutoGE::glm
    PlutoGE::imgui
    PlutoGE::EditorUI
)

add_dependencies(PlutoGEEditor PlutoGE::Platform PlutoGE::Render PlutoGE::Assets PlutoGE::Core)
pluto_attach_streamline_runtime(PlutoGEEditor)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL>
            $<TARGET_FILE_DIR:PlutoGEEditor>/$<TARGET_FILE_NAME:OpenAL>
    )
endif()

set(PLUTO_EDITOR_RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
if (EXISTS ${PLUTO_EDITOR_RESOURCES_DIR})
    file(GLOB_RECURSE PLUTO_EDITOR_RESOURCE_FILES CONFIGURE_DEPENDS
        LIST_DIRECTORIES false RELATIVE "${PLUTO_EDITOR_RESOURCES_DIR}"
        "${PLUTO_EDITOR_RESOURCES_DIR}/*")
    set(PLUTO_EDITOR_RESOURCE_OUTPUTS)
    set(PLUTO_EDITOR_RESOURCE_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
    if (CMAKE_CONFIGURATION_TYPES)
        string(APPEND PLUTO_EDITOR_RESOURCE_DESTINATION "/$<CONFIG>")
    endif()
    foreach(resource IN LISTS PLUTO_EDITOR_RESOURCE_FILES)
        # Match the executable layout without depending on the executable itself.
        set(resource_output "${PLUTO_EDITOR_RESOURCE_DESTINATION}/resources/${resource}")
        get_filename_component(resource_directory "${resource_output}" DIRECTORY)
        add_custom_command(OUTPUT "${resource_output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${resource_directory}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLUTO_EDITOR_RESOURCES_DIR}/${resource}" "${resource_output}"
            DEPENDS "${PLUTO_EDITOR_RESOURCES_DIR}/${resource}"
            VERBATIM)
        list(APPEND PLUTO_EDITOR_RESOURCE_OUTPUTS "${resource_output}")
    endforeach()
    add_custom_target(PlutoGEEditorResources DEPENDS ${PLUTO_EDITOR_RESOURCE_OUTPUTS})
    add_dependencies(PlutoGEEditor PlutoGEEditorResources)

    install(DIRECTORY ${PLUTO_EDITOR_RESOURCES_DIR}/
        DESTINATION resources
        COMPONENT Runtime
        PATTERN "*.rc" EXCLUDE
    )
endif()

install(TARGETS PlutoGEEditor
    RUNTIME DESTINATION .
    COMPONENT Runtime
)
