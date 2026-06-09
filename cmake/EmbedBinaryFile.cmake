if (NOT INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

if (NOT OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

if (NOT SYMBOL_NAME)
    message(FATAL_ERROR "SYMBOL_NAME is required")
endif()

file(READ "${INPUT_FILE}" HEX_BYTES HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTE_LIST "${HEX_BYTES}")

file(WRITE "${OUTPUT_FILE}" "#include <cstddef>\n#include <cstdint>\n\nnamespace PlutoGE::render::nvrhi_shaders\n{\n")
file(APPEND "${OUTPUT_FILE}" "    extern const std::uint8_t ${SYMBOL_NAME}[] = {${BYTE_LIST}};\n")
file(APPEND "${OUTPUT_FILE}" "    extern const std::size_t ${SYMBOL_NAME}Size = sizeof(${SYMBOL_NAME});\n")
file(APPEND "${OUTPUT_FILE}" "}\n")
