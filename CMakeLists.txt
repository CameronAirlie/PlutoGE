cmake_minimum_required(VERSION 3.10.0)
project(PlutoGE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PLUTO_BUILD_EDITOR "Build the PlutoGE Editor" ON)
option(PLUTO_BUILD_SAMPLES "Build sample applications" ON)

add_subdirectory(engine)
add_subdirectory(third_party)

if (PLUTO_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

# if (PLUTO_BUILD_SAMPLES)
#     add_subdirectory(samples)
# endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
