import React from "react";

export type PanelId =
	| "hierarchy"
	| "viewport"
	| "game"
	| "inspector"
	| "content"
	| "console"
	| "asset"
	| "performance";

export const panelTitles: Record<PanelId, string> = {
	hierarchy: "Hierarchy",
	viewport: "Scene View",
	game: "Game View",
	inspector: "Inspector",
	content: "Content Browser",
	console: "Console",
	asset: "Asset Editor",
	performance: "Performance",
};

export function PanelFrame({
	id,
	title,
	actions,
	children,
	className = "",
}: {
	id: PanelId;
	title: string;
	actions?: React.ReactNode;
	children: React.ReactNode;
	className?: string;
}): React.JSX.Element {
	return (
		<section className={`dock-panel ${className}`} data-panel-id={id}>
			{actions && (
				<header className="dock-panel-toolbar" aria-label={`${title} actions`}>
					<div className="panel-actions">{actions}</div>
				</header>
			)}
			<div className="dock-panel-body">{children}</div>
		</section>
	);
}
