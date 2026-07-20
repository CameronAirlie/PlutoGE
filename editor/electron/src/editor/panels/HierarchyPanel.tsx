import React, { useState } from "react";
import { PanelFrame } from "../components/PanelFrame";
import {
	PopupMenu,
	type PopupMenuItem,
	type PopupMenuState,
} from "../components/PopupMenu";
import { entityPresets } from "../constants";
import { NameDialog } from "../components/NameDialog";

function HierarchyNode({
	entity,
	entities,
	selectedId,
	disabled,
	collapsed,
	onToggleCollapsed,
	openContextMenu,
}: {
	entity: EditorEntity;
	entities: EditorEntity[];
	selectedId: number;
	disabled: boolean;
	collapsed: Set<number>;
	onToggleCollapsed(id: number): void;
	openContextMenu(event: React.MouseEvent, entity: EditorEntity): void;
}): React.JSX.Element {
	const children = entities.filter(
		(candidate) => candidate.parentId === entity.id,
	);
	return (
		<div className="hierarchy-node">
			<button
				className={`tree-row ${selectedId === entity.id ? "selected" : ""} ${entity.active ? "" : "inactive"}`}
				draggable={!disabled}
				onDragStart={(event) =>
					event.dataTransfer.setData(
						"application/x-plutoge-entity",
						String(entity.id),
					)
				}
				onDragOver={(event) => event.preventDefault()}
				onDrop={(event) => {
					event.preventDefault();
					const childId = Number(
						event.dataTransfer.getData("application/x-plutoge-entity"),
					);
					if (!disabled && childId && childId !== entity.id)
						window.plutoEditor.reparentEntity(childId, entity.id);
				}}
				onClick={() => window.plutoEditor.selectEntity(entity.id)}
				onContextMenu={(event) => openContextMenu(event, entity)}
			>
				<span
					onClick={(event) => {
						event.stopPropagation();
						if (children.length) onToggleCollapsed(entity.id);
					}}
				>
					{children.length ? (collapsed.has(entity.id) ? "▸" : "▾") : "◇"}
				</span>
				{entity.name || `Entity ${entity.id}`}
			</button>
			{children.length > 0 && !collapsed.has(entity.id) && (
				<div className="tree-children">
					{children.map((child) => (
						<HierarchyNode
							key={child.id}
							entity={child}
							entities={entities}
							selectedId={selectedId}
							disabled={disabled}
							collapsed={collapsed}
							onToggleCollapsed={onToggleCollapsed}
							openContextMenu={openContextMenu}
						/>
					))}
				</div>
			)}
		</div>
	);
}

