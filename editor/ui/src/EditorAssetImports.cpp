#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include "PlutoGE/asset_import/ModelImportTask.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/asset_import/ModelImportPublication.h"
#include <algorithm>
#include <chrono>
#include <exception>

namespace PlutoGE::ui
{
    bool EditorShell::BeginModelImport(std::string sourceReference,
                                      std::optional<assetimport::MeshImportOptions> options,
                                      std::string *errorMessage, bool forceReimport)
    {
        if (m_engine.IsRuntimeRunning())
        {
            if (errorMessage) *errorMessage = "Stop Play before importing a model.";
            return false;
        }
        if (!m_project)
        {
            if (errorMessage) *errorMessage = "Open a project before importing a model.";
            return false;
        }
        if (m_importWatchFuture.valid() || m_assetReconciliation.valid() || m_assetReconciliationRequested || m_assetRefreshPending)
        {
            if (errorMessage) *errorMessage = "Wait for asset state reconciliation or cancel it before importing.";
            return false;
        }
        if (!m_modelImportTask) m_modelImportTask = std::make_unique<assetimport::ModelImportTask>();
        if (m_modelImportTask->GetState() != assetimport::ModelImportTaskState::Ready)
        {
            if (errorMessage) *errorMessage = "Finish or cancel the active model import first.";
            return false;
        }
        const auto queuedSource = sourceReference;
        assetimport::MeshImportOptions effective;
        if (!assetimport::ModelImportService{}.ReadOptions(*m_project, sourceReference, effective, errorMessage)) return false;
        assets::AssetMetadata sourceMetadata;
        const auto metadataStatus = assets::LoadAssetMetadata(assets::GetAssetMetadataPath(m_project->ResolveAssetReference(sourceReference)), sourceMetadata);
        m_activeModelAssetSnapshot = scene::CaptureModelAssetSnapshot(m_engine.GetAssetManager(),
            metadataStatus == assets::AssetMetadataStatus::Success ? sourceMetadata.id : std::string{});
        assetimport::ModelImportRequest request{.sourceReference=std::move(sourceReference), .options=options, .forceReimport=forceReimport};
        if (!m_modelImportTask->Start(*m_project, std::move(request), errorMessage)) return false;
        m_activeModelImportEpoch = m_assetContextEpoch;
        std::erase(m_pendingModelImports, queuedSource);
        m_modelImportErrors.erase(queuedSource);
        m_lastModelImportError.clear();
        return true;
    }

    bool EditorShell::ApplyReviewedModelNodeRepair(const assetimport::ModelNodeRepairProposal &proposal,
        std::string *errorMessage)
    {
        if (!m_project || m_engine.IsRuntimeRunning() || m_sceneEditInProgress || m_activeBakeTask || IsModelImportRunning())
        {
            if (errorMessage) *errorMessage = "Finish the active edit/import or stop Play before repairing model correspondence.";
            return false;
        }
        if (!assetimport::ModelNodeRepairService{}.Apply(*m_project, proposal, errorMessage)) return false;
        // Settings are authored project state; scene history must not undo the
        // import or discard a previously accepted generation. Queue work only
        // after the source metadata transaction has released its writer lock.
        std::string importError;
        if (!BeginModelImport(proposal.sourceReference, std::nullopt, &importError))
        {
            if (std::find(m_pendingModelImports.begin(), m_pendingModelImports.end(), proposal.sourceReference) == m_pendingModelImports.end())
                m_pendingModelImports.push_back(proposal.sourceReference);
            Log(ConsoleSeverity::Warning, "Reviewed node repair saved; reimport queued: " + importError);
        }
        else Log(ConsoleSeverity::Info, "Reviewed node repair saved; reimport started.");
        if (errorMessage) errorMessage->clear();
        return true;
    }

    bool EditorShell::IsModelImportRunning() const
    {
        return m_importWatchFuture.valid() || m_assetReconciliation.valid() || m_assetReconciliationRequested || !m_pendingModelImports.empty() ||
               (m_modelImportTask && m_modelImportTask->GetState() != assetimport::ModelImportTaskState::Ready);
    }

    std::string EditorShell::GetModelImportProgress() const
    {
        if (m_assetReconciliation.valid() || m_assetReconciliationRequested) return "Checking source asset state";
        if (m_importWatchFuture.valid()) return "Checking source file changes";
        if (m_modelImportTask && m_modelImportTask->GetState() != assetimport::ModelImportTaskState::Ready) return m_modelImportTask->GetProgress();
        return m_pendingModelImports.empty() ? std::string{} : "Queued model imports: " + std::to_string(m_pendingModelImports.size());
    }

