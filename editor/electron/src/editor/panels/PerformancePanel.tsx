import type React from "react";
import { useEffect, useState } from "react";
import { PanelFrame } from "../components/PanelFrame";

type UiPerformance = {
	fps: number;
	frameTimeMs: number;
	maxFrameTimeMs: number;
};

const historyLength = 120;

const formatMs = (value?: number): string =>
	value === undefined ? "—" : `${value.toFixed(value < 10 ? 2 : 1)} ms`;
const formatFps = (value?: number): string =>
	value === undefined ? "—" : `${value.toFixed(1)} FPS`;
const formatCount = (value?: number): string =>
	value === undefined ? "—" : value.toLocaleString();

function MetricRows({
	rows,
}: {
	rows: ReadonlyArray<readonly [string, string]>;
}): React.JSX.Element {
	return (
		<div className="performance-detail-rows">
			{rows.map(([label, value]) => (
				<div key={label}>
					<span>{label}</span>
					<strong>{value}</strong>
				</div>
			))}
		</div>
	);
}

function PassRows({
	timings,
}: {
	timings: HostPassTiming[];
}): React.JSX.Element {
	return (
		<MetricRows
			rows={timings.map((timing) => [
				timing.name,
				timing.available === false ? "pending" : formatMs(timing.timeMs),
			])}
		/>
	);
}

function PerformanceDetails({
	sample,
}: {
	sample: HostPerformance;
}): React.JSX.Element | null {
	const [open, setOpen] = useState(sample.visible);
	const workload = sample.workload;
	const lighting = sample.lighting;
	const hasDetails = Boolean(
		sample.cpuPasses?.length ||
			sample.gpuPasses?.length ||
			sample.postProcessGpuPasses?.length ||
			lighting ||
			workload,
	);
	if (!hasDetails) return null;

	return (
		<details
			className="performance-details"
			open={open}
			onToggle={(event) => setOpen(event.currentTarget.open)}
		>
			<summary>Renderer breakdown</summary>
			<div className="performance-detail-content">
				{sample.cpuPasses && sample.cpuPasses.length > 0 && (
					<section>
						<h5>CPU passes</h5>
						<PassRows timings={sample.cpuPasses} />
					</section>
				)}
				{sample.gpuPasses && sample.gpuPasses.length > 0 && (
					<section>
						<h5>GPU passes</h5>
						<PassRows timings={sample.gpuPasses} />
					</section>
				)}
				{sample.postProcessGpuPasses &&
					sample.postProcessGpuPasses.length > 0 && (
						<section>
							<h5>Post process GPU</h5>
							<PassRows timings={sample.postProcessGpuPasses} />
						</section>
					)}
				{lighting && (
					<section>
						<h5>Lighting GPU</h5>
						<MetricRows
							rows={[
								[
									"Setup + depth copy",
									lighting.setupAvailable
										? formatMs(lighting.setupMs)
										: "pending",
								],
								[
									"Ambient fullscreen",
									lighting.ambientAvailable
										? formatMs(lighting.ambientMs)
										: "pending",
								],
								[
									"Per-light accumulation",
									lighting.lightAccumulationAvailable
										? formatMs(lighting.lightAccumulationMs)
										: "pending",
								],
								["Lights", formatCount(lighting.lightCount)],
								["Shadowed lights", formatCount(lighting.shadowedLightCount)],
							]}
						/>
					</section>
				)}
				{workload && (
					<>
						<section>
							<h5>Geometry workload</h5>
							<MetricRows
								rows={[
									[
										"Commands submitted",
										formatCount(workload.submittedRenderCommands),
									],
									[
										"Commands visible",
										formatCount(workload.visibleRenderCommands),
									],
									[
										"Submission culled",
										formatCount(workload.submissionCulledRenderCommands),
									],
									[
										"Frustum culled",
										formatCount(workload.frustumCulledRenderCommands),
									],
									[
										"Logical batches",
										formatCount(workload.geometryLogicalBatches),
									],
									[
										"Material groups",
										formatCount(workload.geometryMaterialGroups),
									],
									[
										"API draw calls",
										formatCount(workload.geometryApiDrawCalls),
									],
									["Instances", formatCount(workload.geometryInstances)],
									["Triangles", formatCount(workload.geometryTriangles)],
									[
										"LOD0 triangles",
										formatCount(workload.geometryTrianglesByLod[0]),
									],
									[
										"LOD1 triangles",
										formatCount(workload.geometryTrianglesByLod[1]),
									],
									[
										"LOD2 triangles",
										formatCount(workload.geometryTrianglesByLod[2]),
									],
									[
										"LOD3+ triangles",
										formatCount(workload.geometryTrianglesByLod[3]),
									],
								]}
							/>
						</section>
						<section>
							<h5>Shadow workload</h5>
							<MetricRows
								rows={[
									[
										"Updated surfaces",
										formatCount(workload.shadowUpdatedSurfaces),
									],
									[
										"Directional cascades",
										formatCount(workload.shadowUpdatedDirectionalCascades),
									],
									["Updated pixels", formatCount(workload.shadowUpdatedPixels)],
									[
										"Logical batches",
										formatCount(workload.shadowLogicalBatches),
									],
									[
										"Material groups",
										formatCount(workload.shadowMaterialGroups),
									],
									["API draw calls", formatCount(workload.shadowApiDrawCalls)],
									["Instances", formatCount(workload.shadowInstances)],
									["Triangles", formatCount(workload.shadowTriangles)],
								]}
							/>
						</section>
					</>
				)}
			</div>
		</details>
	);
}

