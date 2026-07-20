import React, { useEffect, useState } from "react";
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
				["Scene update", sample.updateMs],
				["Render CPU", sample.renderMs],
				["Present / VSync", sample.presentMs],
				["Event polling", sample.eventPollingMs],
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

	return (
		<PanelFrame
			id="performance"
			title="Performance"
			className="performance-panel"
			actions={
				<>
					<button onClick={() => setPaused((value) => !value)}>
						{paused ? "Resume" : "Pause"}
					</button>
					<button onClick={() => setResetToken((value) => value + 1)}>
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
