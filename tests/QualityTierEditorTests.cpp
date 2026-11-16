#include "PlutoGE/ui/quality/QualityTierSession.h"

#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::ui;

namespace
{
    void Check(bool value, const char *message)
    {
        if (!value) throw std::runtime_error(message);
    }

    struct Recorder
    {
        render::GraphicsQualityPresets stored;
        int writes = 0;
        bool failWrites = false;
        std::optional<render::GraphicsQuality> previewed;
        int previews = 0;
        bool previewEnded = false;
    };

    class FakeStorage final : public QualityTierStorage
    {
    public:
        explicit FakeStorage(Recorder &recorder) : m_recorder(recorder) {}
        render::GraphicsQualityPresets Read() const override { return m_recorder.stored; }
        bool Write(const render::GraphicsQualityPresets &presets) override
        {
            if (m_recorder.failWrites) return false;
            m_recorder.stored = presets;
            ++m_recorder.writes;
            return true;
        }
        void Preview(const render::GraphicsQuality *quality) override
        {
            ++m_recorder.previews;
            m_recorder.previewEnded = quality == nullptr;
            m_recorder.previewed = quality ? std::optional(*quality) : std::nullopt;
        }

    private:
        Recorder &m_recorder;
    };

    const QualityTierField &Field(std::string_view id)
    {
        const auto *field = FindQualityTierField(id);
        if (!field) throw std::runtime_error("Missing quality field " + std::string(id));
        return *field;
    }

    void FieldTable()
    {
        std::set<std::string_view> ids;
        render::GraphicsQuality quality = render::GraphicsQuality::FromPreset(render::GraphicsPreset::High);
        for (const auto &field : GetQualityTierFields())
        {
            Check(ids.insert(field.id).second, "Duplicate quality field id");
            Check(!field.label.empty() && !field.description.empty(), "Quality field lacks a label or description");
            // Every field's range is exactly what GraphicsQuality accepts.
            auto edited = quality;
            field.Set(edited, field.min);
            Check(edited.IsValid(), "A field's minimum is not a valid quality");
            field.Set(edited, field.max);
            Check(edited.IsValid(), "A field's maximum is not a valid quality");
            field.Set(edited, field.max * 4.0 + 1.0);
            Check(field.Get(edited) == field.max, "A field did not clamp to its maximum");
        }
        Check(ids.size() == 15, "Quality field table does not cover every GraphicsQuality ceiling");
        auto edited = quality;
        Field("shadowResolution").Set(edited, 2500.0);
        Check(edited.shadowResolution == 2048, "Shadow resolution did not round to a power of two");
        Check(Field("grassDensity").Format(0.4) == "40%" && Field("shadowDistance").Format(150.0) == "150 m",
              "Quality values are formatted unexpectedly");
        Check(Field("cascadedShadows").Cost(edited) == -0.0 && Field("bloom").Cost(edited) == 1.0,
              "Quality field cost direction is wrong");
    }

