add_library(PlutoGEScripting
    src/HostFxrScriptRuntime.cpp
    src/ScriptEngine.cpp
)

add_library(PlutoGE::Scripting ALIAS PlutoGEScripting)

target_include_directories(PlutoGEScripting PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGEScripting PUBLIC
    PlutoGE::glm
    PlutoGE::Core
    PlutoGE::Scene
)