# Keep the pinned AMD checkout pristine. Build a reproducible local overlay
# with linear-depth and constant-footprint reconstruction for orthographic views.
set(_pluto_fsr2_original_api "${_pluto_fsr2_api_dir}")
set(_pluto_fsr2_api_dir "${CMAKE_CURRENT_BINARY_DIR}/fsr2-api")
file(GLOB_RECURSE _pluto_fsr2_files RELATIVE "${_pluto_fsr2_original_api}"
    "${_pluto_fsr2_original_api}/*.h" "${_pluto_fsr2_original_api}/*.cpp"
    "${_pluto_fsr2_original_api}/*.glsl")
foreach(relative IN LISTS _pluto_fsr2_files)
    if(NOT relative STREQUAL "ffx_fsr2.h" AND NOT relative STREQUAL "ffx_fsr2.cpp" AND
       NOT relative STREQUAL "shaders/ffx_fsr2_common.h")
        configure_file("${_pluto_fsr2_original_api}/${relative}" "${_pluto_fsr2_api_dir}/${relative}" COPYONLY)
    endif()
endforeach()
function(pluto_fsr2_replace old new)
    string(FIND "${source}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pinned FSR2 orthographic patch no longer matches: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" source "${source}")
    set(source "${source}" PARENT_SCOPE)
endfunction()
file(READ "${_pluto_fsr2_original_api}/ffx_fsr2.h" source)
pluto_fsr2_replace("    float                       viewSpaceToMetersFactor;"
    "    float                       orthographicViewWidth;  // PlutoGE: zero for perspective.
    float                       orthographicViewHeight;
    float                       viewSpaceToMetersFactor;")
file(CONFIGURE OUTPUT "${_pluto_fsr2_api_dir}/ffx_fsr2.h" CONTENT "${source}" @ONLY)
file(READ "${_pluto_fsr2_original_api}/ffx_fsr2.cpp" source)
pluto_fsr2_replace("    const float fQ = fMax / (fMin - fMax);" [=[
    if (params->orthographicViewHeight > 0.0f) {
        // Linear device-depth mapping, including reversed Z. Negative Y extent
        // identifies this mode without changing the GPU constant-buffer layout.
        context->constants.deviceToViewDepth[0] = fMin;
        context->constants.deviceToViewDepth[1] = fMax - fMin;
        context->constants.deviceToViewDepth[2] = params->orthographicViewWidth * 0.5f;
        context->constants.deviceToViewDepth[3] = -params->orthographicViewHeight * 0.5f;
        return;
    }
    const float fQ = fMax / (fMin - fMax);
]=])
file(CONFIGURE OUTPUT "${_pluto_fsr2_api_dir}/ffx_fsr2.cpp" CONTENT "${source}" @ONLY)
file(READ "${_pluto_fsr2_original_api}/shaders/ffx_fsr2_common.h" source)
pluto_fsr2_replace("    return (fDeviceToViewDepth[1] / (fDeviceDepth - fDeviceToViewDepth[0]));" [=[
    if (fDeviceToViewDepth[3] < 0.0)
        return fDeviceToViewDepth[0] + fDeviceDepth * fDeviceToViewDepth[1];
    return (fDeviceToViewDepth[1] / (fDeviceDepth - fDeviceToViewDepth[0]));
]=])
pluto_fsr2_replace("    const FfxFloat32 X = fDeviceToViewDepth[2] * fNdcPos.x * Z;"
    "    const FfxFloat32 scale = fDeviceToViewDepth[3] < 0.0 ? 1.0 : Z;
    const FfxFloat32 X = fDeviceToViewDepth[2] * fNdcPos.x * scale;")
pluto_fsr2_replace("    const FfxFloat32 Y = fDeviceToViewDepth[3] * fNdcPos.y * Z;"
    "    const FfxFloat32 Y = abs(fDeviceToViewDepth[3]) * fNdcPos.y * scale;")
file(CONFIGURE OUTPUT "${_pluto_fsr2_api_dir}/shaders/ffx_fsr2_common.h" CONTENT "${source}" @ONLY)