function appendHostReport(
	lines: string[],
	title: string,
	sample?: HostPerformance,
): void {
	lines.push(title);
	if (!sample) {
		lines.push("Unavailable", "");
		return;
	}
	const ms = (value?: number): string =>
		value === undefined ? "unavailable" : `${value.toFixed(3)} ms`;
	lines.push(
		`Status: ${sample.visible ? "Rendering" : "Hidden"}`,
		`Viewport: ${sample.viewportWidth ?? "?"} x ${sample.viewportHeight ?? "?"}`,
		`FPS: ${sample.fps.toFixed(3)}`,
		`Average frame: ${ms(sample.frameTimeMs)}`,
		`Peak frame: ${ms(sample.maxFrameTimeMs)}`,
		`VSync: ${sample.vSync === undefined ? "unknown" : sample.vSync ? "On" : "Off"}`,
		`Profiled renders: ${sample.profiledRenderCount ?? "unavailable"}`,
		"Frame breakdown",
		`  Command handling: ${ms(sample.commandMs)}`,
		`  Event polling: ${ms(sample.eventPollingMs)}`,
		`  Viewport interaction: ${ms(sample.interactionMs)}`,
		`  Scene update: ${ms(sample.updateMs)}`,
		`  Render CPU: ${ms(sample.renderMs)}`,
		`  Present / swap: ${ms(sample.presentMs)}`,
		`  Frame wait: ${ms(sample.waitMs)}`,
		`  Other host overhead: ${ms(sample.overheadMs)}`,
		`  CPU passes total: ${ms(sample.cpuPassMs)}`,
		`  GPU passes total: ${ms(sample.gpuPassMs)}`,
	);
	const appendPasses = (label: string, timings?: HostPassTiming[]): void => {
		if (!timings?.length) return;
		lines.push(label);
		for (const timing of timings)
			lines.push(
				`  ${timing.name}: ${timing.available === false ? "pending" : ms(timing.timeMs)}`,
			);
	};
	appendPasses("CPU pass timings", sample.cpuPasses);
	appendPasses("GPU pass timings", sample.gpuPasses);
	appendPasses("Post-process GPU timings", sample.postProcessGpuPasses);
	if (sample.lighting) {
		const lighting = sample.lighting;
		lines.push(
			"Lighting GPU",
			`  Setup + depth copy: ${lighting.setupAvailable ? ms(lighting.setupMs) : "pending"}`,
			`  Ambient fullscreen: ${lighting.ambientAvailable ? ms(lighting.ambientMs) : "pending"}`,
			`  Per-light accumulation: ${lighting.lightAccumulationAvailable ? ms(lighting.lightAccumulationMs) : "pending"}`,
			`  Lights: ${lighting.lightCount}`,
			`  Shadowed lights: ${lighting.shadowedLightCount}`,
		);
	}
	if (sample.workload) {
		const workload = sample.workload;
		lines.push(
			"Geometry workload",
			`  Render commands submitted: ${workload.submittedRenderCommands}`,
			`  Render commands submission culled: ${workload.submissionCulledRenderCommands}`,
			`  Render commands visible: ${workload.visibleRenderCommands}`,
			`  Render commands frustum culled: ${workload.frustumCulledRenderCommands}`,
			`  Visible commands with one LOD: ${workload.visibleSingleLodCommands}`,
			`  Visible commands with multiple LODs: ${workload.visibleMultiLodCommands}`,
			`  Render command sorts: ${workload.renderCommandSorts}`,
			`  Logical batches: ${workload.geometryLogicalBatches}`,
			`  Material groups: ${workload.geometryMaterialGroups}`,
			`  API draw calls: ${workload.geometryApiDrawCalls}`,
			`  Instances: ${workload.geometryInstances}`,
			`  Triangles: ${workload.geometryTriangles}`,
			`  LOD triangles: ${workload.geometryTrianglesByLod.join(", ")}`,
			"Shadow workload",
			`  Updated surfaces: ${workload.shadowUpdatedSurfaces}`,
			`  Updated directional cascades: ${workload.shadowUpdatedDirectionalCascades}`,
			`  Updated pixels: ${workload.shadowUpdatedPixels}`,
			`  Logical batches: ${workload.shadowLogicalBatches}`,
			`  Material groups: ${workload.shadowMaterialGroups}`,
			`  API draw calls: ${workload.shadowApiDrawCalls}`,
			`  Instances: ${workload.shadowInstances}`,
			`  Triangles: ${workload.shadowTriangles}`,
			"Resize activity",
			`  Intermediate targets: ${ms(workload.intermediateTargetResizeMs)} (${workload.intermediateTargetResizes})`,
			`  GBuffer: ${ms(workload.gBufferResizeMs)} (${workload.gBufferResizes})`,
		);
	}
	lines.push("");
}

