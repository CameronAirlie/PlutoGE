if (NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "NormalizeOpenGLShader requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" shader_source)
# Some OpenGL drivers do not propagate Slang's global row-major default into
# matrices nested in uniform structs. Declare it on each block so their layout
# matches the generated row-vector operations and our column-major CPU data.
string(REPLACE "layout(std140) uniform" "layout(std140, row_major) uniform" shader_source "${shader_source}")
# Slang emits Vulkan descriptor-set qualifiers for resources carrying
# [[vk::binding]]. OpenGL has one namespace per resource class, so the source
# uses deliberately flattened register indices and this removes only `set`.
string(REGEX REPLACE "binding[ \t]*=[ \t]*0,[ \t]*set[ \t]*=[ \t]*2" "binding = 16" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*0,[ \t]*set[ \t]*=[ \t]*3" "binding = 17" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*0,[ \t]*set[ \t]*=[ \t]*1" "binding = 8" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*1,[ \t]*set[ \t]*=[ \t]*1" "binding = 9" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*2,[ \t]*set[ \t]*=[ \t]*1" "binding = 10" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*3,[ \t]*set[ \t]*=[ \t]*1" "binding = 11" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*4,[ \t]*set[ \t]*=[ \t]*1" "binding = 12" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*5,[ \t]*set[ \t]*=[ \t]*1" "binding = 13" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*6,[ \t]*set[ \t]*=[ \t]*1" "binding = 14" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*7,[ \t]*set[ \t]*=[ \t]*1" "binding = 15" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*8,[ \t]*set[ \t]*=[ \t]*1" "binding = 16" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*9,[ \t]*set[ \t]*=[ \t]*1" "binding = 17" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*10,[ \t]*set[ \t]*=[ \t]*1" "binding = 18" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*11,[ \t]*set[ \t]*=[ \t]*1" "binding = 19" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*12,[ \t]*set[ \t]*=[ \t]*1" "binding = 20" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*13,[ \t]*set[ \t]*=[ \t]*1" "binding = 21" shader_source "${shader_source}")
string(REGEX REPLACE ",[ \t]*set[ \t]*=[ \t]*0" "" shader_source "${shader_source}")
# VSM's Vulkan descriptors 8/9 share a descriptor namespace with its buffers.
# OpenGL images have a separate namespace with only eight guaranteed units.
# Keep these in sync with the storage-image slots in VirtualShadowMaps.
if (VSM_IMAGE_BINDINGS)
    string(REPLACE "binding = 8)" "binding = 0)" shader_source "${shader_source}")
    string(REPLACE "binding = 9)" "binding = 1)" shader_source "${shader_source}")
endif()
# Slang uses the SPIR-V/Vulkan builtin spelling for SV_VertexID when emitting
# GLSL. Desktop OpenGL exposes the equivalent builtin as gl_VertexID.
string(REPLACE "gl_VertexIndex" "gl_VertexID" shader_source "${shader_source}")
string(REPLACE "gl_InstanceIndex" "gl_InstanceID" shader_source "${shader_source}")
# Slang derives the GLSL image qualifier from the source value type. Some
# resources intentionally store 32-bit shader values into 16-bit float images;
# OpenGL requires the declaration to match the bound image's internal format.
if (DEFINED IMAGE_FORMAT AND NOT IMAGE_FORMAT STREQUAL "")
    string(REPLACE "layout(rgba32f)" "layout(${IMAGE_FORMAT})" shader_source "${shader_source}")
endif()
file(WRITE "${OUTPUT}" "${shader_source}")
