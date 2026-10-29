#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/assets/PostProcessStack.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/panels/PostProcessEditorPanel.h"
#include "PlutoGE/ui/postprocess/PostProcessPresetSession.h"
#include "PlutoGE/ui/postprocess/PostProcessViews.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Drives the post-process preset pipeline: the effect registry, syncing camera effects to a preset while keeping
// their instances, presets following saves and previews, the editor session (undo, saving, live preview), the
// stack analysis, and every editor view in a hidden OpenGL window. With an output prefix argument, writes a .ppm of
// each view for visual QA.
namespace
{
    using namespace PlutoGE;

    constexpr int kWidth = 1100;
    constexpr int kHeight = 720;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    assets::PostProcessPresetAsset MakePreset(std::initializer_list<const char *> types)
    {
        assets::PostProcessPresetAsset preset;
        for (const char *type : types)
            preset.effects.push_back(assets::CreateDefaultPostProcessEffectAsset(type));
        return preset;
    }

    void SetParameter(assets::PostProcessPresetAsset &preset, int effect, const std::string &name, std::string value)
    {
        auto &parameters = preset.effects[effect].parameters;
        const auto found = std::ranges::find(parameters, name, &render::PostProcessParameter::name);
        Require(found != parameters.end(), "Test parameter " + name + " exists");
        found->value = std::move(value);
    }

    std::string GetParameter(const render::IPostProcessEffect &effect, const std::string &name)
    {
        const auto parameters = effect.GetParameters();
        const auto found = std::ranges::find(parameters, name, &render::PostProcessParameter::name);
        return found == parameters.end() ? std::string{} : found->value;
    }

    void TestRegistry()
    {
        for (const auto &descriptor : render::GetPostProcessEffectDescriptors())
        {
            const auto effect = render::CreatePostProcessEffect(descriptor.typeName);
            Require(effect && effect->GetTypeName() == descriptor.typeName, "Each registered effect creates its own type");
            Require(!descriptor.description.empty(), "Each registered effect is described");
            const auto *defaults = render::GetPostProcessEffectDefaults(descriptor.typeName);
            Require(defaults && defaults->displayName == effect->GetDisplayName(), "Defaults carry the display name");
        }
        Require(!render::CreatePostProcessEffect("NoSuchEffect") && !render::GetPostProcessEffectDefaults("NoSuchEffect"),
                "Unknown types create nothing");
        Require(render::GetRegisteredPostProcessEffectTypes().size() == render::GetPostProcessEffectDescriptors().size(),
                "The type list matches the registry");
        const auto descriptors = render::GetPostProcessEffectDescriptors();
        Require(std::ranges::is_sorted(descriptors, {}, &render::PostProcessEffectDescriptor::stage), "The registry is in pipeline order");
    }

    void TestSync()
    {
        auto preset = MakePreset({"Bloom", "ToneMapping"});
        std::vector<std::unique_ptr<render::IPostProcessEffect>> effects;
        Require(assets::SyncPostProcessEffects(preset, effects) && effects.size() == 2, "Syncing an empty stack creates the effects");
        auto *bloom = effects[0].get();
        auto *toneMapping = effects[1].get();
        Require(!assets::SyncPostProcessEffects(preset, effects), "Syncing an unchanged preset changes nothing");
        const auto bloomRevision = bloom->GetConfigurationRevision();
        const auto toneRevision = toneMapping->GetConfigurationRevision();

        SetParameter(preset, 0, "Intensity", "2.500000");
        Require(assets::SyncPostProcessEffects(preset, effects) && effects[0].get() == bloom, "A changed setting keeps the instance");
        Require(GetParameter(*bloom, "Intensity").starts_with("2.5"), "A changed setting is applied");
        Require(toneMapping->GetConfigurationRevision() == toneRevision && bloom->GetConfigurationRevision() > bloomRevision,
                "Only the effect that changed is reconfigured");

        std::ranges::reverse(preset.effects);
        preset.effects[0].enabled = false;
        Require(assets::SyncPostProcessEffects(preset, effects) && effects[0].get() == toneMapping && effects[1].get() == bloom,
                "Reordering keeps instances");
        Require(!toneMapping->IsEnabled(), "Turning an effect off is applied");

        preset.effects.erase(preset.effects.begin());
        preset.effects.push_back({"NoSuchEffect", true, {}});
        preset.effects.push_back(assets::CreateDefaultPostProcessEffectAsset("FXAA"));
        Require(assets::SyncPostProcessEffects(preset, effects) && effects.size() == 2 && effects[0].get() == bloom &&
                    effects[1]->GetTypeName() == "FXAA",
                "Removed effects go, unknown ones are skipped and new ones are created");
    }