function buildMetricsReport(
	scene?: HostPerformance,
	game?: HostPerformance,
	ui?: UiPerformance,
): string {
	const lines = [
		"PlutoGE Electron Performance",
		`Captured: ${new Date().toISOString()}`,
		"",
	];
	appendHostReport(lines, "Scene View", scene);
	appendHostReport(lines, "Game View", game);
	lines.push("Editor UI");
	if (ui)
		lines.push(
			`FPS: ${ui.fps.toFixed(3)}`,
			`Average frame: ${ui.frameTimeMs.toFixed(3)} ms`,
			`Peak frame: ${ui.maxFrameTimeMs.toFixed(3)} ms`,
		);
	else lines.push("Unavailable");
	return lines.join("\n");
}

function useUiPerformance(): UiPerformance | undefined {
	const [sample, setSample] = useState<UiPerformance>();

	useEffect(() => {
		let animationFrame = 0;
		let previousFrame = performance.now();
		let windowStart = previousFrame;
		let frameCount = 0;
		let totalFrameTime = 0;
		let maxFrameTime = 0;

		const tick = (now: number): void => {
			const frameTime = now - previousFrame;
			previousFrame = now;
			if (frameTime > 0 && frameTime < 1000) {
				frameCount += 1;
				totalFrameTime += frameTime;
				maxFrameTime = Math.max(maxFrameTime, frameTime);
			}

			const windowTime = now - windowStart;
			if (windowTime >= 250 && frameCount > 0) {
				setSample({
					fps: (frameCount * 1000) / windowTime,
					frameTimeMs: totalFrameTime / frameCount,
					maxFrameTimeMs: maxFrameTime,
				});
				windowStart = now;
				frameCount = 0;
				totalFrameTime = 0;
				maxFrameTime = 0;
			}
			animationFrame = requestAnimationFrame(tick);
		};

		animationFrame = requestAnimationFrame(tick);
		return () => cancelAnimationFrame(animationFrame);
	}, []);

	return sample;
}

function useFrameHistory(
	value: number | undefined,
	paused: boolean,
	resetToken: number,
): number[] {
	const [history, setHistory] = useState<number[]>([]);
	// biome-ignore lint/correctness/useExhaustiveDependencies: changing the explicit reset token clears the rolling history
	useEffect(() => setHistory([]), [resetToken]);
	useEffect(() => {
		if (value === undefined || paused) return;
		setHistory((current) => [...current.slice(-(historyLength - 1)), value]);
	}, [paused, value]);
	return history;
}

function FrameGraph({ values }: { values: number[] }): React.JSX.Element {
	const ceiling = Math.max(33.33, ...values) * 1.1;
	const points = values
		.map((value, index) => {
			const x = values.length <= 1 ? 100 : (index * 100) / (values.length - 1);
			const y = 40 - Math.min(40, (value / ceiling) * 40);
			return `${x.toFixed(2)},${y.toFixed(2)}`;
		})
		.join(" ");
	const guideY = (milliseconds: number): number =>
		40 - Math.min(40, (milliseconds / ceiling) * 40);

	return (
		<div
			className="performance-graph"
			title={`Graph scale: 0–${ceiling.toFixed(1)} ms`}
		>
			<svg
				viewBox="0 0 100 40"
				preserveAspectRatio="none"
				aria-label="Rolling frame-time graph"
			>
				<title>Rolling frame-time graph</title>
				<line
					className="performance-guide guide-60"
					x1="0"
					x2="100"
					y1={guideY(16.67)}
					y2={guideY(16.67)}
				/>
				<line
					className="performance-guide guide-30"
					x1="0"
					x2="100"
					y1={guideY(33.33)}
					y2={guideY(33.33)}
				/>
				{points && <polyline points={points} />}
			</svg>
			<span>16.7</span>
			<span>33.3</span>
		</div>
	);
}

