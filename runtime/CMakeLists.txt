add_executable(PlutoGERuntime
    src/main.cpp
)

if (WIN32)
    target_sources(PlutoGERuntime PRIVATE resources/PlutoGERuntime.rc)

    if (PLUTO_RUNTIME_WINDOWED)
        set_target_properties(PlutoGERuntime PROPERTIES WIN32_EXECUTABLE TRUE)
        target_compile_definitions(PlutoGERuntime PRIVATE PLUTO_RUNTIME_WINDOWED=1)
    endif()
endif()

target_include_directories(PlutoGERuntime
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGERuntime PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Platform,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Import,PlutoGE::Scripting>"
    )
else()
    target_link_libraries(PlutoGERuntime PRIVATE
        PlutoGE::Platform PlutoGE::Render PlutoGE::Assets PlutoGE::Core
        PlutoGE::Scene PlutoGE::Import PlutoGE::Scripting
    )
endif()

target_link_libraries(PlutoGERuntime PRIVATE PlutoGE::glm)

add_dependencies(PlutoGERuntime PlutoGE::Platform PlutoGE::Render PlutoGE::Assets PlutoGE::Core PlutoGE::Scene)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGERuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL>
            $<TARGET_FILE_DIR:PlutoGERuntime>/$<TARGET_FILE_NAME:OpenAL>
    )
endif()

install(TARGETS PlutoGERuntime
    RUNTIME DESTINATION .
    COMPONENT Runtime
)