    void TestStackFollowsPreset()
    {
        assets::AssetManager assets;
        const std::string reference = "project://PostProcessing/Test.plutopostprocess";
        assets::PostProcessStack stack;
        Require(!stack.SetPreset(assets, reference) && !stack.HasPreset(), "A preset that cannot be loaded is not assigned");

        auto preset = MakePreset({"Bloom", "ToneMapping"});
        assets.SetPostProcessPresetPreview(reference, preset);
        Require(stack.SetPreset(assets, reference) && stack.GetEffects().size() == 2 && !stack.IsOutOfDate(assets),
                "A preset is applied when assigned");
        auto *bloom = stack.GetEffects()[0].get();

        const auto revision = assets.GetPostProcessPresetRevision(reference);
        assets.SetPostProcessPresetPreview(reference, preset);
        Require(assets.GetPostProcessPresetRevision(reference) == revision, "Previewing identical settings is not a change");

        SetParameter(preset, 0, "Intensity", "3.000000");
        assets.SetPostProcessPresetPreview(reference, preset);
        Require(stack.IsOutOfDate(assets), "A changed preset marks cameras out of date");
        Require(stack.Refresh(assets) && stack.GetEffects()[0].get() == bloom && GetParameter(*bloom, "Intensity").starts_with("3"),
                "Refreshing applies the change to the same effect");
        Require(!stack.Refresh(assets), "Refreshing again does nothing");

        // The file does not exist: clearing the preview makes the preset unreadable, which keeps the last good effects.
        assets.ClearPostProcessPresetPreview(reference);
        Require(stack.IsOutOfDate(assets) && !stack.Refresh(assets) && stack.GetEffects().size() == 2,
                "An unreadable preset keeps the last good effects");

        const auto path = std::filesystem::temp_directory_path() / "PlutoGEPostProcessEditorTests.plutopostprocess";
        const std::string fileReference = path.generic_string();
        std::string error;
        Require(assets.SavePostProcessPresetAsset(fileReference, MakePreset({"ToneMapping"}), &error), "Saving a preset: " + error);
        assets::PostProcessStack fileStack;
        Require(fileStack.SetPreset(assets, fileReference) && fileStack.GetEffects().size() == 1, "A saved preset loads");
        Require(assets.SavePostProcessPresetAsset(fileReference, MakePreset({"ToneMapping", "FXAA"}), &error) && fileStack.IsOutOfDate(assets),
                "Saving marks cameras using the preset out of date");
        Require(fileStack.Refresh(assets) && fileStack.GetEffects().size() == 2, "Cameras pick up the saved preset");
        assets.SetPostProcessPresetPreview(fileReference, MakePreset({"Bloom"}));
        Require(assets.SavePostProcessPresetAsset(fileReference, MakePreset({"ToneMapping"}), &error) &&
                    assets.LoadPostProcessPresetAsset(fileReference).effects.size() == 1,
                "Saving replaces the preview");
        std::filesystem::remove(path);

        stack.SetInlineEffects({});
        Require(!stack.HasPreset() && !stack.IsOutOfDate(assets), "An inline stack follows no preset");
    }

    // Keeps the preset in memory, standing in for an asset file, and records previews.
    class MemoryStorage final : public ui::PostProcessPresetStorage
    {
    public:
        explicit MemoryStorage(assets::PostProcessPresetAsset &preset) : m_preset(preset) {}
        std::string GetLabel() const override { return "Look.plutopostprocess"; }
        bool Read(assets::PostProcessPresetAsset &preset, std::string &) override
        {
            preset = m_preset;
            return true;
        }
        bool Write(const assets::PostProcessPresetAsset &preset, std::string &) override
        {
            m_preset = preset;
            ++writes;
            return true;
        }
        void Preview(const assets::PostProcessPresetAsset *preset) override
        {
            previewing = preset != nullptr;
            if (preset)
                previewed = *preset;
        }
        int writes = 0;
        bool previewing = false;
        assets::PostProcessPresetAsset previewed;

