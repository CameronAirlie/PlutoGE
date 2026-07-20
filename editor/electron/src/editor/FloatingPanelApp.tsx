import type React from "react";
import { useEffect, useMemo, useState } from "react";
import { panelTitles, type PanelId } from "./components/PanelFrame";
import { WindowTitleBar } from "./components/WindowTitleBar";
import { ContentBrowserPanel } from "./panels/ContentBrowserPanel";
import { GameViewportPanel } from "./panels/GameViewportPanel";
import { HierarchyPanel } from "./panels/HierarchyPanel";
import { InspectorPanel } from "./panels/InspectorPanel";
import { PerformancePanel } from "./panels/PerformancePanel";
import { ViewportPanel } from "./panels/ViewportPanel";

export function FloatingPanelApp({
	panel,
}: {
	panel: PanelId;
}): React.JSX.Element {
	const [host, setHost] = useState<HostState>({ status: "starting" });
	const [gameHost, setGameHost] = useState<HostState>({ status: "starting" });
	const [hostPerformance, setHostPerformance] = useState<HostPerformance>();
	const [gameHostPerformance, setGameHostPerformance] =
		useState<HostPerformance>();
	const [editor, setEditor] = useState<EditorState>();
	const selectedEntity = useMemo(
		() =>
			editor?.entities.find((entity) => entity.id === editor.selectedEntityId),
		[editor],
	);

	useEffect(() => {
		const removeHostListener = window.plutoEditor.onHostState(setHost);
		const removeGameHostListener =
			window.plutoEditor.onGameHostState(setGameHost);
		const removeHostPerformanceListener =
			window.plutoEditor.onHostPerformance(setHostPerformance);
		const removeGameHostPerformanceListener =
			window.plutoEditor.onGameHostPerformance(setGameHostPerformance);
		const removeEditorListener = window.plutoEditor.onEditorState(setEditor);
		void window.plutoEditor.getHostState().then(setHost);
		void window.plutoEditor.getGameHostState().then(setGameHost);
		void window.plutoEditor.getHostPerformance().then((performance) => {
			if (performance) setHostPerformance(performance);
		});
		void window.plutoEditor.getGameHostPerformance().then((performance) => {
			if (performance) setGameHostPerformance(performance);
		});
		void window.plutoEditor.getEditorState().then((state) => {
			if (state) setEditor(state);
		});
		return () => {
			removeHostListener();
			removeGameHostListener();
			removeHostPerformanceListener();
			removeGameHostPerformanceListener();
			removeEditorListener();
		};
	}, []);

	let content: React.JSX.Element;
	switch (panel) {
		case "hierarchy":
			content = (
				<HierarchyPanel editor={editor} selectedEntity={selectedEntity} />
			);
			break;
		case "viewport":
			content = <ViewportPanel host={host} />;
			break;
		case "game":
			content = <GameViewportPanel host={gameHost} editor={editor} />;
			break;
		case "inspector":
			content = (
				<InspectorPanel
					editor={editor}
					selectedEntity={selectedEntity}
					showEditorCamera={false}
				/>
			);
			break;
		case "content":
			content = <ContentBrowserPanel editor={editor} />;
			break;
		case "performance":
			content = (
				<PerformancePanel scene={hostPerformance} game={gameHostPerformance} />
			);
			break;
	}

	return (
		<main className="floating-panel-shell">
			<WindowTitleBar title={panelTitles[panel]} dockPanel={panel} />
			<div className="floating-panel-content">{content}</div>
		</main>
	);
}
