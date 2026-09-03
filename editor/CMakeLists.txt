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
    add_custom_command(TARGET PlutoGEEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PLUTO_EDITOR_RESOURCES_DIR}
            $<TARGET_FILE_DIR:PlutoGEEditor>/resources
    )

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
