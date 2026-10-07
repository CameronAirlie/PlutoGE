#include "PlutoGE/ui/ViewportOverlayLayout.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace PlutoGE::ui
{
    namespace
    {
        std::map<std::string, ViewportOverlayLayout> layouts;
    }
    ViewportOverlayLayout &GetViewportOverlayLayout(const std::string &name)
    {
        return layouts[name];
    }
    void SaveViewportOverlayLayout()
    {
        ImGui::MarkIniSettingsDirty();
    }
    void RegisterViewportOverlaySettings()
    {
        layouts.clear();
        ImGuiSettingsHandler handler;
        handler.TypeName = "ViewportOverlay";
        handler.TypeHash = ImHashStr(handler.TypeName);
        handler.ClearAllFn = [](ImGuiContext *, ImGuiSettingsHandler *) { layouts.clear(); };
        handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *, const char *name) -> void *
        { return &layouts[name]; };
        handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line)
        {
            float x, y;
            if (std::sscanf(line, "Position=%f,%f", &x, &y) == 2 && std::isfinite(x) && std::isfinite(y))
            {
                auto &layout = *static_cast<ViewportOverlayLayout *>(entry);
                layout.fraction = ImVec2(std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f));
                layout.customized = true;
            }
        };
        handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *, ImGuiTextBuffer *out)
        {
            for (const auto &[name, layout] : layouts)
                if (layout.customized)
                    out->appendf("[ViewportOverlay][%s]\nPosition=%.6f,%.6f\n\n",
                                 name.c_str(), layout.fraction.x, layout.fraction.y);
        };
        ImGui::AddSettingsHandler(&handler);
    }
}
