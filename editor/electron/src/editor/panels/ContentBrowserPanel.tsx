import React, { useMemo, useState } from "react";
import { PanelFrame } from "../components/PanelFrame";
import {
	PopupMenu,
	type PopupMenuItem,
	type PopupMenuState,
} from "../components/PopupMenu";
import { NameDialog } from "../components/NameDialog";

const assetPath = (reference: string): string =>
	reference.replace(/^(project|engine):\/\//, "");
const parentFolder = (value: string): string =>
	value.includes("/") ? value.slice(0, value.lastIndexOf("/")) : "";
const assetName = (reference: string): string =>
	assetPath(reference).split("/").pop() ?? reference;
const formatSize = (bytes: number): string =>
	bytes < 1024
		? `${bytes} B`
		: bytes < 1048576
			? `${(bytes / 1024).toFixed(1)} KB`
			: `${(bytes / 1048576).toFixed(1)} MB`;
const assetIcon = (type: string): string =>
	({
		Model: "◆",
		Mesh: "⬡",
		Material: "◩",
		Texture: "▧",
		Scene: "◈",
		Audio: "♫",
		Script: "⌘",
		Animation: "▶",
	})[type] ?? "◇";

export function ContentBrowserPanel({
	editor,
}: {
	editor?: EditorState;
}): React.JSX.Element {
	const [folder, setFolder] = useState("");
	const [filter, setFilter] = useState("");
	const [selected, setSelected] = useState("");
	const [message, setMessage] = useState("");
	const [importing, setImporting] = useState(false);
	const [menu, setMenu] = useState<PopupMenuState>();
	type CreatableAsset =
		| "material"
		| "post-process"
		| "particle-system"
		| "shader-graph"
		| "animation-graph";
	const [createType, setCreateType] = useState<CreatableAsset>();
	const [showScriptableDialog, setShowScriptableDialog] = useState(false);
	const [scriptableName, setScriptableName] = useState("");
	const [scriptableClass, setScriptableClass] = useState("");
	const projectAssets = useMemo(
		() =>
			(editor?.assets ?? []).filter((asset) =>
				asset.reference.startsWith("project://"),
			),
		[editor?.assets],
	);

	const folders = useMemo(() => {
		const result = new Set<string>();
		for (const asset of projectAssets) {
			const relative = assetPath(asset.reference);
			if (folder && !relative.startsWith(`${folder}/`)) continue;
			const remainder = folder ? relative.slice(folder.length + 1) : relative;
			if (remainder.includes("/"))
				result.add(`${folder ? `${folder}/` : ""}${remainder.split("/")[0]}`);
		}
		return [...result].sort();
	}, [folder, projectAssets]);

	const visibleAssets = useMemo(
		() =>
			projectAssets.filter((asset) => {
				const relative = assetPath(asset.reference);
				const matchesFolder = filter
					? relative.toLowerCase().includes(filter.toLowerCase())
					: parentFolder(relative) === folder;
				return matchesFolder;
			}),
		[filter, folder, projectAssets],
	);

	const importModels = async (): Promise<void> => {
		setImporting(true);
		setMessage("");
		try {
			const result = await window.plutoEditor.importModels();
			const summary = result.imported.length
				? `Queued ${result.imported.length} model${result.imported.length === 1 ? "" : "s"} for the asset pipeline.`
				: "";
			setMessage([summary, ...result.warnings].filter(Boolean).join(" "));
			if (result.imported[0]) setSelected(result.imported[0]);
		} finally {
			setImporting(false);
		}
	};

	const actions = (
		<>
			<button
				disabled={!editor?.projectPath || importing}
				onClick={() => void importModels()}
			>
				{importing ? "Importing…" : "＋ Import 3D"}
			</button>
			<button
				disabled={!editor?.projectPath}
				title="Refresh assets"
				onClick={() => window.plutoEditor.refreshAssets()}
			>
				↻
			</button>
		</>
	);

	const copyReference = (reference: string): void => {
		void navigator.clipboard.writeText(reference).then(
			() => setMessage(`Copied ${reference}`),
			() => setMessage("Could not copy the asset reference."),
		);
	};

	const createAsset = (type: CreatableAsset, rawName: string): void => {
		const extension = (
			{
				material: ".plutomaterial",
				"post-process": ".plutopostprocess",
				"particle-system": ".plutoparticles",
				"shader-graph": ".plutoshadergraph",
				"animation-graph": ".plutoanimgraph",
			} as const
		)[type];
		const name = rawName
			.replace(/[^a-zA-Z0-9_-]+/g, "_")
			.replace(/^_+|_+$/g, "");
		if (!name) {
			setMessage("Enter a valid asset name.");
			return;
		}
		const reference = `project://${folder ? `${folder}/` : ""}${name}${extension}`;
		if (
			projectAssets.some(
				(asset) => asset.reference.toLowerCase() === reference.toLowerCase(),
			)
		) {
			setMessage(`${assetName(reference)} already exists.`);
			return;
		}
		window.plutoEditor.createAsset(type, reference);
		setCreateType(undefined);
		setSelected(reference);
		setMessage(`Creating ${reference}`);
	};

	const createItems: PopupMenuItem[] = [
		{ label: "Material", action: () => setCreateType("material") },
		{
			label: "Particle System",
			action: () => setCreateType("particle-system"),
		},
		{
			label: "Post Process Preset",
			action: () => setCreateType("post-process"),
		},
		{ label: "Shader Graph", action: () => setCreateType("shader-graph") },
		{
			label: "Animation Graph",
			action: () => setCreateType("animation-graph"),
		},
		{
			label: "Scriptable Object",
			disabled: !editor?.scriptableObjectClassNames.length,
			action: () => {
				setScriptableName("");
				setScriptableClass(editor?.scriptableObjectClassNames[0] ?? "");
				setShowScriptableDialog(true);
			},
		},
	];

	const openAssetContextMenu = (
		event: React.MouseEvent,
		asset: EditorAsset,
	): void => {
		event.preventDefault();
		event.stopPropagation();
		setSelected(asset.reference);
		const items: PopupMenuItem[] = [];
		items.push({
			label: asset.type === "Scene" ? "Open Scene" : "Open in Asset Editor",
			action: () => void window.plutoEditor.openAsset(asset.reference),
		});
		if (asset.type === "Model")
			items.push({
				label: "Add to Scene",
				action: () => window.plutoEditor.instantiateAsset(asset.reference),
			});
		items.push(
			{
				label: "Copy Reference",
				separatorBefore: items.length > 0,
				action: () => copyReference(asset.reference),
			},
			{
				label: "Show in Explorer",
				action: () => void window.plutoEditor.revealAsset(asset.reference),
			},
			{
				label: "Refresh Assets",
				separatorBefore: true,
				action: () => window.plutoEditor.refreshAssets(),
			},
		);
		setMenu({ x: event.clientX, y: event.clientY, items });
	};

	const openFolderContextMenu = (
		event: React.MouseEvent,
		path: string,
	): void => {
		event.preventDefault();
		event.stopPropagation();
		setSelected(`folder:${path}`);
		setMenu({
			x: event.clientX,
			y: event.clientY,
			items: [
				{ label: "Open Folder", action: () => setFolder(path) },
				{ label: "Create", separatorBefore: true, children: createItems },
				{
					label: "Refresh Assets",
					separatorBefore: true,
					action: () => window.plutoEditor.refreshAssets(),
				},
			],
		});
	};

	const openBackgroundContextMenu = (event: React.MouseEvent): void => {
		event.preventDefault();
		setMenu({
			x: event.clientX,
			y: event.clientY,
			items: [
				{ label: "Create", children: createItems },
				{
					label: "Import 3D Models…",
					disabled: importing,
					action: () => void importModels(),
				},
				{
					label: "Refresh Assets",
					action: () => window.plutoEditor.refreshAssets(),
				},
			],
		});
	};

	return (
		<PanelFrame
			id="content"
			title="Content Browser"
			actions={actions}
			className="content-browser-panel"
		>
			{!editor?.projectPath ? (
				<div className="empty-state content-empty">
					Open a project to browse and import assets.
				</div>
			) : (
				<>
					<div className="content-toolbar">
						<button
							disabled={!folder}
							onClick={() => setFolder(parentFolder(folder))}
						>
							←
						</button>
						<div className="breadcrumbs">
							<button onClick={() => setFolder("")}>Assets</button>
							{folder
								.split("/")
								.filter(Boolean)
								.map((part, index, parts) => (
									<React.Fragment key={`${part}-${index}`}>
										<span>/</span>
										<button
											onClick={() =>
												setFolder(parts.slice(0, index + 1).join("/"))
											}
										>
											{part}
										</button>
									</React.Fragment>
								))}
						</div>
						<input
							value={filter}
							onChange={(event) => setFilter(event.currentTarget.value)}
							placeholder="Search assets…"
						/>
					</div>
					{message && (
						<div className="import-message" role="status">
							{message}
						</div>
					)}
					<div className="asset-grid" onContextMenu={openBackgroundContextMenu}>
						{!filter &&
							folders.map((path) => (
								<button
									className={`asset-tile folder-tile ${selected === `folder:${path}` ? "selected" : ""}`}
									key={path}
									onDoubleClick={() => setFolder(path)}
									onClick={() => setSelected(`folder:${path}`)}
									onContextMenu={(event) => openFolderContextMenu(event, path)}
								>
									<span className="asset-icon">▰</span>
									<strong>{path.split("/").pop()}</strong>
									<small>Folder</small>
								</button>
							))}
						{visibleAssets.map((asset) => (
							<button
								className={`asset-tile ${selected === asset.reference ? "selected" : ""}`}
								key={asset.reference}
								title={
									asset.type === "Model"
										? "Double-click or drag into the Scene View to create an entity"
										: asset.reference
								}
								draggable={asset.type === "Model"}
								onDragStart={(event) =>
									event.dataTransfer.setData(
										"application/x-plutoge-asset",
										asset.reference,
									)
								}
								onClick={() => setSelected(asset.reference)}
								onContextMenu={(event) => openAssetContextMenu(event, asset)}
								onDoubleClick={() =>
									void window.plutoEditor.openAsset(asset.reference)
								}
							>
								<span className={`asset-icon type-${asset.type.toLowerCase()}`}>
									{assetIcon(asset.type)}
								</span>
								<strong>{assetName(asset.reference)}</strong>
								<small>
									{asset.type} · {formatSize(asset.size)}
								</small>
							</button>
						))}
						{!folders.length && !visibleAssets.length && (
							<div className="empty-state">No assets in this folder.</div>
						)}
					</div>
				</>
			)}
			<PopupMenu menu={menu} onClose={() => setMenu(undefined)} />
			{createType && (
				<NameDialog
					title={`Create ${createType.replaceAll("-", " ")}`}
					onClose={() => setCreateType(undefined)}
					onConfirm={(name) => createAsset(createType, name)}
				/>
			)}
			{showScriptableDialog && (
				<div
					className="dialog-backdrop"
					onMouseDown={() => setShowScriptableDialog(false)}
				>
					<form
						className="name-dialog"
						onMouseDown={(event) => event.stopPropagation()}
						onSubmit={(event) => {
							event.preventDefault();
							const name = scriptableName
								.replace(/[^a-zA-Z0-9_-]+/g, "_")
								.replace(/^_+|_+$/g, "");
							if (!name || !scriptableClass) return;
							const reference = `project://${folder ? `${folder}/` : ""}${name}.plutoscriptable`;
							window.plutoEditor.createAsset(
								"scriptable-object",
								reference,
								scriptableClass,
							);
							setSelected(reference);
							setMessage(`Creating ${reference}`);
							setShowScriptableDialog(false);
						}}
					>
						<h3>Create Scriptable Object</h3>
						<label>
							Name
							<input
								value={scriptableName}
								onChange={(event) =>
									setScriptableName(event.currentTarget.value)
								}
							/>
						</label>
						<label>
							Type
							<select
								value={scriptableClass}
								onChange={(event) =>
									setScriptableClass(event.currentTarget.value)
								}
							>
								{editor?.scriptableObjectClassNames.map((name) => (
									<option key={name} value={name}>
										{name}
									</option>
								))}
							</select>
						</label>
						<div className="dialog-actions">
							<button
								type="button"
								onClick={() => setShowScriptableDialog(false)}
							>
								Cancel
							</button>
							<button
								type="submit"
								disabled={!scriptableName.trim() || !scriptableClass}
							>
								Create
							</button>
						</div>
					</form>
				</div>
			)}
		</PanelFrame>
	);
}
