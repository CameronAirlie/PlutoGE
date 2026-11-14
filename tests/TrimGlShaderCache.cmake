# Clears the tests' private OpenGL driver shader cache once it nears the driver's size limit.
#
# The NVIDIA driver stops storing new programs when its disk cache is full (1 GB by default) but keeps serving the
# old ones. Every OpenGL test then recompiles each shader that changed since the cache filled, and BasicLit variants
# take 20-30 s apiece uncached, so single test modes take minutes and the suite appears to hang. Starting over costs
# one cold run.
#
# Usage: cmake -DCACHE_DIR=<dir> [-DLIMIT_MB=<n>] -P TrimGlShaderCache.cmake

if (NOT CACHE_DIR)
    message(FATAL_ERROR "CACHE_DIR is required")
endif()
if (NOT LIMIT_MB)
    set(LIMIT_MB 768)
endif()

file(MAKE_DIRECTORY "${CACHE_DIR}")
file(GLOB_RECURSE cache_files LIST_DIRECTORIES false "${CACHE_DIR}/*")
set(total_bytes 0)
foreach (cache_file IN LISTS cache_files)
    file(SIZE "${cache_file}" file_bytes)
    math(EXPR total_bytes "${total_bytes} + ${file_bytes}")
endforeach()
math(EXPR total_mb "${total_bytes} / 1048576")

if (total_mb GREATER_EQUAL LIMIT_MB)
    message(STATUS "OpenGL shader cache ${CACHE_DIR} holds ${total_mb} MB (limit ${LIMIT_MB} MB); clearing it")
    file(REMOVE_RECURSE "${CACHE_DIR}")
    file(MAKE_DIRECTORY "${CACHE_DIR}")
else()
    message(STATUS "OpenGL shader cache ${CACHE_DIR} holds ${total_mb} MB (limit ${LIMIT_MB} MB)")
endif()
