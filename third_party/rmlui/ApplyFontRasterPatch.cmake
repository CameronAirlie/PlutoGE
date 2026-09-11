# Keep the patch against pinned RmlUi 6.1 reproducible on fresh and existing builds.
find_package(Git REQUIRED)
set(patch "${CMAKE_CURRENT_LIST_DIR}/font-raster-density.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch}"
    WORKING_DIRECTORY "${rmlui_SOURCE_DIR}" RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET)
if (NOT already_applied EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${patch}"
        WORKING_DIRECTORY "${rmlui_SOURCE_DIR}" RESULT_VARIABLE check_result
        ERROR_VARIABLE check_error)
    if (NOT check_result EQUAL 0)
        message(FATAL_ERROR "RmlUi font raster patch no longer matches the dependency: ${check_error}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
        WORKING_DIRECTORY "${rmlui_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
endif()
target_sources(rmlui_core PRIVATE "${CMAKE_CURRENT_LIST_DIR}/PlutoGE_RmlUi_FontRaster.cpp")
target_include_directories(rmlui_core PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