    void Editing()
    {
        Recorder recorder;
        auto session = std::make_unique<QualityTierSession>(std::make_unique<FakeStorage>(recorder));
        const auto &distance = Field("shadowDistance");
        Check(!session->IsModified() && !session->CanUndo(), "A default project started modified");

        // A held control is one undo step; edits write through to storage at once.
        Check(session->SetField(1, distance, 90.0) && session->SetField(1, distance, 95.0), "Edit rejected");
        session->EndFrame(false);
        Check(recorder.writes == 2 && recorder.stored.tiers[1].shadowDistance == 95.0f, "Edits were not written through");
        Check(session->IsModified(1, distance) && !session->IsModified(0, distance), "Modified state is wrong");
        Check(session->Undo() && session->GetTier(1).shadowDistance == 80.0f && recorder.stored.tiers[1].shadowDistance == 80.0f,
              "Undo did not restore the whole held edit, or did not write it");
        Check(session->Redo() && session->GetTier(1).shadowDistance == 95.0f, "Redo failed");

        // No-op edits record nothing; a failed write changes nothing.
        const auto writes = recorder.writes;
        Check(!session->SetField(1, distance, 95.0) && recorder.writes == writes, "A no-op edit was written");
        recorder.failWrites = true;
        Check(!session->SetField(1, distance, 10.0) && session->GetTier(1).shadowDistance == 95.0f, "A failed write changed the tier");
        recorder.failWrites = false;
        Check(!session->Modify(0, [](auto &quality) { quality.shadowCascades = 0; }), "An invalid tier was accepted");

        // Copying a field to higher tiers is one step and leaves the source and lower tiers alone.
        session->EndFrame(false);
        Check(session->CopyField(Field("volumetrics"), 0, QualityTiersAbove(0)), "Copy to higher tiers failed");
        for (std::size_t tier = 1; tier < QualityTierCount; ++tier)
            Check(!session->GetTier(tier).volumetrics, "Field copy missed a tier");
        Check(session->Undo() && session->GetTier(3).volumetrics && session->GetTier(1).shadowDistance == 95.0f,
              "Field copy was not a single undo step");

        Check(session->CopyTier(3, 0) && session->GetTier(0) == session->GetTier(3), "Tier copy failed");
        Check(session->ResetTier(0) && session->GetTier(0) == QualityTierSession::EngineDefault(0), "Tier reset failed");
        Check(session->ResetField(1, distance) && !session->IsModified(1), "Field reset failed");
        Check(session->Undo() && session->Undo() && session->GetTier(0) == session->GetTier(3), "Discrete actions merged into one undo step");
        Check(session->Redo() && session->Redo() && !session->IsModified(1), "Redo of discrete actions failed");
        Check(session->SetEnabled(2, false) && session->ResetAll() && !session->IsModified(), "Reset all failed");
        Check(session->Undo() && !session->GetTier(2).enabled, "Reset all was not undoable");
        Check(QualityTiersAbove(3) == 0 && QualityTiersBelow(0) == 0 && QualityTiersBelow(2) == 3u, "Tier masks are wrong");
    }

    void OrderIssues()
    {
        Recorder recorder;
        QualityTierSession session(std::make_unique<FakeStorage>(recorder));
        Check(session.FindOrderIssues().empty(), "Engine default tiers are out of order");
        // Medium allowing GI that High skips is out of order; disabling High moves the comparison to Ultra.
        session.Modify(1, [](auto &quality) { quality.globalIllumination = true; });
        session.Modify(2, [](auto &quality) { quality.globalIllumination = false; });
        auto issues = session.FindOrderIssues();
        Check(issues.size() == 1 && issues[0].field->id == "globalIllumination" && issues[0].lower == 1 && issues[0].higher == 2,
              "A tier allowing more than the one above it was not reported");
        session.SetEnabled(2, false);
        Check(session.FindOrderIssues().empty(), "A disabled tier took part in order checks");
        // Forcing cascaded shadows is the cheaper choice: a higher tier forcing it while a lower one does not is wrong.
        session.Modify(0, [](auto &quality) { quality.cascadedShadows = false; });
        issues = session.FindOrderIssues();
        Check(issues.size() == 1 && issues[0].field->id == "cascadedShadows" && issues[0].lower == 0 && issues[0].higher == 1,
              "Inverse-cost field order was not checked");
    }

    void Preview()
    {
        Recorder recorder;
        {
            QualityTierSession session(std::make_unique<FakeStorage>(recorder));
            session.SetPreviewTier(1);
            Check(recorder.previewed && *recorder.previewed == session.GetTier(1), "Preview did not show the tier");
            session.SetField(1, Field("grassDensity"), 0.3);
            Check(recorder.previewed && recorder.previewed->grassDensity == 0.3f, "Preview did not follow an edit");
            const auto previews = recorder.previews;
            session.SetField(3, Field("grassDensity"), 0.3);
            session.SetPreviewTier(1);
            Check(recorder.previews == previews, "Preview refreshed for an unrelated change or a repeated selection");
            session.SetPreviewTier(std::nullopt);
            Check(recorder.previewEnded, "Clearing preview did not return to scene settings");
            session.SetPreviewTier(2);
            recorder.previewEnded = false;
        }
        Check(recorder.previewEnded, "Closing a previewing session left the preview on");
    }
}

int main()
{
    try
    {
        FieldTable();
        Editing();
        OrderIssues();
        Preview();
        std::cout << "PASS: quality tier fields, editing, undo, copies, order issues and preview\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
