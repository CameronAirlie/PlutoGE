if (PLUTO_BUILD_ENGINE_SHARED)
    add_library(PlutoGE SHARED
        src/PlutoGE.cpp
    )
    set_target_properties(PlutoGE PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        DEBUG_POSTFIX ""
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    target_compile_definitions(PlutoGE PUBLIC
        PLUTOGE_SHARED_LIBRARY=1
    )
else()
    add_library(PlutoGE INTERFACE)
endif()

add_library(PlutoGE::engine ALIAS PlutoGE)

# Register an engine module either as its usual standalone static library or,
# in shared mode, as sources and an alias of the single PlutoGE library.
function(pluto_add_engine_module modular_target alias_name output_variable)
    set(module_sources)
    foreach(source_file IN LISTS ARGN)
        if (IS_ABSOLUTE "${source_file}")
            list(APPEND module_sources "${source_file}")
        else()
            list(APPEND module_sources "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
        endif()
    endforeach()

    if (PLUTO_BUILD_ENGINE_SHARED)
        target_sources(PlutoGE PRIVATE ${module_sources})
        add_library(${alias_name} ALIAS PlutoGE)
        set(${output_variable} PlutoGE PARENT_SCOPE)
    else()
        add_library(${modular_target} ${module_sources})
        add_library(${alias_name} ALIAS ${modular_target})
        set(${output_variable} ${modular_target} PARENT_SCOPE)
    endif()
endfunction()

add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(audio)
add_subdirectory(render)
add_subdirectory(assets)
add_subdirectory(import)
add_subdirectory(scripting)
add_subdirectory(scene)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(PlutoGE
        INTERFACE
        PlutoGE::Core
        PlutoGE::Platform
        PlutoGE::Audio
        PlutoGE::Render
        PlutoGE::Assets
        PlutoGE::Import
        PlutoGE::Scripting
        PlutoGE::Scene
    )
else()
    install(TARGETS PlutoGE
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )

    # Shared mode has one link unit, so all non-engine dependencies belong to
    # the parent PlutoGE target. Keeping this here also supports CMake versions
    # predating cross-directory target_link_libraries calls.
    target_link_libraries(PlutoGE PUBLIC
        PlutoGE::glfw
        PlutoGE::glad
        PlutoGE::glm
        PlutoGE::openal_soft
        PlutoGE::stb_image
        PlutoGE::tinygltf
        PlutoGE::assimp
        PlutoGE::meshoptimizer
        PlutoGE::bullet
    )

    if (WIN32)
        target_link_libraries(PlutoGE PUBLIC xaudio2)
    endif()
endif()
