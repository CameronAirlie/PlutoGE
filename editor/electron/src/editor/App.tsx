import type React from "react";
import { useEffect, useMemo, useState } from "react";
import { DockWorkspace, useDockLayout } from "./components/DockWorkspace";
import { EditorOperationOverlay } from "./components/EditorOperationOverlay";
import { ProjectSettingsDialog } from "./components/ProjectSettingsDialog";
import { StatusBar } from "./components/StatusBar";
import { Toolbar } from "./components/Toolbar";
import { WindowTitleBar } from "./components/WindowTitleBar";

export function App(): React.JSX.Element {
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
	const [showEditorCamera, setShowEditorCamera] = useState(false);
	const [showProjectSettings, setShowProjectSettings] = useState(false);
	const dock = useDockLayout();
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

	useEffect(
		() =>
			window.plutoEditor.onAssetOpened((asset) => {
				if (asset) {
					// A menu action can activate this panel in the same event that
					// dismisses its portal. Clear transient panel-drag occlusion and
					// switch only after the current event has completed.
					window.plutoEditor.setViewportOccluded("panel-drag", false);
					window.setTimeout(() => dock.showPanel("asset"), 0);
				}
			}),
		[dock.showPanel],
	);

	useEffect(() => {
		const shortcuts = (event: KeyboardEvent): void => {
			const target = event.target as HTMLElement | null;
			const editingText =
				target?.matches('input, textarea, select, [contenteditable="true"]') ??
				false;
			if (!editingText && event.key === "F5" && editor) {
				event.preventDefault();
				window.plutoEditor.setRuntime(event.shiftKey ? false : !editor.running);
				return;
			}
			if (
				!editingText &&
				event.shiftKey &&
				event.key === "F1" &&
				editor?.running
			) {
				event.preventDefault();
				window.plutoEditor.setForceShowCursor(true);
				return;
			}
			if (!(event.ctrlKey || event.metaKey) || !editor) {
				if (
					!editingText &&
					(event.key === "Delete" || event.key === "Backspace") &&
					editor?.selectedEntityId &&
					!editor.running
				)
					window.plutoEditor.deleteEntity(editor.selectedEntityId);
				if (
					!editingText &&
					event.key.toLowerCase() === "f" &&
					editor?.selectedEntityId
				)
					window.plutoEditor.frameSelected();
				if (!editingText && !editor?.running && event.key.toLowerCase() === "w")
					window.plutoEditor.setGizmoOperation("translate");
				if (!editingText && !editor?.running && event.key.toLowerCase() === "e")
					window.plutoEditor.setGizmoOperation("rotate");
				if (!editingText && !editor?.running && event.key.toLowerCase() === "r")
					window.plutoEditor.setGizmoOperation("scale");
				if (!editingText && event.key === "Escape")
					window.plutoEditor.selectEntity(0);
				return;
			}
			if (event.key.toLowerCase() === "s") {
				event.preventDefault();
				void window.plutoEditor.saveScene(event.shiftKey);
			}
			if (event.key.toLowerCase() === "z") {
				event.preventDefault();
				event.shiftKey ? window.plutoEditor.redo() : window.plutoEditor.undo();
			}
			if (event.key.toLowerCase() === "y") {
				event.preventDefault();
				window.plutoEditor.redo();
			}
			if (
				event.key.toLowerCase() === "d" &&
				editor.selectedEntityId &&
				!editor.running
			) {
				event.preventDefault();
				window.plutoEditor.duplicateEntity(editor.selectedEntityId);
			}
			if (
				event.key.toLowerCase() === "c" &&
				editor.selectedEntityId &&
				!editor.running
			) {
				event.preventDefault();
				window.plutoEditor.copyEntity(editor.selectedEntityId);
			}
			if (event.key.toLowerCase() === "v" && !editor.running) {
				event.preventDefault();
				window.plutoEditor.pasteEntity();
			}
		};
		window.addEventListener("keydown", shortcuts);
		return () => window.removeEventListener("keydown", shortcuts);
	}, [editor]);

	const running = editor?.running ?? false;
	return (
		<div className="editor-shell" aria-busy={operation.busy}>
			<WindowTitleBar
				title={
					editor?.projectName
						? `${editor.projectName} — PlutoGE Editor`
						: "PlutoGE Editor"
				}
			/>
			<Toolbar
				editor={editor}
				running={running}
				showEditorCamera={showEditorCamera}
				visiblePanels={dock.visiblePanels}
				onToggleCamera={() => setShowEditorCamera((visible) => !visible)}
				onProjectSettings={() => setShowProjectSettings(true)}
				onTogglePanel={dock.togglePanel}
				onResetLayout={dock.reset}
			/>
			<DockWorkspace
				layout={dock.layout}
				onLayoutChange={dock.setLayout}
				onTogglePanel={dock.togglePanel}
				onDetachPanel={dock.detachPanel}
				dockHoverPanel={dock.dockHoverPanel}
				host={host}
				gameHost={gameHost}
				hostPerformance={hostPerformance}
				gameHostPerformance={gameHostPerformance}
				editor={editor}
				selectedEntity={selectedEntity}
				showEditorCamera={showEditorCamera}
			/>
			<StatusBar host={host} editor={editor} />
			{showProjectSettings && editor?.projectPath ? (
				<ProjectSettingsDialog
					key={editor.projectPath}
					editor={editor}
					onClose={() => setShowProjectSettings(false)}
				/>
			) : null}
			<EditorOperationOverlay operation={operation} />
		</div>
	);
}
