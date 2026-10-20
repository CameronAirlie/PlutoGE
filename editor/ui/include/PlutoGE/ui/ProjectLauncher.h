#pragma once

#include <filesystem>
#include <span>
#include <string>

namespace PlutoGE::ui
{
    struct ProjectTemplateOption
    {
        std::string id;
        std::string label;
        std::string description;
        bool available = true;
    };

    // What the user asked for this frame. The shell performs the actions
    // (dialogs, loading, editing the recent list) after the frame presents.
    struct ProjectLauncherActions
    {
        std::filesystem::path open;
        bool browse = false;
        std::string createTemplateId;
        std::filesystem::path forget;
        std::filesystem::path reveal;
        bool quit = false;
    };

    // Full-window start page shown before a project is loaded. Follows the
    // Preferences / Project Settings layout: a navigation sidebar beside
    // titled pages built from cards.
    class ProjectLauncher
    {
    public:
        ProjectLauncherActions Draw(std::span<const std::filesystem::path> recentProjects,
                                    std::span<const ProjectTemplateOption> templates,
                                    const std::string &errorMessage);

    private:
        enum class Page
        {
            Projects,
            NewProject,
        };

        void DrawSidebar(ProjectLauncherActions &actions, float height);
        void DrawProjectsPage(std::span<const std::filesystem::path> recentProjects, ProjectLauncherActions &actions);
        void DrawNewProjectPage(std::span<const ProjectTemplateOption> templates, ProjectLauncherActions &actions);

        Page m_page = Page::Projects;
        std::string m_search;
        std::string m_selectedTemplate;
    };
}