    private:
        assets::PostProcessPresetAsset &m_preset;
    };

    void TestSession()
    {
        auto stored = MakePreset({"Bloom", "ToneMapping"});
        std::string error;
        auto storage = std::make_unique<MemoryStorage>(stored);
        auto *memory = storage.get();
        auto session = ui::PostProcessPresetSession::Open(std::move(storage), error);
        Require(session && !session->IsDirty() && !memory->previewing, "A preset opens without unsaved changes");

        // A drag: several edits while the control is held make one undo step.
        session->Modify([](auto &preset) { SetParameter(preset, 0, "Intensity", "1.100000"); });
        session->EndFrame(true);
        session->Modify([](auto &preset) { SetParameter(preset, 0, "Intensity", "1.200000"); });
        session->EndFrame(false);
        Require(session->IsDirty() && memory->previewing && memory->previewed == session->GetPreset(),
                "Edits mark the session unsaved and preview live");
        session->Undo();
        Require(!session->IsDirty() && !memory->previewing, "One undo reverts the drag and ends the preview");
        session->Redo();
        Require(session->IsDirty() && memory->previewing, "Redo restores the drag and the preview");

        session->SetLivePreview(false);
        Require(!memory->previewing, "Turning live preview off shows the saved preset");
        session->SetLivePreview(true);
        Require(memory->previewing, "Turning live preview on shows the edits again");

        Require(session->Save(error) && !session->IsDirty() && memory->writes == 1 && !memory->previewing && stored == session->GetPreset(),
                "Save writes the preset and ends the preview");

        session->Modify([](auto &preset) { preset.effects.clear(); });
        session->EndFrame(false);
        session->Revert();
        Require(session->GetPreset() == stored && !session->IsDirty() && !memory->previewing, "Revert restores the saved preset");
        session->Undo();
        Require(session->GetPreset().effects.empty(), "Revert can be undone");

        session.reset();
        Require(!memory->previewing, "Closing without saving ends the preview");
    }

    void TestAnalysis()
    {
        namespace pp = ui::postprocess;
        pp::PostProcessStackAnalysis analysis;
        analysis.Analyze(MakePreset({"ToneMapping", "Bloom"}));
        Require(analysis.WarningCount() == 0 && analysis.issues.empty(), "A plain stack has no findings");

        auto preset = MakePreset({"Bloom", "TAA", "FXAA", "Bloom"});
        preset.effects.push_back({"NoSuchEffect", true, {}});
        analysis.Analyze(preset);
        Require(analysis.WarningCount() == 3, "Duplicates, unknown effects and missing tone mapping are problems");
        Require(analysis.effectWarnings[3] == 1 && analysis.effectWarnings[4] == 1 && analysis.effectWarnings[0] == 0,
                "Problems are counted on their effects");
        Require(std::ranges::any_of(analysis.issues, [](const auto &issue) { return issue.severity == pp::PostProcessIssue::Severity::Note && issue.effect < 0; }),
                "TAA with FXAA is noted");

        const auto ordered = MakePreset({"ToneMapping", "Bloom", "DDGI"});
        Require(pp::PipelineOrder(ordered) == std::vector<int>{2, 1, 0}, "Effects are shown in pipeline order");
        const auto inPipelineOrder = MakePreset({"DDGI", "Bloom", "ToneMapping"});
        Require(pp::InsertionIndex(inPipelineOrder, "AutoExposure") == 2 && pp::InsertionIndex(inPipelineOrder, "SSAO") == 1 &&
                    pp::InsertionIndex(MakePreset({"Bloom", "ToneMapping"}), "MotionBlur") == 1,
                "New effects go after the last effect of their stage or an earlier one");
    }

    struct Window
    {
        GLFWwindow *handle = nullptr;
        ui::fonts::EditorFontLibrary fonts;