export function HierarchyPanel({
	editor,
	selectedEntity,
}: {
	editor?: EditorState;
	selectedEntity?: EditorEntity;
}): React.JSX.Element {
	const [menu, setMenu] = useState<PopupMenuState>();
	const [collapsed, setCollapsed] = useState<Set<number>>(new Set());
	const [renameEntity, setRenameEntity] = useState<EditorEntity>();
	const running = editor?.running ?? false;
	const roots =
		editor?.entities.filter((entity) => entity.parentId === 0) ?? [];
	const createItems = (parentId: number): PopupMenuItem[] =>
		entityPresets.map((name) => ({
			label: name,
			disabled: running || !editor,
			action: () => window.plutoEditor.createEntity(name, parentId),
		}));
	const openContextMenu = (
		event: React.MouseEvent,
		entity?: EditorEntity,
	): void => {
		event.preventDefault();
		event.stopPropagation();
		if (entity) window.plutoEditor.selectEntity(entity.id);
		const items: PopupMenuItem[] = entity
			? [
					{ label: "Rename", action: () => setRenameEntity(entity) },
					{
						label: "Copy",
						shortcut: "Ctrl+C",
						disabled: running,
						action: () => window.plutoEditor.copyEntity(entity.id),
					},
					{
						label: "Create Child",
						disabled: running,
						children: createItems(entity.id),
					},
					{
						label: "Duplicate",
						shortcut: "Ctrl+D",
						disabled: running,
						action: () => window.plutoEditor.duplicateEntity(entity.id),
					},
					{
						label: "Frame Selected",
						shortcut: "F",
						action: () => window.plutoEditor.frameSelected(),
					},
					{
						label: entity.active ? "Disable" : "Enable",
						disabled: running,
						action: () =>
							window.plutoEditor.setEntityActive(entity.id, !entity.active),
					},
					{
						label: "Save As Prefab",
						disabled: running || !editor?.projectPath,
						action: () => window.plutoEditor.saveEntityAsPrefab(entity.id),
					},
					{
						label: "Create Skeleton Attachments",
						disabled:
							running ||
							!entity.components.some(
								(component) => component.type === "MeshComponent",
							),
						action: () =>
							window.plutoEditor.createSkeletonAttachments(entity.id),
					},
					...(entity.parentId
						? [
								{
									label: "Move to Root",
									disabled: running,
									action: () => window.plutoEditor.reparentEntity(entity.id, 0),
								} satisfies PopupMenuItem,
							]
						: []),
					{
						label: "Delete",
						shortcut: "Del",
						disabled: running,
						danger: true,
						separatorBefore: true,
						action: () => window.plutoEditor.deleteEntity(entity.id),
					},
				]
			: [
					{
						label: "Paste",
						shortcut: "Ctrl+V",
						disabled: running || !editor,
						action: () => window.plutoEditor.pasteEntity(),
					},
					{
						label: "Create",
						disabled: running || !editor,
						children: createItems(0),
					},
				];
		setMenu({ x: event.clientX, y: event.clientY, items });
	};
	const create = (
		<select
			disabled={running || !editor}
			value=""
			aria-label="Create entity"
			onChange={(event) => {
				if (event.currentTarget.value)
					window.plutoEditor.createEntity(event.currentTarget.value, 0);
			}}
		>
			<option value="">＋ Create</option>
			{entityPresets.map((name) => (
				<option key={name}>{name}</option>
			))}
		</select>
	);

	return (
		<PanelFrame
			id="hierarchy"
			title="Hierarchy"
			actions={create}
			className="hierarchy-panel"
		>
			<div className="scene-label">
				⌕ {editor?.projectName ? `${editor.projectName} · ` : ""}
				{editor?.scenePath
					? editor.scenePath.split(/[\\/]/).pop()
					: "Untitled Scene"}
				{editor?.dirty ? " •" : ""}
			</div>
			<div
				className="hierarchy-root"
				onContextMenu={(event) => openContextMenu(event)}
				onDragOver={(event) => event.preventDefault()}
				onDrop={(event) => {
					const id = Number(
						event.dataTransfer.getData("application/x-plutoge-entity"),
					);
					if (id && !running) window.plutoEditor.reparentEntity(id, 0);
				}}
			>
				{roots.map((entity) => (
					<HierarchyNode
						key={entity.id}
						entity={entity}
						entities={editor?.entities ?? []}
						selectedId={editor?.selectedEntityId ?? 0}
						disabled={running}
						collapsed={collapsed}
						onToggleCollapsed={(id) =>
							setCollapsed((current) => {
								const next = new Set(current);
								next.has(id) ? next.delete(id) : next.add(id);
								return next;
							})
						}
						openContextMenu={openContextMenu}
					/>
				))}
				{!roots.length && (
					<div className="empty-state">Create an entity to begin.</div>
				)}
			</div>
			<button
				className="delete-entity"
				disabled={!selectedEntity || running}
				onClick={() =>
					selectedEntity && window.plutoEditor.deleteEntity(selectedEntity.id)
				}
			>
				Delete selected
			</button>
			<PopupMenu menu={menu} onClose={() => setMenu(undefined)} />
			{renameEntity && (
				<NameDialog
					title="Rename Entity"
					initialValue={renameEntity.name}
					onClose={() => setRenameEntity(undefined)}
					onConfirm={(name) => {
						window.plutoEditor.setEntityName(renameEntity.id, name);
						setRenameEntity(undefined);
					}}
				/>
			)}
		</PanelFrame>
	);
}
