#pragma once

#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/assets/Project.h"
#include <mutex>
#include <optional>
#include <thread>

namespace PlutoGE::assetimport
{
    enum class ModelImportTaskState { Ready, Running, Completed };

    struct ModelImportCompletion
    {
        std::filesystem::path projectPath;
        std::string sourceReference;
        bool succeeded = false;
        ModelImportResult result;
        std::string error;
    };

    // One CPU import with a copied project/request snapshot. Owner-thread Start,
    // Cancel and TakeCompletion never invoke editor or resource-manager APIs.
    // Progress/state queries are synchronized. Destruction requests cancellation
    // and joins; parsers currently observe cancellation at service stage boundaries.
    class ModelImportTask
    {
    public:
        ~ModelImportTask();
        bool Start(assets::Project project, ModelImportRequest request, std::string *errorMessage = nullptr);
        void Cancel();
        ModelImportTaskState GetState() const;
        std::string GetProgress() const;
        std::optional<ModelImportCompletion> TakeCompletion();
    private:
        mutable std::mutex m_mutex;
        ModelImportTaskState m_state = ModelImportTaskState::Ready;
        std::string m_progress;
        std::optional<ModelImportCompletion> m_completion;
        // Declared last: join before destroying worker-visible state.
        std::jthread m_worker;
    };
}
