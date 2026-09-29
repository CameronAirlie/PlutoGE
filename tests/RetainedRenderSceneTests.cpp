#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Material.h"
#include "RetainedOpaqueBatches.h"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE::render;
void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main() try
{
    RetainedRenderScene scene;
    Material material, replacement;
    std::array source{RenderCommand{.material = &material}};
    scene.BeginFrame();
    const auto first = scene.Publish(1, 1, source);
    scene.Synchronize();
    Require(scene.Commands().size() == 1 && scene.GetStats().rebuiltCommands == 1, "Initial publication");
    const auto *address = scene.Commands()[0];
    const auto initial = scene.GetRevisions(first);
    const auto initialSequence = scene.Revision();
    scene.BeginFrame();
    Require(scene.Publish(1, 1, source) == first, "Stable producer handle");
    scene.Synchronize();
    Require(scene.Commands()[0] == address && scene.GetStats().rebuiltCommands == 0 && scene.GetStats().updates == 0,
        "Unchanged producer rebuilt its records");
    source[0].model[3].x = 3;
    scene.Commands()[0]->lodIndex = 3; // Last view's selection, not producer topology.
    scene.Publish(1, 2, source);
    source[0].model[3].x = 4;
    scene.Publish(1, 3, source);
    scene.Synchronize();
    Require(scene.Commands()[0]->model[3].x == 4 && scene.GetStats().rebuiltCommands == 1, "Coalesced updates");
    Require(scene.Revision() == initialSequence && scene.Commands()[0]->lodIndex == 3,
        "Transform update discarded persistent order/LOD state");
    Require(scene.GetRevisions(first).geometry == initial.geometry && scene.GetRevisions(first).transform > initial.transform,
        "Transform revision changed geometry");
    source[0].previousModel = source[0].model;
    scene.Publish(1, 4, source);
    scene.Synchronize();
    Require(scene.Commands()[0]->previousModel == source[0].model, "Motion history update");

    auto overlay = std::make_shared<Material>();
    material.GetConfig().additionalPasses.push_back(overlay);
    scene.BeginFrame(); scene.Publish(1, 4, source); scene.Synchronize();
    Require(scene.Commands().size() == 2 && !scene.Commands()[1]->castsShadow, "Material topology invalidation");
    material.GetConfig().additionalPasses.clear();
    scene.BeginFrame(); scene.Publish(1, 4, source); scene.Synchronize();
    Require(scene.Commands().size() == 1, "Removed overlay survived");
    scene.BeginFrame(); scene.Synchronize();
    Require(scene.Commands().empty() && !scene.IsValid(first), "Missing producer was not retired");
    source[0].material = &replacement;
    const auto recycled = scene.Publish(2, 1, source);
    scene.Synchronize();
    Require(recycled.index == first.index && recycled.generation != first.generation && !scene.IsValid(first), "Stale handle resurrected");
    source[0].instanceModels = std::make_shared<std::vector<glm::mat4>>(1, glm::mat4(1));
    bool rejected = false;
    try { scene.Publish(3, 1, source); } catch (const std::invalid_argument &) { rejected = true; }
    Require(rejected, "Mutable instance producer accepted");

    // A scene lease retains stationary producers without object publications.
    RetainedRenderScene scoped;
    auto lifetime = std::make_shared<const int>(0);
    source[0].instanceModels.reset();
    scoped.BeginFrame(); Require(scoped.BeginScope(lifetime), "New scope did not request publication");
    const auto scopedHandle = scoped.Publish(42, 1, source); scoped.EndScope(); scoped.Synchronize();
    for (int frame = 0; frame < 8; ++frame)
    {
        scoped.BeginFrame(); Require(!scoped.BeginScope(lifetime), "Stable scope requested publication");
        scoped.EndScope(); scoped.Synchronize();
        Require(scoped.IsValid(scopedHandle) && scoped.Commands().size() == 1 && scoped.GetStats().updates == 0,
            "Stationary producer required a per-frame publication");
    }
    scoped.Remove(42); scoped.Synchronize(); Require(scoped.Commands().empty(), "Explicit removal failed");
    scoped.BeginScope(lifetime); scoped.Publish(42, 2, source); scoped.EndScope();
    lifetime.reset(); scoped.Synchronize(); Require(scoped.Commands().empty(), "Destroyed scene retained resources");
    auto anotherScope = std::make_shared<const int>(0);
    scoped.BeginScope(anotherScope); scoped.Publish(43, 1, source); scoped.EndScope(); scoped.Synchronize();
    scoped.BeginFrame(); scoped.Synchronize(); Require(scoped.Commands().empty(), "Inactive scope survived scene switch");
    Require(scoped.BeginScope(anotherScope), "Returning scene failed to request publication"); scoped.EndScope();

    auto oldScope = std::make_shared<const int>(0), newScope = std::make_shared<const int>(0);
    scoped.BeginScope(oldScope); scoped.Publish(44, 1, source); scoped.EndScope();
    scoped.BeginScope(newScope); scoped.Publish(44, 1, source); scoped.EndScope();
    scoped.BeginScope(oldScope); scoped.Remove(44); scoped.EndScope(); scoped.Synchronize();
    Require(scoped.Commands().size() == 1, "Queued old-scene removal deleted a migrated producer");

    int reusedScopeStorage = 0;
    const auto makeScope = [&] { return std::shared_ptr<const void>(&reusedScopeStorage, [](const void *) {}); };
    auto originalScope = makeScope();
    RetainedRenderScene reusedScopes;
    reusedScopes.BeginFrame(); reusedScopes.BeginScope(originalScope); reusedScopes.Publish(45, 1, source);
    reusedScopes.EndScope(); reusedScopes.Synchronize(); originalScope.reset();
    auto replacementScope = makeScope();
    Require(reusedScopes.BeginScope(replacementScope), "Recycled scope address retained old generation");
    reusedScopes.EndScope(); reusedScopes.Synchronize();
    Require(reusedScopes.Commands().empty(), "Recycled scope address resurrected stale producer");

    Material versioned;
    RetainedRenderScene dependencies;
    auto dependencyScope = std::make_shared<const int>(0);
    std::array dependencySource{RenderCommand{.material = &versioned}};
    dependencies.BeginFrame(); dependencies.BeginScope(dependencyScope);
    dependencies.Publish(100, 1, dependencySource); dependencies.EndScope(); dependencies.Synchronize();
    auto editedConfig = versioned.ReadConfig();
    editedConfig.additionalPasses.push_back(std::make_shared<Material>());
    versioned.SetConfig(std::move(editedConfig));
    dependencies.BeginFrame(); dependencies.BeginScope(dependencyScope); dependencies.EndScope(); dependencies.Synchronize();
    Require(dependencies.Commands().size() == 2 && dependencies.GetStats().updates == 0,
        "Tracked material topology edit required object republication");
    dependencies.BeginFrame(); dependencies.BeginScope(dependencyScope); dependencies.EndScope(); dependencies.Synchronize();
    Require(dependencies.GetStats().rebuiltCommands == 0, "Stable tracked dependencies rebuilt commands");

    Material tracked;
    const auto materialRevision = tracked.GetRevision();
    tracked.SetRoughness(tracked.ReadConfig().roughness);
    Require(tracked.GetRevision() == materialRevision, "No-op material edit invalidated records");
    tracked.SetRoughness(.25f);
    Require(tracked.GetRevision() > materialRevision, "Material edit missed revision");
    const auto config = tracked.ReadConfig(); tracked.SetConfig(config);
    Require(tracked.GetRevision() != 0, "Tracked config replacement escaped mutable storage");
    auto &escaped = tracked.GetConfig(); escaped.roughness = .5f;
    Require(tracked.GetRevision() == 0, "Escaped material reference incorrectly trusted revisions");

    // The renderer's modern views borrow persistent rigid records. Compatibility
    // snapshots and transient submissions still describe the same scene.
    Renderer renderer;
    source[0].instanceModels.reset();
    renderer.ClearRenderCommands(); renderer.PublishRenderProducer(5, 1, source);
    const auto view = renderer.GetSceneRenderCommandView();
    Require(view.size() == 1 && view[0].model == source[0].model, "Borrowed scene view");
    const auto *persistent = &view[0];
    renderer.ClearRenderCommands(); renderer.PublishRenderProducer(5, 1, source);
    Require(&renderer.GetSceneRenderCommandView()[0] == persistent && renderer.GetRetainedSceneStats().rebuiltCommands == 0,
        "Steady frame copied rigid payload");
    renderer.SubmitRenderCommand(RenderCommand{});
    Require(renderer.GetSceneRenderCommandView().size() == 2 && renderer.GetSceneRenderCommands().size() == 2, "Mixed submission");
    renderer.ClearRenderCommands(); renderer.PublishRenderProducer(5, 1, source);
    Require(renderer.GetSceneRenderCommandView().size() == 1, "Transient command persisted");
    renderer.ClearRenderCommands();
    Require(renderer.GetSceneRenderCommandView().empty(), "Scene switch retained old commands");

    // Persistent batching must match the existing global algorithm even when
    // compatible materials are interleaved. Unchanged islands retain tokens.
    BasicMesh mesh;
    std::vector<BasicDraw> draws(6);
    std::uint64_t revision = 100;
    const auto nextRevision = [&] { return ++revision; };
    for (std::size_t i = 0; i < draws.size(); ++i)
    {
        auto &draw = draws[i];
        draw.mesh = &mesh; draw.firstIndex = 0; draw.indexCount = 3;
        draw.previousModel = glm::mat4(1);
        draw.model[3].x = float(i);
        draw.baseColor = i < 4 ? glm::vec4(1, 0, 0, 1) : glm::vec4(0, 1, 0, 1);
        draw.preparationRevision = nextRevision(); draw.preparedMaterialHash = BasicMaterialBatchHash(draw);
    }
    RetainedOpaqueBatches batches;
    std::vector<BasicDraw> output;
    const auto check = [&] {
        auto reference = draws;
        BatchOpaqueDraws(reference, nextRevision); MergeAdjacentOpaqueDraws(reference, nextRevision);
        const auto stats = batches.Update(draws, output, nextRevision);
        Require(output.size() == reference.size(), "Batch count changed");
        for (std::size_t i = 0; i < output.size(); ++i)
        {
            const auto &a = output[i], &b = reference[i];
            Require(SameBasicDrawMaterial(a, b) && a.model == b.model && a.previousModel == b.previousModel,
                "Batch surface/order changed");
            Require(bool(a.instanceModels) == bool(b.instanceModels), "Batch instancing changed");
            if (a.instanceModels) Require(*a.instanceModels == *b.instanceModels && *a.previousInstanceModels == *b.previousInstanceModels,
                "Batch instance history changed");
        }
        return stats;
    };
    Require(check().rebuilt == 2, "Initial batch groups");
    Require(check().reused == 2, "Unchanged batch groups rebuilt");
    draws[0].model[3].y = 2; draws[0].preparationRevision = nextRevision();
    const auto partial = check();
    Require(partial.reused == 1 && partial.rebuilt == 1, "Unrelated material batch rebuilt");
    std::swap(draws[1], draws[4]); check();
    draws.erase(draws.begin()); check();
    draws[0].preparationRevision = 0; Require(check().rebuilt > 0, "Mutable batch cached");
    for (std::size_t i = 0; i < draws.size(); ++i)
    {
        draws[i].firstIndex = static_cast<std::uint32_t>(i * 3);
        draws[i].model = glm::mat4(1);
        draws[i].previousModel = glm::mat4(1);
        draws[i].preparationRevision = nextRevision();
    }
    check(); // Adjacent ranges merge exactly as in the global reference.
    draws[2].alphaMode = 2;
    draws[2].preparedMaterialHash = BasicMaterialBatchHash(draws[2]);
    draws[2].preparationRevision = nextRevision();
    check(); // An order-dependent surface remains at the original boundary.

    // Mechanism benchmark: identical steady rigid payloads through the old
    // transient API and the retained producer API. No GPU or visibility work is
    // included, and timing is reported rather than used as a pass threshold.
    std::vector<RenderCommand> many(2048);
    for (std::size_t i = 0; i < many.size(); ++i) many[i].model[3].x = float(i);
    for (const bool retained : {false, true})
    {
        Renderer benchmark;
        std::size_t submitted = 0;
        constexpr int frames = 240;
        benchmark.ClearRenderCommands();
        if (retained) benchmark.PublishRenderProducer(11, 1, many);
        else benchmark.SubmitSortedRenderCommands(many, false);
        (void)benchmark.GetSceneRenderCommandView();
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; ++frame)
        {
            benchmark.ClearRenderCommands();
            if (retained) benchmark.PublishRenderProducer(11, 1, many);
            else benchmark.SubmitSortedRenderCommands(many, false);
            submitted += benchmark.GetSceneRenderCommandView().size();
            if (retained) Require(benchmark.GetRetainedSceneStats().rebuiltCommands == 0, "Steady benchmark rewrote commands");
        }
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
        Require(submitted == frames * many.size(), "Submission benchmark lost commands");
        std::cout << (retained ? "Retained" : "Transient") << " rigid submission: " << ms << " ms/frame (2048 commands)\n";
    }
    // One producer per object measures elimination of the per-object heartbeat.
    for (const bool useScope : {false, true})
    {
        Renderer benchmark;
        auto lifetime = std::make_shared<const int>(0);
        benchmark.ClearRenderCommands();
        if (useScope) benchmark.BeginRenderSceneScope(lifetime);
        for (std::size_t i = 0; i < many.size(); ++i)
            benchmark.PublishRenderProducer(i + 1, 1, std::span(&many[i], 1));
        benchmark.EndRenderSceneScope(); (void)benchmark.GetSceneRenderCommandView();
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 240; ++frame)
        {
            benchmark.ClearRenderCommands();
            if (useScope) { benchmark.BeginRenderSceneScope(lifetime); benchmark.EndRenderSceneScope(); }
            else for (std::size_t i = 0; i < many.size(); ++i)
                benchmark.PublishRenderProducer(i + 1, 1, std::span(&many[i], 1));
            Require(benchmark.GetSceneRenderCommandView().size() == many.size(), "Producer scope lost objects");
            Require(benchmark.GetRetainedSceneStats().rebuiltCommands == 0, "Stationary producer scope rebuilt commands");
        }
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 240;
        std::cout << (useScope ? "Scoped" : "Leased") << " stationary scene: " << ms << " ms/frame (2048 producers)\n";
    }
    std::cout << "Retained scene lifecycle, borrowed views and incremental batching passed\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
