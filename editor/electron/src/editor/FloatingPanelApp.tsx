import type React from "react";
import { useEffect, useMemo, useState } from "react";
import { EditorOperationOverlay } from "./components/EditorOperationOverlay";
import { type PanelId, panelTitles } from "./components/PanelFrame";
import { WindowTitleBar } from "./components/WindowTitleBar";
import { ContentBrowserPanel } from "./panels/ContentBrowserPanel";
import { GameViewportPanel } from "./panels/GameViewportPanel";
import { HierarchyPanel } from "./panels/HierarchyPanel";
import { InspectorPanel } from "./panels/InspectorPanel";
import { PerformancePanel } from "./panels/PerformancePanel";
import { ViewportPanel } from "./panels/ViewportPanel";
import { ConsolePanel } from "./panels/ConsolePanel";
import { AssetEditorPanel } from "./panels/AssetEditorPanel";

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
	const [operation, setOperation] = useState<EditorOperationState>({
		busy: false,
		label: "",
	});
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
		const removeOperationListener =
			window.plutoEditor.onEditorOperation(setOperation);
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
		void window.plutoEditor.getEditorOperation().then(setOperation);
		return () => {
			removeHostListener();
			removeGameHostListener();
			removeHostPerformanceListener();
			removeGameHostPerformanceListener();
			removeEditorListener();
			removeOperationListener();
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
			content = <ViewportPanel host={host} editor={editor} />;
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
		case "console":
			content = <ConsolePanel />;
			break;
		case "asset":
			content = <AssetEditorPanel />;
			break;
		case "performance":
			content = (
				<PerformancePanel scene={hostPerformance} game={gameHostPerformance} />
			);
			break;
	}

	return (
		<main className="floating-panel-shell" aria-busy={operation.busy}>
			<WindowTitleBar title={panelTitles[panel]} dockPanel={panel} />
			<div className="floating-panel-content">{content}</div>
			<EditorOperationOverlay operation={operation} />
		</main>
	);
}
