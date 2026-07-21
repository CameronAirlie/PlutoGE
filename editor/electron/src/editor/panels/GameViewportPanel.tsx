import React from "react";
import { EngineViewportCanvas } from "../components/EngineViewportCanvas";
import { PanelFrame } from "../components/PanelFrame";

export function GameViewportPanel({
	host,
	editor,
}: {
	host: HostState;
	editor?: EditorState;
}): React.JSX.Element {
	const hasCamera =
		editor?.entities.some(
			(entity) =>
				entity.active &&
				entity.components.some(
					(component) =>
						component.type === "CameraComponent" && component.enabled,
				),
		) ?? false;

	return (
		<PanelFrame
			id="game"
			title="Game View"
			className="viewport-panel game-viewport-panel"
		>
			<div
				className="viewport"
				aria-label="Game camera viewport"
			>
				<EngineViewportCanvas kind="game" />
				<div className="viewport-overlay">
					<span>Game Camera</span>
					<span>Rendered</span>
				</div>
				{host.status !== "ready" && (
					<div className="viewport-message">
						<strong>
							{host.status === "starting"
								? "Starting game view…"
								: "Game view unavailable"}
						</strong>
						{host.message && <span>{host.message}</span>}
						{host.status === "error" && (
							<button onClick={() => void window.plutoEditor.restartGameHost()}>
								Restart game view
							</button>
						)}
					</div>
				)}
				{host.status === "ready" && !hasCamera && (
					<div className="viewport-message">
						<strong>No active game camera</strong>
						<span>
							Add or enable a Camera component to render the Game View.
						</span>
					</div>
				)}
			</div>
		</PanelFrame>
	);
}
