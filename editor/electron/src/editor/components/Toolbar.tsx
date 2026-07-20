import React, { useState } from "react";
import { entityPresets } from "../constants";
import { panelTitles, type PanelId } from "./PanelFrame";
import {
	PopupMenu,
	type PopupMenuItem,
	type PopupMenuState,
} from "./PopupMenu";

export function Toolbar({
	editor,
	running,
	showEditorCamera,
	visiblePanels,
	onToggleCamera,
	onProjectSettings,
	onTogglePanel,
	onResetLayout,
}: {
	editor?: EditorState;
	running: boolean;
	showEditorCamera: boolean;
	visiblePanels: Set<PanelId>;
	onToggleCamera(): void;
	onProjectSettings(): void;
	onTogglePanel(panel: PanelId): void;
	onResetLayout(): void;
}): React.JSX.Element {
	const [menu, setMenu] = useState<PopupMenuState>();
	const [activeMenu, setActiveMenu] = useState("");
	const closeMenu = (): void => {
		setMenu(undefined);
		setActiveMenu("");
	};
	const openMenu = (
		event: React.MouseEvent<HTMLButtonElement>,
		name: string,
		items: PopupMenuItem[],
	): void => {
		const bounds = event.currentTarget.getBoundingClientRect();
		if (activeMenu === name) {
			closeMenu();
			return;
		}
		setActiveMenu(name);
		setMenu({ x: bounds.left, y: bounds.bottom + 4, items });
	};

	const fileItems: PopupMenuItem[] = [
		{
			label: "New Project…",
			action: () => void window.plutoEditor.newProject(),
		},
		{
			label: "Open Project…",
			action: () => void window.plutoEditor.openProject(),
		},
		{
			label: "Save Project",
			disabled: !editor?.projectPath,
			action: () => void window.plutoEditor.saveProject(),
		},
		{
			label: "Project Settings…",
			disabled: !editor?.projectPath,
			action: onProjectSettings,
		},
		{
			label: "New Scene",
			separatorBefore: true,
			action: () => void window.plutoEditor.newScene(),
		},
		{ label: "Open Scene…", action: () => void window.plutoEditor.openScene() },
		{
			label: "Save Scene",
			shortcut: "Ctrl+S",
			action: () => void window.plutoEditor.saveScene(),
		},
		{
			label: "Save Scene As…",
			shortcut: "Ctrl+Shift+S",
			action: () => void window.plutoEditor.saveScene(true),
		},
	];
	const editItems: PopupMenuItem[] = [
		{
			label: "Undo",
			shortcut: "Ctrl+Z",
			disabled: !editor?.canUndo || running,
			action: () => window.plutoEditor.undo(),
		},
		{
			label: "Redo",
			shortcut: "Ctrl+Y",
			disabled: !editor?.canRedo || running,
			action: () => window.plutoEditor.redo(),
		},
		{
			label: "Duplicate Selected",
			shortcut: "Ctrl+D",
			disabled: !editor?.selectedEntityId || running,
			separatorBefore: true,
			action: () =>
				editor?.selectedEntityId &&
				window.plutoEditor.duplicateEntity(editor.selectedEntityId),
		},
		{
			label: "Delete Selected",
			shortcut: "Del",
			disabled: !editor?.selectedEntityId || running,
			danger: true,
			action: () =>
				editor?.selectedEntityId &&
				window.plutoEditor.deleteEntity(editor.selectedEntityId),
		},
	];
	const createItems: PopupMenuItem[] = entityPresets.map((name) => ({
		label: name,
		disabled: !editor || running,
		action: () => window.plutoEditor.createEntity(name),
	}));
	const viewItems: PopupMenuItem[] = (
		[
			"hierarchy",
			"viewport",
			"game",
			"inspector",
			"content",
			"performance",
		] as PanelId[]
	).map((panel) => ({
		label: `${visiblePanels.has(panel) ? "✓ " : ""}${panelTitles[panel]}`,
		action: () => onTogglePanel(panel),
	}));
	viewItems.push(
		{
			label: `${showEditorCamera ? "✓ " : ""}Editor Camera Settings`,
			disabled: !editor,
			separatorBefore: true,
			action: onToggleCamera,
		},
		{
			label: "Frame Selected",
			shortcut: "F",
			disabled: !editor?.selectedEntityId,
			action: () => window.plutoEditor.frameSelected(),
		},
		{
			label: "Reset Panel Layout",
			separatorBefore: true,
			action: onResetLayout,
		},
	);

	return (
		<header className="toolbar">
			<div className="brand">
				<span className="brand-mark">P</span>
				<span>PlutoGE</span>
			</div>
			<nav className="toolbar-menubar" aria-label="Editor menu">
				<button
					className={activeMenu === "file" ? "active" : ""}
					onClick={(event) => openMenu(event, "file", fileItems)}
				>
					File
				</button>
				<button
					className={activeMenu === "edit" ? "active" : ""}
					onClick={(event) => openMenu(event, "edit", editItems)}
				>
					Edit
				</button>
				<button
					className={activeMenu === "create" ? "active" : ""}
					onClick={(event) => openMenu(event, "create", createItems)}
				>
					Create
				</button>
				<button
					className={activeMenu === "view" ? "active" : ""}
					onClick={(event) => openMenu(event, "view", viewItems)}
				>
					View
				</button>
			</nav>
			<div className="toolbar-divider" />
			<div
				className="gizmo-toolbar"
				aria-label="Transform gizmo"
				title="Hold Ctrl while dragging to snap"
			>
				<button
					className={editor?.gizmoOperation === "translate" ? "active" : ""}
					disabled={!editor || running}
					title="Move tool (W)"
					onClick={() => window.plutoEditor.setGizmoOperation("translate")}
				>
					W&nbsp; Move
				</button>
				<button
					className={editor?.gizmoOperation === "rotate" ? "active" : ""}
					disabled={!editor || running}
					title="Rotate tool (E)"
					onClick={() => window.plutoEditor.setGizmoOperation("rotate")}
				>
					E&nbsp; Rotate
				</button>
				<button
					className={editor?.gizmoOperation === "scale" ? "active" : ""}
					disabled={!editor || running}
					title="Scale tool (R)"
					onClick={() => window.plutoEditor.setGizmoOperation("scale")}
				>
					R&nbsp; Scale
				</button>
				<button
					className="gizmo-space"
					disabled={!editor || running}
					title="Toggle local/world space"
					onClick={() =>
						window.plutoEditor.setGizmoSpace(
							editor?.gizmoSpace === "world" ? "local" : "world",
						)
					}
				>
					{editor?.gizmoSpace === "world" ? "World" : "Local"}
				</button>
			</div>
			<button
				className={`play-button ${running ? "running" : ""}`}
				disabled={!editor}
				onClick={() => window.plutoEditor.setRuntime(!running)}
			>
				{running ? "■ Stop" : "▶ Play"}
			</button>
			<PopupMenu
				menu={menu}
				onClose={closeMenu}
				className="toolbar-popup-menu"
			/>
		</header>
	);
}
