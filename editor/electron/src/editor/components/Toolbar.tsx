import React, { useState } from "react";
import { entityPresets } from "../constants";
import { panelTitles, type PanelId } from "./PanelFrame";
import {
	PopupMenu,
	type PopupMenuItem,
	type PopupMenuState,
} from "./PopupMenu";
import { NameDialog } from "./NameDialog";

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
	const [forceShowCursor, setForceShowCursor] = useState(false);
	const [showCustomBake, setShowCustomBake] = useState(false);
	const [showNewScript, setShowNewScript] = useState(false);
	const [bakeSettings, setBakeSettings] = useState<SceneBakeSettings>({
		lightmapResolution: 64,
		lightmapTileSize: 16,
		probeDirectionCount: 8,
		indirectBounceSampleCount: 6,
		bakeIndirectBounce: true,
		probeBounceStrength: 0.65,
		lightmapBounceStrength: 0.75,
		bakeProbeVolume: true,
	});
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
			label: "Build Project…",
			disabled: !editor?.projectPath,
			action: () => void window.plutoEditor.buildProject(false),
		},
		{
			label: "Build and Run Project…",
			disabled: !editor?.projectPath,
			action: () => void window.plutoEditor.buildProject(true),
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
		{
			label: editor?.bakeRunning ? "Cancel Bake" : "Bake Scene",
			separatorBefore: true,
			disabled: !editor,
			children: editor?.bakeRunning
				? undefined
				: [
						{
							label: "Fast Preview",
							action: () => window.plutoEditor.bakeScene("fast"),
						},
						{
							label: "Balanced Preview",
							action: () => window.plutoEditor.bakeScene("balanced"),
						},
						{
							label: "Final",
							action: () => window.plutoEditor.bakeScene("final"),
						},
						{ label: "Custom…", action: () => setShowCustomBake(true) },
					],
			action: editor?.bakeRunning
				? () => window.plutoEditor.cancelBake()
				: undefined,
		},
		{
			label: "Exit",
			separatorBefore: true,
			action: () => window.plutoEditor.closeWindow(),
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
			"asset",
			"console",
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
	const runtimeItems: PopupMenuItem[] = [
		{
			label: running ? "Stop" : "Play",
			shortcut: running ? "Shift+F5" : "F5",
			disabled: !editor,
			action: () => window.plutoEditor.setRuntime(!running),
		},
		{
			label: `${forceShowCursor ? "✓ " : ""}Force Show Cursor`,
			shortcut: "Shift+F1",
			disabled: !running,
			action: () => {
				const next = !forceShowCursor;
				setForceShowCursor(next);
				window.plutoEditor.setForceShowCursor(next);
			},
		},
	];
	const scriptItems: PopupMenuItem[] = [
		{
			label: "New Script…",
			disabled: !editor?.projectPath,
			action: () => setShowNewScript(true),
		},
		{
			label: "Build Scripts",
			disabled: !editor?.projectPath,
			action: () => window.plutoEditor.buildScripts(),
		},
		{
			label: "Reload Script Assembly",
			disabled: !editor?.projectPath,
			action: () => window.plutoEditor.reloadScripts(),
		},
	];

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
				<button
					className={activeMenu === "runtime" ? "active" : ""}
					onClick={(event) => openMenu(event, "runtime", runtimeItems)}
				>
					Runtime
				</button>
				<button
					className={activeMenu === "scripts" ? "active" : ""}
					onClick={(event) => openMenu(event, "scripts", scriptItems)}
				>
					Scripts
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
			{showCustomBake && (
				<div
					className="dialog-backdrop"
					onMouseDown={() => setShowCustomBake(false)}
				>
					<div
						className="name-dialog custom-bake-dialog"
						onMouseDown={(event) => event.stopPropagation()}
					>
						<h3>Custom Scene Bake</h3>
						{(
							[
								"lightmapResolution",
								"lightmapTileSize",
								"probeDirectionCount",
								"indirectBounceSampleCount",
								"probeBounceStrength",
								"lightmapBounceStrength",
							] as const
						).map((field) => (
							<label key={field}>
								{field.replace(/([A-Z])/g, " $1")}
								<input
									type="number"
									value={bakeSettings[field]}
									onChange={(event) =>
										setBakeSettings({
											...bakeSettings,
											[field]: Number(event.currentTarget.value),
										})
									}
								/>
							</label>
						))}
						<label>
							<input
								type="checkbox"
								checked={bakeSettings.bakeIndirectBounce}
								onChange={(event) =>
									setBakeSettings({
										...bakeSettings,
										bakeIndirectBounce: event.currentTarget.checked,
									})
								}
							/>{" "}
							Bake indirect bounce
						</label>
						<label>
							<input
								type="checkbox"
								checked={bakeSettings.bakeProbeVolume}
								onChange={(event) =>
									setBakeSettings({
										...bakeSettings,
										bakeProbeVolume: event.currentTarget.checked,
									})
								}
							/>{" "}
							Bake probe volume
						</label>
						<div className="dialog-actions">
							<button onClick={() => setShowCustomBake(false)}>Cancel</button>
							<button
								onClick={() => {
									window.plutoEditor.bakeScene("custom", bakeSettings);
									setShowCustomBake(false);
								}}
							>
								Bake
							</button>
						</div>
					</div>
				</div>
			)}
			{showNewScript && (
				<NameDialog
					title="Create Script"
					onClose={() => setShowNewScript(false)}
					onConfirm={(name) => {
						window.plutoEditor.createScript(name);
						setShowNewScript(false);
					}}
				/>
			)}
		</header>
	);
}
