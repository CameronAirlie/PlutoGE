import React, { useEffect, useLayoutEffect, useRef } from "react";
import { PanelFrame } from "../components/PanelFrame";

const compositeViews = [
	{ value: 0, label: "Composite (Lit)" },
	{ value: 1, label: "G-buffer Quadrants" },
	{ value: 2, label: "World Position" },
	{ value: 3, label: "World Normal" },
	{ value: 4, label: "Albedo" },
	{ value: 5, label: "Depth" },
	{ value: 6, label: "Shadow Cascades" },
	{ value: 7, label: "Raw Shadow Mask" },
	{ value: 8, label: "Filtered Shadow Mask" },
	{ value: 9, label: "LOD" },
] as const;

export function ViewportPanel({
	host,
	editor,
}: {
	host: HostState;
	editor?: EditorState;
}): React.JSX.Element {
	const viewport = useRef<HTMLDivElement>(null);

	useLayoutEffect(() => {
		const element = viewport.current;
		if (!element) return;
		let animationFrame = 0;
		let previousBounds = "";
		const updateBounds = (): void => {
			const bounds = element.getBoundingClientRect();
			const scale = window.devicePixelRatio;
			const next = [bounds.left, bounds.top, bounds.width, bounds.height].map(
				(value) => Math.round(value * scale),
			);
			const key = next.join(",");
			if (key !== previousBounds && next[2] > 0 && next[3] > 0) {
				previousBounds = key;
				window.plutoEditor.setViewportBounds({
					x: next[0],
					y: next[1],
					width: next[2],
					height: next[3],
				});
			}
			animationFrame = requestAnimationFrame(updateBounds);
		};
		updateBounds();
		return () => {
			cancelAnimationFrame(animationFrame);
			window.plutoEditor.setViewportVisible(false);
		};
	}, []);

	useEffect(() => {
		const updateVisibility = (): void =>
			window.plutoEditor.setViewportVisible(!document.hidden);
		document.addEventListener("visibilitychange", updateVisibility);
		updateVisibility();
		return () =>
			document.removeEventListener("visibilitychange", updateVisibility);
	}, []);

	const settings = editor?.viewportSettings;
	const compositeView =
		compositeViews.find((view) => view.value === settings?.debugView) ??
		compositeViews[0];
	const updateSettings = (
		changes: Partial<EditorState["viewportSettings"]>,
	): void => {
		if (settings)
			window.plutoEditor.setViewportSettings({ ...settings, ...changes });
	};
	const actions = editor ? (
		<>
			<span className="viewport-toolbar-caption">Composite</span>
			<select
				className="viewport-composite-select"
				aria-label="Viewport composite"
				title="Choose the viewport composite or diagnostic render output"
				value={settings?.debugView ?? 0}
				onChange={(event) =>
					updateSettings({ debugView: Number(event.currentTarget.value) })
				}
			>
				{compositeViews.map((view) => (
					<option key={view.value} value={view.value}>
						{view.label}
					</option>
				))}
			</select>
			<label>
				<input
					type="checkbox"
					checked={editor.editorCamera.gridVisible}
					onChange={(event) =>
						window.plutoEditor.setEditorCamera({
							...editor.editorCamera,
							gridVisible: event.currentTarget.checked,
						})
					}
				/>{" "}
				Grid
			</label>
			<label>
				<input
					type="checkbox"
					checked={settings?.debugShapes ?? true}
					onChange={(event) =>
						updateSettings({ debugShapes: event.currentTarget.checked })
					}
				/>{" "}
				Shapes
			</label>
			<label>
				<input
					type="checkbox"
					checked={settings?.snapEnabled ?? false}
					onChange={(event) =>
						updateSettings({ snapEnabled: event.currentTarget.checked })
					}
				/>{" "}
				Snap
			</label>
			{settings?.snapEnabled && (
				<>
					<input
						className="viewport-snap-input"
						type="number"
						title="Move snap"
						value={settings.translateSnap}
						min="0.001"
						step="0.1"
						onChange={(event) =>
							updateSettings({
								translateSnap: Number(event.currentTarget.value),
							})
						}
					/>
					<input
						className="viewport-snap-input"
						type="number"
						title="Rotate snap"
						value={settings.rotateSnap}
						min="0.1"
						step="1"
						onChange={(event) =>
							updateSettings({ rotateSnap: Number(event.currentTarget.value) })
						}
					/>
					<input
						className="viewport-snap-input"
						type="number"
						title="Scale snap"
						value={settings.scaleSnap}
						min="0.001"
						step="0.05"
						onChange={(event) =>
							updateSettings({ scaleSnap: Number(event.currentTarget.value) })
						}
					/>
				</>
			)}
		</>
	) : undefined;
	return (
		<PanelFrame
			id="viewport"
			title="Scene View"
			actions={actions}
			className="viewport-panel"
		>
			<div
				ref={viewport}
				className="viewport"
				aria-label="Engine viewport"
				onDragOver={(event) => {
					if (event.dataTransfer.types.includes("application/x-plutoge-asset"))
						event.preventDefault();
				}}
				onDrop={(event) => {
					const reference = event.dataTransfer.getData(
						"application/x-plutoge-asset",
					);
					if (reference) window.plutoEditor.instantiateAsset(reference);
				}}
			>
				<div className="viewport-overlay">
					<span>Perspective</span>
					<span>{compositeView.label}</span>
				</div>
				{host.status !== "ready" && (
					<div className="viewport-message">
						<strong>
							{host.status === "starting"
								? "Starting engine…"
								: "Viewport unavailable"}
						</strong>
						{host.message && <span>{host.message}</span>}
						{host.status === "error" && (
							<button onClick={() => void window.plutoEditor.restartHost()}>
								Restart host
							</button>
						)}
					</div>
				)}
			</div>
		</PanelFrame>
	);
}
