#include "PlutoGE/ui/ViewportToolPreferences.h"

#include "PlutoGE/ui/EditorSettings.h"

#include <algorithm>

namespace PlutoGE::ui
{
    namespace
    {
        float LoadPositive(const EditorSettings &settings, std::string_view key, float fallback, float maximum)
        {
            const float value = settings.GetFloat(key, fallback);
            return value > 0.0f && value <= maximum ? value : fallback;
        }
    }

    ViewportToolPreferences LoadViewportToolPreferences(const EditorSettings &settings, const ViewportToolPreferences &defaults)
    {
        ViewportToolPreferences result = defaults;
        const int tool = settings.GetInt("viewport.tool", static_cast<int>(defaults.tool));
        if (tool >= 0 && tool <= static_cast<int>(ViewportToolPreferences::Tool::Scale))
            result.tool = static_cast<ViewportToolPreferences::Tool>(tool);
        result.localSpace = settings.GetBool("viewport.localSpace", defaults.localSpace);
        result.snap = settings.GetBool("viewport.snap", defaults.snap);
        result.moveSnap[0] = LoadPositive(settings, "viewport.snapMoveX", defaults.moveSnap[0], 100.0f);
        result.moveSnap[1] = LoadPositive(settings, "viewport.snapMoveY", defaults.moveSnap[1], 100.0f);
        result.moveSnap[2] = LoadPositive(settings, "viewport.snapMoveZ", defaults.moveSnap[2], 100.0f);
        result.rotateSnapDegrees = LoadPositive(settings, "viewport.snapRotate", defaults.rotateSnapDegrees, 180.0f);
        result.scaleSnap = LoadPositive(settings, "viewport.snapScale", defaults.scaleSnap, 10.0f);
        result.showGrid = settings.GetBool("viewport.showGrid", defaults.showGrid);
        return result;
    }

    void SaveViewportToolPreferences(EditorSettings &settings, const ViewportToolPreferences &preferences)
    {
        settings.SetInt("viewport.tool", static_cast<int>(preferences.tool));
        settings.SetBool("viewport.localSpace", preferences.localSpace);
        settings.SetBool("viewport.snap", preferences.snap);
        settings.SetFloat("viewport.snapMoveX", preferences.moveSnap[0]);
        settings.SetFloat("viewport.snapMoveY", preferences.moveSnap[1]);
        settings.SetFloat("viewport.snapMoveZ", preferences.moveSnap[2]);
        settings.SetFloat("viewport.snapRotate", preferences.rotateSnapDegrees);
        settings.SetFloat("viewport.snapScale", preferences.scaleSnap);
        settings.SetBool("viewport.showGrid", preferences.showGrid);
    }
}
