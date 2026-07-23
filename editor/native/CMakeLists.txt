find_package(Threads REQUIRED)

add_library(PlutoGEEditorNative SHARED
    src/EditorNative.cpp
)

target_compile_definitions(PlutoGEEditorNative PRIVATE PLUTO_EDITOR_NATIVE_BUILD)
target_include_directories(PlutoGEEditorNative PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(PlutoGEEditorNative PRIVATE
    PlutoGE::engine
    PlutoGE::imgui
    PlutoGE::imguizmo
    PlutoGE::glad
    PlutoGE::glm
    Threads::Threads
)

if (WIN32)
    target_link_libraries(PlutoGEEditorNative PRIVATE opengl32 gdi32 user32)
endif()

set_target_properties(PlutoGEEditorNative PROPERTIES
    OUTPUT_NAME "PlutoGE.Editor.Native"
    DEBUG_POSTFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEEditorNative POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL>
            $<TARGET_FILE_DIR:PlutoGEEditorNative>/$<TARGET_FILE_NAME:OpenAL>
    )
endif()

install(TARGETS PlutoGEEditorNative
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)
