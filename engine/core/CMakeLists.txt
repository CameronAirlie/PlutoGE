pluto_add_engine_module(PlutoGECore PlutoGE::Core PLUTO_CORE_TARGET
    src/Engine.cpp
)

target_include_directories(${PLUTO_CORE_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(${PLUTO_CORE_TARGET} PUBLIC
        PlutoGE::Audio
        PlutoGE::Platform
        PlutoGE::Render
        PlutoGE::Assets
        PlutoGE::Import
        PlutoGE::Scripting
    )
endif()
