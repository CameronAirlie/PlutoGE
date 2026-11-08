# Engineering notes

Notes for people working on the engine itself: plans, roadmaps, performance passes, investigations and handoffs.
They record what was done and why at the time; the code is the authority where they differ. To make games with
PlutoGE, use the [manual](../README.md) instead.

## Plans and roadmaps

| Note | Topic |
| --- | --- |
| [Feature milestones](FEATURE_MILESTONES.md) | Engine feature roadmap |
| [Environment design milestones](ENVIRONMENT_DESIGN_MILESTONES.md) | Environment roadmap |
| [M11–M14 implementation plan](M11_M14_IMPLEMENTATION_PLAN.md) | Milestone plan |
| [Usability and workflow plan](USABILITY_PLAN.md) | Editor and scripting usability work, with its progress log |
| [Asset management transition plan](asset-management-transition-plan-2026-10-07.md) | Asset pipeline migration |
| [Asset ownership and identity contract](ASSET_IDENTITY_AND_OWNERSHIP.md) | Rules the asset migration follows |
| [Editor scene edit performance plan](editor-scene-edit-performance-plan-2026-11-06.md) | Undo and scene edit costs |
| [Project workflow performance](PROJECT_WORKFLOW_PERFORMANCE.md) | Project open, save and import times |
| [Jungle scene plan](JUNGLE_SCENE_PLAN.md), [handover](jungle-scene-handover.md) | Showcase scene |

## Renderer architecture

| Note | Topic |
| --- | --- |
| [Core renderer implementation](core-renderer-implementation-plan.md) | Renderer foundations |
| [Renderer modernisation](renderer-modernisation.md) | Clustered lights, synchronisation, GPU-driven opaque, frame graph |
| [Frame pipeline](frame-pipeline.md) | Render thread, parallel animation and visibility |
| [Retained render scene](retained-render-scene.md), [retained packets and glass snapshots](retained-packets-and-glass-snapshots.md) | Retained scene data |
| [Change-driven updates](renderer-change-driven-updates.md) | Updating only what changed |
| [Parallel recording](parallel-recording.md) | Command recording and object streaming |
| [Legacy OpenGL removal plan](legacy-opengl-removal-plan.md) | Retiring the old renderer |
| [Custom shader audit](CustomShaderAudit.md) | Custom shader support |
| [Rough reflections](rough-reflections.md) | Screen-space reflection model |

## RHI and Vulkan

| Note | Topic |
| --- | --- |
| [RHI conventions](rhi-conventions.md) | Rules for RHI code |
| [RHI implementation plan (historical)](rhi-implementation-plan.md), [agent handoff](rhi-agent-handoff.md) | RHI build-out |
| [Editor Vulkan migration](rhi-editor-migration.md) | Moving the editor to the RHI |
| [Post-process migration handoff](rhi-postprocess-next-agent.md) | Post-processing on the RHI |
| [Particles, point lights and thumbnails](rhi-particles-and-point-lights.md) | RHI feature ports |
| [Vulkan skeletal animation](vulkan-skinning.md), [GPU skinning](GPU_SKINNING.md) | Skinning on the GPU |

## Performance passes and investigations

| Area | Notes |
| --- | --- |
| Overall | [Optimisation plan](render-performance-optimization-plan.md), [structural investigation](renderer-performance-investigation-2026-09-29.md), [structural review](renderer-structural-review.md), [submission follow-up](renderer-submission-optimizations.md), [GPU pass](gpu-optimisation-pass.md), [integrated GPU lighting](igpu-lighting-performance-pass.md), [tooling and opaque pass](rendering-performance.md), [volumetric, particle and colour pass](rendering-optimizations.md), [capturing metrics](profiler-capture-geometry-diagnostics.md) |
| Shadows | [Directional optimisation](directional-shadow-optimisation.md), [gather pass](directional-shadow-gather-pass.md), [next pass](directional-shadow-next-pass.md), [shadow and transparency](shadow-and-transparency-performance.md), [face caching](shadow-face-caching-and-diagnostics.md), [VSM in dense foliage](vsm-dense-foliage.md), [exclusive VSM path](vsm-exclusive-shadow-path.md), [VSM quality and freshness](vsm-quality-and-freshness.md) |
| Screen space | [Shared effects](screen-space-effects-optimisation.md), [SSR stage](ssr-stage-optimisation.md), [SSR loop](ssr-loop-optimisation.md), [SSR adaptive rays](ssr-adaptive-rays-and-filter.md), [SSR capture](ssr-capture-investigation-2026-09-16.md), [SSR and deformation](ssr-parallel-deformation-investigation.md), [SSS fidelity plan](sss-fidelity-optimisation-plan.md), [SSS geometry](sss-geometry-investigation.md) |
| Characters | [Animated characters](animated-character-performance.md), [CPU skinning and SSR](skinning-ssr-optimisation.md), [parallel skinning and SSR](parallel-skinning-ssr-pass.md), [soldier LODs and occlusion](soldier-lods-and-occlusion.md) |
| Sample projects | DungeonCrawler: [performance](dungeon-performance-pass.md), [CPU](dungeon-cpu-optimisation-pass.md), [Radeon](dungeon-radeon-performance-pass.md), [rendering](dungeon-rendering-optimisation-pass.md), [architecture](dungeon-rendering-architecture-investigation.md), [structural](dungeon-structural-performance-pass.md). Bistro: [render preparation](bistro-render-preparation-optimizations.md), [shadow CPU](bistro-shadow-cpu-performance.md). [ShadowSouls](shadowsouls-render-performance.md), [RocketLeg](rocketleg-shader-graph-performance-2026-10-01.md), [Standoff](standoff-performance-plan-2026-10-15.md) |
| Lighting | [Surface lighting extensions plan](surface-lighting-extensions-plan.md) |
| Audio | [Debug mixer deadline fix](audio-debug-mixer-performance.md) |