    void EditorShell::CancelModelImport()
    {
        if (m_modelImportTask) m_modelImportTask->Cancel();
        m_assetReconciliationStop.request_stop();
        m_importWatchStop.request_stop();
        m_importWatchDebouncer.Reset();
        // A user cancellation establishes a fresh baseline without retrying
        // the same request. Project context changes explicitly re-arm the audit.
        m_importWatchPrimed = true;
        m_nextImportWatch = {};
        m_importWatchError.clear();
        m_pendingModelImports.clear();
        m_assetReconciliationRequested = false;
        m_reconciliationChangedPaths.clear();
        m_assetRefreshPending = false;
    }

    bool EditorShell::PublishModelImportResult(const std::string &sourceReference, const assetimport::ModelImportResult &result,
                                               const scene::ModelAssetSnapshot &previous, std::string *errorMessage)
    {
        auto catalog = result.catalog;
        auto storage = result.storage;
        assetimport::PreparedModelImportPublication publication;
        if (m_project && m_project->GetManifest().assetPipelineVersion >= 5)
        {
            std::string error;
            if (!assetimport::PrepareModelImportPublication(*m_project, sourceReference, result, publication, &error))
            {
                if (errorMessage) *errorMessage = error;
                Log(ConsoleSeverity::Warning, "Deferred model publication: " + error);
                m_assetRefreshPending = true;
                return false;
            }
            catalog = publication.catalog;
            storage = publication.storage;
        }
        auto &manager = m_engine.GetAssetManager();
        manager.SetAssetSnapshot(catalog, storage);
        m_modelInstancesNeedRefresh = true;
        manager.RefreshImportedAssets(result.changedAssets);
        if (auto *activeScene = m_engine.GetScene())
        {
            const auto report = activeScene->ApplyModelAssetGeneration(result.sourceAssetId, sourceReference, manager, previous);
            for (const auto &diagnostic : report.diagnostics) Log(ConsoleSeverity::Error, diagnostic);
        }
        if (m_project) m_project->RefreshAssetRegistry();
        ClearCachedMaterialPreviews();
        MarkProjectDirty();
        if (errorMessage) errorMessage->clear();
        return true;
    }

