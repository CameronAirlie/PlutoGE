pluto_add_engine_module(PlutoGEScripting PlutoGE::Scripting PLUTO_SCRIPTING_TARGET
    src/HostFxrScriptRuntime.cpp
    src/ScriptEngine.cpp
)

target_include_directories(${PLUTO_SCRIPTING_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(${PLUTO_SCRIPTING_TARGET} PUBLIC
        PlutoGE::glm
        PlutoGE::Core
        PlutoGE::Scene
    )
endif()
