import type React from "react";
import { useEffect, useState } from "react";

function formatElapsed(milliseconds: number): string {
	const totalSeconds = Math.max(0, Math.floor(milliseconds / 1000));
	const minutes = Math.floor(totalSeconds / 60);
	const seconds = totalSeconds % 60;
	return minutes > 0 ? `${minutes}m ${seconds.toString().padStart(2, "0")}s` : `${seconds}s`;
}

export function EditorOperationOverlay({
	operation,
}: {
	operation: EditorOperationState;
}): React.JSX.Element | null {
	const [now, setNow] = useState(Date.now());
	useEffect(() => {
		if (!operation.busy) return;
		setNow(Date.now());
		const timer = window.setInterval(() => setNow(Date.now()), 1000);
		return () => window.clearInterval(timer);
	}, [operation.busy, operation.startedAt]);

	if (!operation.busy) return null;
	const hasProgress = typeof operation.progress === "number";
	const progress = hasProgress
		? Math.max(0, Math.min(100, operation.progress ?? 0))
		: 0;
	const elapsed = operation.startedAt
		? formatElapsed(now - operation.startedAt)
		: undefined;
	return (
		<output className="editor-operation-overlay" aria-live="assertive">
			<div className="editor-operation-message">
				{hasProgress ? null : (
					<span className="editor-operation-spinner" aria-hidden="true" />
				)}
				<strong>{operation.label || "Loading…"}</strong>
				{operation.detail ? (
					<span className="editor-operation-detail">{operation.detail}</span>
				) : null}
				{hasProgress ? (
					<>
						<div
							className="editor-operation-progress"
							role="progressbar"
							aria-label={operation.detail || operation.label}
							aria-valuemin={0}
							aria-valuemax={100}
							aria-valuenow={Math.round(progress)}
						>
							<span style={{ width: `${progress}%` }} />
						</div>
						<div className="editor-operation-meta">
							<span>{Math.round(progress)}%</span>
							{elapsed ? <span>Elapsed {elapsed}</span> : null}
						</div>
					</>
				) : (
					<span>Editor controls will unlock when the native host finishes.</span>
				)}
				{operation.target ? (
					<span className="editor-operation-target" title={operation.target}>
						Output: {operation.target}
					</span>
				) : null}
			</div>
		</output>
	);
}
