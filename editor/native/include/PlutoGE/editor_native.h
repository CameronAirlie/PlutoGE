#pragma once

#include <stdint.h>

#if defined(_WIN32)
#if defined(PLUTO_EDITOR_NATIVE_BUILD)
#define PLUTO_EDITOR_API __declspec(dllexport)
#else
#define PLUTO_EDITOR_API __declspec(dllimport)
#endif
#else
#define PLUTO_EDITOR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef uint64_t PlutoEditorHandle;
    typedef void *(*PlutoEditorGLProcAddress)(const char *name, void *user_data);

    enum PlutoEditorResult
    {
        PLUTO_EDITOR_OK = 0,
        PLUTO_EDITOR_INVALID_ARGUMENT = 1,
        PLUTO_EDITOR_INVALID_HANDLE = 2,
        PLUTO_EDITOR_ALREADY_INITIALIZED = 3,
        PLUTO_EDITOR_OPENGL_UNAVAILABLE = 4,
        PLUTO_EDITOR_CONTEXT_NOT_SHARED = 5,
        PLUTO_EDITOR_INTERNAL_ERROR = 6,
    };

    enum PlutoEditorGizmoOperation
    {
        PLUTO_EDITOR_GIZMO_TRANSLATE = 0,
        PLUTO_EDITOR_GIZMO_ROTATE = 1,
        PLUTO_EDITOR_GIZMO_SCALE = 2,
    };

    typedef struct PlutoEditorEngineConfig
    {
        uint32_t struct_size;
        uint32_t api_version;
        int32_t initial_width;
        int32_t initial_height;
        PlutoEditorGLProcAddress get_proc_address;
        void *user_data;
    } PlutoEditorEngineConfig;

    typedef struct PlutoEditorViewportFrame
    {
        uint32_t struct_size;
        int32_t width;
        int32_t height;
        int32_t framebuffer;
        float delta_seconds;
        float target_refresh_hz;
        float mouse_x;
        float mouse_y;
        float mouse_wheel;
        uint8_t mouse_left;
        uint8_t mouse_right;
        uint8_t mouse_middle;
        uint8_t focused;
        float camera_x;
        float camera_y;
        float camera_z;
        float camera_yaw_degrees;
        float camera_pitch_degrees;
        float camera_fov_degrees;
        PlutoEditorGLProcAddress get_proc_address;
        void *user_data;
    } PlutoEditorViewportFrame;

    typedef struct PlutoEditorEntityInfo
    {
        uint32_t id;
        uint32_t parent_id;
        uint8_t active;
        char name[120];
    } PlutoEditorEntityInfo;

    typedef struct PlutoEditorTransform
    {
        float position[3];
        float rotation[3];
        float scale[3];
    } PlutoEditorTransform;

    typedef struct PlutoEditorFrameStats
    {
        uint64_t frame_count;
        double average_frame_ms;
        double maximum_frame_ms;
        double resize_ms;
        int32_t width;
        int32_t height;
        float target_refresh_hz;
    } PlutoEditorFrameStats;

    typedef struct PlutoEditorProjectInfo
    {
        char name[120];
        char manifest_path[512];
        char asset_directory[256];
        char startup_scene[256];
    } PlutoEditorProjectInfo;

    typedef struct PlutoEditorAssetInfo
    {
        int32_t type;
        uint64_t size;
        char reference[512];
    } PlutoEditorAssetInfo;

    typedef struct PlutoEditorSceneInfo
    {
        char path[512];
    } PlutoEditorSceneInfo;

    typedef struct PlutoEditorComponentInfo
    {
        uint32_t index;
        uint8_t enabled;
        char name[120];
    } PlutoEditorComponentInfo;

    typedef struct PlutoEditorComponentProperty
    {
        int32_t type;
        char name[120];
        char value[512];
    } PlutoEditorComponentProperty;

    PLUTO_EDITOR_API uint32_t pluto_editor_api_version(void);
    PLUTO_EDITOR_API int32_t pluto_editor_engine_create(const PlutoEditorEngineConfig *config, PlutoEditorHandle *engine);
    PLUTO_EDITOR_API int32_t pluto_editor_engine_destroy(PlutoEditorHandle engine);
    PLUTO_EDITOR_API int32_t pluto_editor_viewport_create(PlutoEditorHandle engine, PlutoEditorHandle *viewport);
    PLUTO_EDITOR_API int32_t pluto_editor_viewport_destroy(PlutoEditorHandle engine, PlutoEditorHandle viewport);
    PLUTO_EDITOR_API int32_t pluto_editor_viewport_render(PlutoEditorHandle engine, PlutoEditorHandle viewport, const PlutoEditorViewportFrame *frame);
    PLUTO_EDITOR_API int32_t pluto_editor_viewport_set_selected_entity(PlutoEditorHandle engine, PlutoEditorHandle viewport, uint32_t entity_id);
    PLUTO_EDITOR_API int32_t pluto_editor_viewport_pick_entity(PlutoEditorHandle engine, PlutoEditorHandle viewport, float mouse_x, float mouse_y, uint32_t *entity_id);
    PLUTO_EDITOR_API int32_t pluto_editor_viewport_set_gizmo_operation(PlutoEditorHandle engine, PlutoEditorHandle viewport, int32_t operation);
    PLUTO_EDITOR_API int32_t pluto_editor_viewport_get_gizmo_active(PlutoEditorHandle engine, PlutoEditorHandle viewport, uint8_t *active);
    PLUTO_EDITOR_API int32_t pluto_editor_viewport_get_stats(PlutoEditorHandle engine, PlutoEditorHandle viewport, PlutoEditorFrameStats *stats);
    PLUTO_EDITOR_API int32_t pluto_editor_scene_get_entity_count(PlutoEditorHandle engine, uint32_t *count);
    PLUTO_EDITOR_API int32_t pluto_editor_scene_get_entity(PlutoEditorHandle engine, uint32_t index, PlutoEditorEntityInfo *entity);
    PLUTO_EDITOR_API int32_t pluto_editor_entity_get_transform(PlutoEditorHandle engine, uint32_t entity_id, PlutoEditorTransform *transform);
    PLUTO_EDITOR_API int32_t pluto_editor_entity_set_transform(PlutoEditorHandle engine, uint32_t entity_id, const PlutoEditorTransform *transform);
    PLUTO_EDITOR_API int32_t pluto_editor_project_load(PlutoEditorHandle engine, const char *manifest_path);
    PLUTO_EDITOR_API int32_t pluto_editor_project_get_info(PlutoEditorHandle engine, PlutoEditorProjectInfo *project);
    PLUTO_EDITOR_API int32_t pluto_editor_project_get_asset_count(PlutoEditorHandle engine, uint32_t *count);
    PLUTO_EDITOR_API int32_t pluto_editor_project_get_asset(PlutoEditorHandle engine, uint32_t index, PlutoEditorAssetInfo *asset);
    PLUTO_EDITOR_API int32_t pluto_editor_scene_load(PlutoEditorHandle engine, const char *path_or_reference);
    PLUTO_EDITOR_API int32_t pluto_editor_scene_get_info(PlutoEditorHandle engine, PlutoEditorSceneInfo *scene);
    PLUTO_EDITOR_API int32_t pluto_editor_entity_get_component_count(PlutoEditorHandle engine, uint32_t entity_id, uint32_t *count);
    PLUTO_EDITOR_API int32_t pluto_editor_entity_get_component(PlutoEditorHandle engine, uint32_t entity_id, uint32_t component_index, PlutoEditorComponentInfo *component);
    PLUTO_EDITOR_API int32_t pluto_editor_component_get_property_count(PlutoEditorHandle engine, uint32_t entity_id, uint32_t component_index, uint32_t *count);
    PLUTO_EDITOR_API int32_t pluto_editor_component_get_property(PlutoEditorHandle engine, uint32_t entity_id, uint32_t component_index, uint32_t property_index, PlutoEditorComponentProperty *property);
    PLUTO_EDITOR_API const char *pluto_editor_get_last_error(void);

#ifdef __cplusplus
}
#endif