function SummaryMetrics({
	fps,
	frameTimeMs,
	maxFrameTimeMs,
}: UiPerformance): React.JSX.Element {
	return (
		<div className="performance-summary">
			<div>
				<small>FPS</small>
				<strong>{formatFps(fps).replace(" FPS", "")}</strong>
			</div>
			<div>
				<small>Average</small>
				<strong>{formatMs(frameTimeMs)}</strong>
			</div>
			<div>
				<small>Peak</small>
				<strong>{formatMs(maxFrameTimeMs)}</strong>
			</div>
		</div>
	);
}

function HostPerformanceCard({
	title,
	sample,
	history,
}: {
	title: string;
	sample?: HostPerformance;
	history: number[];
}): React.JSX.Element {
	const phases = sample
		? ([
				["Command handling", sample.commandMs],
				["Scene update", sample.updateMs],
				["Render CPU", sample.renderMs],
				["Present / VSync", sample.presentMs],
				["Event polling", sample.eventPollingMs],
				["Viewport interaction", sample.interactionMs],
				["Frame wait", sample.waitMs],
				["Other overhead", sample.overheadMs],
				["CPU passes", sample.cpuPassMs],
				["GPU passes", sample.gpuPassMs],
			] as const)
		: [];

	return (
		<section
			className={`performance-card ${sample?.visible === false ? "inactive" : ""}`}
		>
			<header>
				<h4>{title}</h4>
				<span>
					{sample
						? sample.visible
							? "Rendering"
							: "Hidden"
						: "Waiting for host"}
				</span>
			</header>
			{sample ? (
				<>
					<SummaryMetrics
						fps={sample.fps}
						frameTimeMs={sample.frameTimeMs}
						maxFrameTimeMs={sample.maxFrameTimeMs}
					/>
					<FrameGraph values={history} />
					<div className="performance-phases">
						{phases.map(([label, value]) => (
							<div key={label}>
								<span>{label}</span>
								<strong>{formatMs(value)}</strong>
							</div>
						))}
					</div>
					<PerformanceDetails sample={sample} />
				</>
			) : (
				<div className="performance-empty">
					Collecting native frame telemetry…
				</div>
			)}
		</section>
	);
}

export function PerformancePanel({
	scene,
	game,
}: {
	scene?: HostPerformance;
	game?: HostPerformance;
}): React.JSX.Element {
	const ui = useUiPerformance();
	const [paused, setPaused] = useState(false);
	const [resetToken, setResetToken] = useState(0);
	const sceneHistory = useFrameHistory(scene?.frameTimeMs, paused, resetToken);
	const gameHistory = useFrameHistory(game?.frameTimeMs, paused, resetToken);
	const uiHistory = useFrameHistory(ui?.frameTimeMs, paused, resetToken);
	const [copyStatus, setCopyStatus] = useState<"idle" | "copied" | "error">(
		"idle",
	);
	const copyMetrics = async (): Promise<void> => {
		try {
			await navigator.clipboard.writeText(buildMetricsReport(scene, game, ui));
			setCopyStatus("copied");
		} catch {
			setCopyStatus("error");
		}
		window.setTimeout(() => setCopyStatus("idle"), 1800);
	};

	return (
		<PanelFrame
			id="performance"
			title="Performance"
			className="performance-panel"
			actions={
				<>
					<button type="button" onClick={() => void copyMetrics()}>
						{copyStatus === "copied"
							? "Copied!"
							: copyStatus === "error"
								? "Copy failed"
								: "Copy Metrics"}
					</button>
					<button type="button" onClick={() => setPaused((value) => !value)}>
						{paused ? "Resume" : "Pause"}
					</button>
					<button
						type="button"
						onClick={() => setResetToken((value) => value + 1)}
					>
						Clear
					</button>
				</>
			}
		>
			<div className="performance-content">
				<HostPerformanceCard
					title="Scene View"
					sample={scene}
					history={sceneHistory}
				/>
				<HostPerformanceCard
					title="Game View"
					sample={game}
					history={gameHistory}
				/>
				<section className="performance-card">
					<header>
						<h4>Editor UI</h4>
						<span>Animation frame</span>
					</header>
					{ui ? (
						<>
							<SummaryMetrics {...ui} />
							<FrameGraph values={uiHistory} />
						</>
					) : (
						<div className="performance-empty">
							Collecting UI frame telemetry…
						</div>
					)}
				</section>
			</div>
		</PanelFrame>
	);
}