    void EditorShell::PollModelImport()
    {
        if (m_engine.IsRuntimeRunning() || m_sceneEditInProgress || m_activeBakeTask) return;
        if (m_modelImportTask)
        {
            auto completion = m_modelImportTask->TakeCompletion();
            if (completion && m_project && m_project->GetManifestPath() == completion->projectPath &&
                m_activeModelImportEpoch == m_assetContextEpoch)
            {
                m_lastModelImportError = completion->error;
                m_modelImportErrors[completion->sourceReference] = completion->error;
                if (!completion->succeeded)
                    Log(ConsoleSeverity::Error, "Model import failed: " + completion->sourceReference + ": " + completion->error);
                else
                {
                    if (PublishModelImportResult(completion->sourceReference, completion->result, m_activeModelAssetSnapshot, &m_lastModelImportError))
                        Log(ConsoleSeverity::Info, "Imported model: " + completion->sourceReference);
                    else m_modelImportErrors[completion->sourceReference] = m_lastModelImportError;
                }
            }
            if (m_modelImportTask->GetState() != assetimport::ModelImportTaskState::Ready) return;
        }
        if (m_assetReconciliation.valid())
        {
            if (m_assetReconciliation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            try
            {
                auto completion = m_assetReconciliation.get();
                if (m_project && completion.contextEpoch == m_assetContextEpoch && completion.projectPath == m_project->GetManifestPath() &&
                    !completion.cancelled && !m_assetReconciliationStop.stop_requested())
                {
                    if (!completion.succeeded && !completion.cancelled)
                        Log(ConsoleSeverity::Error, "Cannot reconcile model imports: " + completion.error);
                    else if (completion.succeeded)
                        for (const auto &assessment : completion.assessments)
                        {
                            using Status = assetimport::ImportReconciliationStatus;
                            if (assessment.status == Status::Current) continue;
                            if (assessment.status == Status::NeedsImport && assessment.automaticImportSafe)
                            {
                                if (std::find(m_pendingModelImports.begin(), m_pendingModelImports.end(), assessment.sourceReference) == m_pendingModelImports.end())
                                    m_pendingModelImports.push_back(assessment.sourceReference);
                            }
                            else
                                Log(assessment.status == Status::Blocked ? ConsoleSeverity::Error : ConsoleSeverity::Info,
                                    assessment.sourceReference + ": " + assessment.reason + " Manual reimport or migration review is required.");
                        }
                }
            }
            catch (const std::exception &error) { Log(ConsoleSeverity::Error, "Asset reconciliation failed: " + std::string(error.what())); }
        }
        // Consume a watch completion before refresh or manual import acquires
        // the project lock; context epochs discard old project snapshots.
        if (m_importWatchFuture.valid())
        {
            PollImportWatch();
            if (m_importWatchFuture.valid()) return;
        }
        if (m_assetRefreshPending)
        {
            m_assetRefreshPending = false;
            m_assetReconciliationRequested = RefreshProjectAssets() && m_project && m_project->GetManifest().assetPipelineVersion >= 2;
        }
        if (!m_project) return;
        // Asset publication changes mesh bindings; schedule it only in edit mode.
        if (m_engine.IsRuntimeRunning()) return;
        if (m_assetReconciliationRequested)
        {
            m_assetReconciliationRequested = false;
            if (m_project->GetManifest().assetPipelineVersion < 2) return;
            m_assetReconciliationStop = std::stop_source{};
            const auto stop = m_assetReconciliationStop.get_token();
            try
            {
                m_assetReconciliation = std::async(std::launch::async,
                    [project = *m_project, epoch = m_assetContextEpoch, stop, changedPaths = std::move(m_reconciliationChangedPaths)]() mutable
                    {
                        AssetReconciliationCompletion completion{.projectPath=project.GetManifestPath(), .contextEpoch=epoch};
                        assets::ProjectAssetLock lock;
                        if (lock.TryAcquire(project.GetRootDirectory(), &completion.error))
                            completion.succeeded = changedPaths.empty()
                                ? assetimport::ReconcileModelImports(project, completion.assessments, &completion.error, stop)
                                : assetimport::ReconcileChangedModelImports(project, changedPaths, completion.assessments, &completion.error, stop);
                        completion.cancelled = stop.stop_requested();
                        return completion;
                    });
            }
            catch (const std::exception &error) { Log(ConsoleSeverity::Error, "Cannot start asset reconciliation: " + std::string(error.what())); }
            return;
        }
        PollModelInstances();
        if (!m_pendingModelImports.empty())
        {
            auto source = std::move(m_pendingModelImports.front());
            m_pendingModelImports.pop_front();
            std::string error;
            if (!BeginModelImport(source, std::nullopt, &error))
                Log(ConsoleSeverity::Error, "Cannot start queued model import: " + source + ": " + error);
            return;
        }
        PollImportWatch();
    }
    void EditorShell::PollImportWatch()
    {
        using Clock = std::chrono::steady_clock;
        const auto now = Clock::now();
        if (m_importWatchFuture.valid())
        {
            if (m_importWatchFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            try
            {
                auto completion = m_importWatchFuture.get();
                if (m_project && completion.contextEpoch == m_assetContextEpoch && completion.projectPath == m_project->GetManifestPath() &&
                    !completion.cancelled && !m_importWatchStop.stop_requested())
                {
                    if (completion.succeeded)
                    {
                        m_importWatchError.clear();
                        const auto changed = m_importWatchDebouncer.Observe(std::move(completion.snapshot), now);
                        // Audit after the first baseline so an edit between the
                        // startup audit and initial watch capture cannot be missed.
                        if (!m_importWatchPrimed || !changed.empty())
                        {
                            m_reconciliationChangedPaths = m_importWatchPrimed ? changed : std::vector<std::filesystem::path>{};
                            m_assetReconciliationRequested = true;
                        }
                        m_importWatchPrimed = true;
                    }
                    else if (completion.error != m_importWatchError)
                    {
                        m_importWatchError = completion.error;
                        Log(ConsoleSeverity::Error, "Cannot inspect import inputs: " + completion.error);
                    }
                }
            }
            catch (const std::exception &error) { Log(ConsoleSeverity::Error, "Import watching failed: " + std::string(error.what())); }
            m_nextImportWatch = now + std::chrono::seconds(1);
        }
        if (!m_project || m_project->GetManifest().assetPipelineVersion < 2 || m_engine.IsRuntimeRunning() ||
            m_assetRefreshPending || m_assetReconciliationRequested || m_assetReconciliation.valid() || !m_pendingModelImports.empty() || now < m_nextImportWatch) return;
        m_nextImportWatch = now + std::chrono::seconds(1);
        m_importWatchStop = std::stop_source{};
        const auto stop = m_importWatchStop.get_token();
        try
        {
            m_importWatchFuture = std::async(std::launch::async,
                [project = *m_project, epoch = m_assetContextEpoch, stop]() mutable
                {
                    ImportWatchCompletion completion{.projectPath=project.GetManifestPath(), .contextEpoch=epoch};
                    assets::ProjectAssetLock lock;
                    if (lock.TryAcquire(project.GetRootDirectory(), &completion.error))
                        completion.succeeded = assetimport::CaptureImportWatchSnapshot(project, completion.snapshot, &completion.error, stop);
                    completion.cancelled = stop.stop_requested();
                    return completion;
                });
        }
        catch (const std::exception &error) { Log(ConsoleSeverity::Error, "Cannot start import watching: " + std::string(error.what())); }
    }

}
