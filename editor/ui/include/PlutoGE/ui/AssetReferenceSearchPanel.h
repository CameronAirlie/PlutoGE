#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace PlutoGE::ui
{
    class AssetReferenceSearchPanel
    {
    public:
        AssetReferenceSearchPanel();
        ~AssetReferenceSearchPanel();
        void Open(const std::filesystem::path &assetRoot, std::string target);
        void Close();
        void Render(const std::filesystem::path &assetRoot,
                    const std::function<void(const std::string &)> &reveal,
                    const std::function<void(const std::string &)> &open);
    private:
        struct Task;
        std::unique_ptr<Task> m_task;
        std::filesystem::path m_root;
        std::string m_target;
        bool m_open = false;
    };
}
