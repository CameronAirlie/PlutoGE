import type React from "react";

export function EditorOperationOverlay({
	operation,
}: {
	operation: EditorOperationState;
}): React.JSX.Element | null {
	if (!operation.busy) return null;
	return (
		<output className="editor-operation-overlay" aria-live="assertive">
			<div className="editor-operation-message">
				<span className="editor-operation-spinner" aria-hidden="true" />
				<strong>{operation.label || "Loading…"}</strong>
				<span>Editor controls will unlock when the native host finishes.</span>
			</div>
		</output>
	);
}
