import type React from "react";
import type { PanelId } from "./PanelFrame";

export function WindowTitleBar({
	title,
	dockPanel,
}: {
	title: string;
	dockPanel?: PanelId;
}): React.JSX.Element {
	return (
		<header className="window-titlebar">
			<div className="window-titlebar-title">
				<span className="window-titlebar-mark" aria-hidden="true" />
				<span>{title}</span>
			</div>
			<div className="window-titlebar-controls">
				{dockPanel && (
					<button
						type="button"
						className="window-dock-button"
						title="Dock back into the editor"
						aria-label={`Dock ${title} back into the editor`}
						onClick={() => void window.plutoEditor.dockPanel(dockPanel)}
					>
						⇲
					</button>
				)}
				<button
					type="button"
					title="Minimize"
					aria-label="Minimize window"
					onClick={() => window.plutoEditor.minimizeWindow()}
				>
					—
				</button>
				<button
					type="button"
					title="Maximize or restore"
					aria-label="Maximize or restore window"
					onClick={() => window.plutoEditor.toggleMaximizeWindow()}
				>
					□
				</button>
				<button
					type="button"
					className="window-close-button"
					title="Close"
					aria-label="Close window"
					onClick={() => window.plutoEditor.closeWindow()}
				>
					×
				</button>
			</div>
		</header>
	);
}
