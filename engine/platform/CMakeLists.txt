pluto_add_engine_module(PlutoGEPlatform PlutoGE::Platform PLUTO_PLATFORM_TARGET
    src/Window.cpp
)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(${PLUTO_PLATFORM_TARGET} PUBLIC
        PlutoGE::glfw
        PlutoGE::glad
    )
endif()

target_include_directories(${PLUTO_PLATFORM_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
