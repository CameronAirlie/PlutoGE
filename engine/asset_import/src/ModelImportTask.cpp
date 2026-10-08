#include "PlutoGE/asset_import/ModelImportTask.h"
#include <stdexcept>
#include <utility>

namespace PlutoGE::assetimport
{
    ModelImportTask::~ModelImportTask() { Cancel(); }

    bool ModelImportTask::Start(assets::Project project, ModelImportRequest request, std::string *errorMessage)
    {
        std::lock_guard lock(m_mutex);
        if (m_state != ModelImportTaskState::Ready)
        {
            if (errorMessage) *errorMessage = "An import is active or its completion has not been consumed.";
            return false;
        }
        m_progress.clear();
        m_state = ModelImportTaskState::Running;
        try
        {
            m_worker = std::jthread([this, project = std::move(project), request = std::move(request)](std::stop_token stop) mutable
            {
                ModelImportCompletion completion{.projectPath=project.GetManifestPath(), .sourceReference=request.sourceReference};
                try
                {
                    const auto externalStop = request.stop;
                    auto observer = std::move(request.progress);
                    request.stop = stop;
                    request.progress = [this, externalStop, observer=std::move(observer)](std::string_view stage)
                    {
                        if (externalStop.stop_requested()) throw std::runtime_error("Model import cancelled.");
                        {
                            std::lock_guard progressLock(m_mutex);
                            m_progress = stage;
                        }
                        if (observer) observer(stage);
                    };
                    completion.succeeded = ModelImportService{}.Import(project, request, completion.result, &completion.error);
                }
                catch (const std::exception &error) { completion.error = error.what(); }
                catch (...) { completion.error = "Unexpected model import failure."; }
                std::lock_guard completionLock(m_mutex);
                m_completion = std::move(completion);
                m_state = ModelImportTaskState::Completed;
            });
        }
        catch (const std::exception &error)
        {
            m_state = ModelImportTaskState::Ready;
            if (errorMessage) *errorMessage = error.what();
            return false;
        }
        if (errorMessage) errorMessage->clear();
        return true;
    }

    void ModelImportTask::Cancel() { m_worker.request_stop(); }

    ModelImportTaskState ModelImportTask::GetState() const
    {
        std::lock_guard lock(m_mutex);
        return m_state;
    }

    std::string ModelImportTask::GetProgress() const
    {
        std::lock_guard lock(m_mutex);
        return m_progress;
    }

    std::optional<ModelImportCompletion> ModelImportTask::TakeCompletion()
    {
        std::lock_guard lock(m_mutex);
        if (m_state != ModelImportTaskState::Completed) return std::nullopt;
        auto completion = std::move(m_completion);
        m_completion.reset();
        m_state = ModelImportTaskState::Ready;
        return completion;
    }
}