        bool Create()
        {
            if (!glfwInit())
                return false;
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            handle = glfwCreateWindow(kWidth, kHeight, "Post process editor test", nullptr, nullptr);
            if (!handle)
                return false;
            glfwMakeContextCurrent(handle);
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
                return false;
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            fonts.Load(ImGui::GetIO());
            ui::ApplyActiveEditorTheme();
            ImGui_ImplGlfw_InitForOpenGL(handle, false);
            ImGui_ImplOpenGL3_Init("#version 330");
            return true;
        }

        ~Window()
        {
            if (!handle)
                return;
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            glfwDestroyWindow(handle);
            glfwTerminate();
        }

        void Frame(const std::function<void()> &draw)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::GetIO().DisplaySize = ImVec2(kWidth, kHeight);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
            draw();
            ImGui::Render();
            glViewport(0, 0, kWidth, kHeight);
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        void Capture(const std::string &path) const
        {
            std::vector<unsigned char> pixels(kWidth * kHeight * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, kWidth, kHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << kWidth << " " << kHeight << "\n255\n";
            for (int y = kHeight - 1; y >= 0; --y)
                file.write(reinterpret_cast<const char *>(pixels.data() + y * kWidth * 3), kWidth * 3);
        }
    };

    void TestViews(const std::string &capturePrefix)
    {
        Window window;
        if (!window.Create())
        {
            std::cout << "PostProcessEditorTests: no OpenGL context, skipping views\n";
            return;
        }
        auto stored = MakePreset({"SSAO", "TAA", "Bloom", "ToneMapping", "ColorGrading"});
        stored.effects[1].enabled = false;
        std::string error;
        auto session = ui::PostProcessPresetSession::Open(std::make_unique<MemoryStorage>(stored), error);
        auto *raw = session.get();
        const auto capture = [&](const char *name)
        {
            if (!capturePrefix.empty())
                window.Capture(capturePrefix + name + ".ppm");
        };
        const auto frames = [&](const std::function<void()> &draw)
        {
            // A few frames settle layout (tables, child sizes) before capturing.
            for (int frame = 0; frame < 4; ++frame)
                window.Frame(draw);
        };

        ui::PostProcessEditorPanel panel({"Post Process Editor", true});
        panel.Open(std::move(session));
        raw->Modify([](auto &preset) { SetParameter(preset, 2, "Intensity", "1.750000"); });
        raw->EndFrame(false);
        frames([&] { panel.Update(); });
        capture("panel");
        Require(panel.HasUnsavedChanges(), "The panel reports unsaved changes");

        ui::postprocess::PostProcessStackAnalysis analysis;
        ui::postprocess::EffectsViewState effects;
        const auto drawEffects = [&]
        {
            analysis.Update(*raw);
            ImGui::Begin("Effects");
            ui::postprocess::DrawEffectsView(*raw, effects, analysis);
            ImGui::End();
        };
        effects.selected = 4; // Color Grading: colours and many settings
        frames(drawEffects);
        capture("effects-color-grading");
        effects.selected = 1; // TAA, turned off
        frames(drawEffects);
        capture("effects-disabled");

        raw->Modify([](auto &preset) { preset.effects.erase(preset.effects.begin() + 3); preset.effects.push_back({"NoSuchEffect", true, {}}); });
        raw->EndFrame(false);
        effects.selected = 3;
        frames(drawEffects);
        capture("effects-problems");

        frames([&]
               {
                   ImGui::Begin("Used By");
                   bool assign = false;
                   static_cast<void>(ui::postprocess::DrawUsageView({{"Editor Camera", "Editor viewport", 0}, {"Main Camera", "Main camera", 7}}, true, assign));
                   ImGui::End();
               });
        capture("usage");
    }
}

int main(int argc, char **argv)
{
    try
    {
        TestRegistry();
        TestSync();
        TestStackFollowsPreset();
        TestSession();
        TestAnalysis();
        TestViews(argc > 1 ? argv[1] : "");
    }
    catch (const std::exception &error)
    {
        std::cerr << "PostProcessEditorTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "PostProcessEditorTests passed\n";
    return 0;
}
