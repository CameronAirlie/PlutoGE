if (NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "NormalizeOpenGLShader requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" shader_source)
# Slang emits Vulkan descriptor-set qualifiers for resources carrying
# [[vk::binding]]. OpenGL has one namespace per resource class, so the source
# uses deliberately flattened register indices and this removes only `set`.
string(REGEX REPLACE "binding[ \t]*=[ \t]*0,[ \t]*set[ \t]*=[ \t]*2" "binding = 16" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*0,[ \t]*set[ \t]*=[ \t]*1" "binding = 8" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*1,[ \t]*set[ \t]*=[ \t]*1" "binding = 9" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*2,[ \t]*set[ \t]*=[ \t]*1" "binding = 10" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*3,[ \t]*set[ \t]*=[ \t]*1" "binding = 11" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*4,[ \t]*set[ \t]*=[ \t]*1" "binding = 12" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*5,[ \t]*set[ \t]*=[ \t]*1" "binding = 13" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*6,[ \t]*set[ \t]*=[ \t]*1" "binding = 14" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*7,[ \t]*set[ \t]*=[ \t]*1" "binding = 15" shader_source "${shader_source}")
string(REGEX REPLACE "binding[ \t]*=[ \t]*8,[ \t]*set[ \t]*=[ \t]*1" "binding = 16" shader_source "${shader_source}")
string(REGEX REPLACE ",[ \t]*set[ \t]*=[ \t]*0" "" shader_source "${shader_source}")
# Slang uses the SPIR-V/Vulkan builtin spelling for SV_VertexID when emitting
# GLSL. Desktop OpenGL exposes the equivalent builtin as gl_VertexID.
string(REPLACE "gl_VertexIndex" "gl_VertexID" shader_source "${shader_source}")
file(WRITE "${OUTPUT}" "${shader_source}")
