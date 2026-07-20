import { app, BrowserWindow, dialog, ipcMain, screen, shell } from "electron";
import fs from "node:fs";
import path from "node:path";
import { NativeHost } from "./native-host";

let mainWindow: BrowserWindow | undefined;
let nativeHost: NativeHost | undefined;
let gameHost: NativeHost | undefined;
type PanelId =
	| "hierarchy"
	| "viewport"
	| "game"
	| "inspector"
	| "content"
	| "performance";
const panelTitles: Record<PanelId, string> = {
	hierarchy: "Hierarchy",
	viewport: "Scene View",
	game: "Game View",
	inspector: "Inspector",
	content: "Content Browser",
	performance: "Performance",
};
const isPanelId = (value: unknown): value is PanelId =>
	typeof value === "string" &&
	Object.prototype.hasOwnProperty.call(panelTitles, value);
const editorWindows = new Set<BrowserWindow>();
const panelWindows = new Map<PanelId, BrowserWindow>();
type NativeSurface = {
	bounds?: [number, number, number, number];
	visible: boolean;
	owner?: BrowserWindow;
};
const sceneSurface: NativeSurface = { visible: false };
const gameSurface: NativeSurface = { visible: false };
const viewportOcclusions = new Set<string>();
let mirroredProjectPath = "";
let mirroredScenePath = "";

const isTrustedSender = (
	event: Electron.IpcMainEvent | Electron.IpcMainInvokeEvent,
): boolean =>
	[...editorWindows].some(
		(window) =>
			!window.isDestroyed() &&
			!window.webContents.isDestroyed() &&
			event.sender.id === window.webContents.id,
	);

const senderWindow = (
	event: Electron.IpcMainEvent | Electron.IpcMainInvokeEvent,
): BrowserWindow | undefined => {
	const window = BrowserWindow.fromWebContents(event.sender) ?? undefined;
	return window && editorWindows.has(window) ? window : undefined;
};

const sendToEditorWindows = (channel: string, value: unknown): void => {
	for (const window of editorWindows) {
		if (!window.isDestroyed() && !window.webContents.isDestroyed())
			window.webContents.send(channel, value);
	}
};

const syncSurface = (
	host: NativeHost | undefined,
	surface: NativeSurface,
): void => {
	const owner = surface.owner;
	if (!surface.bounds || !owner || owner.isDestroyed()) {
		host?.send("visible 0");
		return;
	}
	const windowCanShowViewport = Boolean(
		owner.isVisible() && !owner.isMinimized(),
	);
	const shouldShow =
		surface.visible && viewportOcclusions.size === 0 && windowCanShowViewport;
	if (!shouldShow) {
		// Avoid moving an owned native window while Windows is minimizing or
		// hiding its Electron owner. Cross-process SetWindowPos calls during that
		// transition can stall the owner's window-state change.
		host?.send("visible 0");
		return;
	}

	host?.setOwnerWindow(owner);
	host?.send(`bounds ${surface.bounds.join(" ")}`);
	host?.send("visible 1");
};

const syncViewports = (): void => {
	syncSurface(nativeHost, sceneSurface);
	syncSurface(gameHost, gameSurface);
};

const clearWindowOcclusions = (webContentsId: number): void => {
	const prefix = `${webContentsId}:`;
	for (const token of viewportOcclusions) {
		if (token.startsWith(prefix)) viewportOcclusions.delete(token);
	}
};

const wireEditorWindow = (window: BrowserWindow): void => {
	// BrowserWindow.webContents cannot be read safely from a `closed` callback.
	// Capture the stable ID while the window is still alive.
	const webContentsId = window.webContents.id;
	editorWindows.add(window);
	window.on("move", syncViewports);
	window.on("minimize", syncViewports);
	window.on("restore", syncViewports);
	window.on("hide", syncViewports);
	window.on("show", syncViewports);
	window.on("closed", () => {
		clearWindowOcclusions(webContentsId);
		editorWindows.delete(window);
		if (sceneSurface.owner === window) {
			sceneSurface.owner = undefined;
			sceneSurface.bounds = undefined;
			sceneSurface.visible = false;
		}
		if (gameSurface.owner === window) {
			gameSurface.owner = undefined;
			gameSurface.bounds = undefined;
			gameSurface.visible = false;
		}
		syncViewports();
	});
};

const createPanelWindow = (
	panel: PanelId,
	position?: { x: number; y: number },
): BrowserWindow => {
	const existing = panelWindows.get(panel);
	if (existing && !existing.isDestroyed()) {
		existing.show();
		existing.focus();
		return existing;
	}

	const viewport = panel === "viewport" || panel === "game";
	const panelWindow = new BrowserWindow({
		x: position?.x,
		y: position?.y,
		width: viewport ? 900 : 480,
		height: viewport ? 650 : 620,
		minWidth: viewport ? 480 : 280,
		minHeight: viewport ? 320 : 240,
		show: false,
		frame: false,
		backgroundColor: "#101319",
		title: `${panelTitles[panel]} — PlutoGE`,
		webPreferences: {
			preload: MAIN_WINDOW_PRELOAD_WEBPACK_ENTRY,
			contextIsolation: true,
			nodeIntegration: false,
			sandbox: true,
		},
	});
	panelWindows.set(panel, panelWindow);
	wireEditorWindow(panelWindow);
	let dockHovered = false;
	let manualMove = false;
	const updateDockHover = (): boolean => {
		const editorWindow = mainWindow;
		const point = screen.getCursorScreenPoint();
		const bounds = editorWindow?.getBounds();
		const hovered = Boolean(
			editorWindow &&
				!editorWindow.isDestroyed() &&
				editorWindow.isVisible() &&
				!editorWindow.isMinimized() &&
				bounds &&
				point.x >= bounds.x &&
				point.x < bounds.x + bounds.width &&
				point.y >= bounds.y &&
				point.y < bounds.y + bounds.height,
		);
		if (hovered !== dockHovered) {
			dockHovered = hovered;
			if (
				editorWindow &&
				!editorWindow.isDestroyed() &&
				!editorWindow.webContents.isDestroyed()
			)
				editorWindow.webContents.send("panel:dock-hover", panel, hovered);
		}
		return hovered;
	};
	panelWindow.removeMenu();
	const url = new URL(MAIN_WINDOW_WEBPACK_ENTRY);
	url.searchParams.set("panel", panel);
	void panelWindow.loadURL(url.toString());
	panelWindow.once("ready-to-show", () => panelWindow.show());
	panelWindow.on("will-move", () => {
		manualMove = true;
		updateDockHover();
	});
	panelWindow.on("move", () => {
		if (manualMove) updateDockHover();
	});
	panelWindow.on("moved", () => {
		const shouldDock =
			process.platform === "win32" && manualMove && updateDockHover();
		manualMove = false;
		if (shouldDock) panelWindow.close();
	});
	panelWindow.on("close", () => {
		if (
			dockHovered &&
			mainWindow &&
			!mainWindow.isDestroyed() &&
			!mainWindow.webContents.isDestroyed()
		)
			mainWindow.webContents.send("panel:dock-hover", panel, false);
		dockHovered = false;
	});
	panelWindow.on("closed", () => {
		if (panelWindows.get(panel) !== panelWindow) return;
		panelWindows.delete(panel);
		if (
			mainWindow &&
			!mainWindow.isDestroyed() &&
			!mainWindow.webContents.isDestroyed()
		)
			mainWindow.webContents.send("panel:closed", panel);
	});
	return panelWindow;
};

const createWindow = (): void => {
	viewportOcclusions.clear();
	mirroredProjectPath = "";
	mirroredScenePath = "";
	sceneSurface.bounds = undefined;
	sceneSurface.visible = false;
	sceneSurface.owner = undefined;
	gameSurface.bounds = undefined;
	gameSurface.visible = false;
	gameSurface.owner = undefined;
	const editorWindow = new BrowserWindow({
		width: 1440,
		height: 900,
		minWidth: 960,
		minHeight: 640,
		frame: false,
		backgroundColor: "#101319",
		title: "PlutoGE Editor",
		webPreferences: {
			preload: MAIN_WINDOW_PRELOAD_WEBPACK_ENTRY,
			contextIsolation: true,
			nodeIntegration: false,
			sandbox: true,
		},
	});
	mainWindow = editorWindow;
	wireEditorWindow(editorWindow);

	editorWindow.removeMenu();
	void editorWindow.loadURL(MAIN_WINDOW_WEBPACK_ENTRY);

	nativeHost = new NativeHost(
		editorWindow,
		(state) => {
			sendToEditorWindows("host:state", state);
			if (state.status === "ready") syncSurface(nativeHost, sceneSurface);
		},
		(state) => {
			if (gameHost?.getState().status === "ready") {
				if (state.projectPath !== mirroredProjectPath) {
					if (state.projectPath)
						gameHost.send(`load_project ${encode(state.projectPath)}`);
					else gameHost.send("new_scene");
					mirroredProjectPath = state.projectPath;
					mirroredScenePath = state.scenePath;
				} else if (state.scenePath !== mirroredScenePath) {
					gameHost.send(
						state.scenePath
							? `load_scene ${encode(state.scenePath)}`
							: "new_scene",
					);
					mirroredScenePath = state.scenePath;
				}
			}
			sendToEditorWindows("editor:state", state);
		},
		(performance) => {
			sendToEditorWindows("host:performance", performance);
		},
	);
	gameHost = new NativeHost(
		editorWindow,
		(state) => {
			sendToEditorWindows("game-host:state", state);
			if (state.status === "ready") {
				mirroredProjectPath = "";
				mirroredScenePath = "";
				syncSurface(gameHost, gameSurface);
				const editorState = nativeHost?.getEditorState();
				if (editorState?.projectPath) {
					gameHost?.send(`load_project ${encode(editorState.projectPath)}`);
					if (editorState.scenePath)
						gameHost?.send(`load_scene ${encode(editorState.scenePath)}`);
					mirroredProjectPath = editorState.projectPath;
					mirroredScenePath = editorState.scenePath;
				}
			}
		},
		() => {
			/* The primary host owns editor state. */
		},
		(performance) => {
			sendToEditorWindows("game-host:performance", performance);
		},
		["--view-mode", "game"],
		"PlutoGEGameViewHost",
	);
	editorWindow.webContents.once("did-finish-load", () => {
		nativeHost?.start();
		gameHost?.start();
	});
	editorWindow.webContents.on("did-finish-load", () => {
		viewportOcclusions.clear();
		syncViewports();
	});
	editorWindow.on("close", () => {
		for (const panelWindow of panelWindows.values()) {
			if (!panelWindow.isDestroyed()) panelWindow.close();
		}
	});
	editorWindow.on("closed", () => {
		if (mainWindow === editorWindow) mainWindow = undefined;
	});
};

ipcMain.on("viewport:set-bounds", (event, value: unknown) => {
	const owner = senderWindow(event);
	if (!owner || !value || typeof value !== "object") return;
	const bounds = value as Record<string, unknown>;
	const numbers = ["x", "y", "width", "height"].map((key) =>
		Number(bounds[key]),
	);
	if (!numbers.every(Number.isFinite)) return;
	const [x, y, width, height] = numbers.map(Math.round);
	if (width <= 0 || height <= 0) return;
	sceneSurface.bounds = [x, y, width, height];
	sceneSurface.owner = owner;
	syncSurface(nativeHost, sceneSurface);
});

ipcMain.on("viewport:set-visible", (event, visible: unknown) => {
	const owner = senderWindow(event);
	if (
		owner &&
		typeof visible === "boolean" &&
		(visible || sceneSurface.owner === owner)
	) {
		if (visible) sceneSurface.owner = owner;
		sceneSurface.visible = visible;
		syncSurface(nativeHost, sceneSurface);
	}
});

ipcMain.on("game-viewport:set-bounds", (event, value: unknown) => {
	const owner = senderWindow(event);
	if (!owner || !value || typeof value !== "object") return;
	const bounds = value as Record<string, unknown>;
	const numbers = ["x", "y", "width", "height"].map((key) =>
		Number(bounds[key]),
	);
	if (!numbers.every(Number.isFinite)) return;
	const [x, y, width, height] = numbers.map(Math.round);
	if (width <= 0 || height <= 0) return;
	gameSurface.bounds = [x, y, width, height];
	gameSurface.owner = owner;
	syncSurface(gameHost, gameSurface);
});

ipcMain.on("game-viewport:set-visible", (event, visible: unknown) => {
	const owner = senderWindow(event);
	if (
		owner &&
		typeof visible === "boolean" &&
		(visible || gameSurface.owner === owner)
	) {
		if (visible) gameSurface.owner = owner;
		gameSurface.visible = visible;
		syncSurface(gameHost, gameSurface);
	}
});

ipcMain.on(
	"viewport:set-occluded",
	(event, token: unknown, occluded: unknown) => {
		if (
			!isTrustedSender(event) ||
			typeof token !== "string" ||
			!token ||
			token.length > 128 ||
			typeof occluded !== "boolean"
		)
			return;
		const key = `${event.sender.id}:${token}`;
		if (occluded) viewportOcclusions.add(key);
		else viewportOcclusions.delete(key);
		syncViewports();
	},
);

ipcMain.handle("panel:detach", (event, panel: unknown, value: unknown) => {
	if (!isTrustedSender(event) || !isPanelId(panel)) return false;
	const coordinates =
		value && typeof value === "object"
			? (value as Record<string, unknown>)
			: undefined;
	const x = Number(coordinates?.x);
	const y = Number(coordinates?.y);
	createPanelWindow(panel, {
		x: Number.isFinite(x) ? Math.round(x - 80) : 100,
		y: Number.isFinite(y) ? Math.round(y - 16) : 100,
	});
	return true;
});

ipcMain.handle("panel:dock", (event, panel: unknown) => {
	if (!isTrustedSender(event) || !isPanelId(panel)) return;
	panelWindows.get(panel)?.close();
});

ipcMain.on("window:control", (event, action: unknown) => {
	const window = senderWindow(event);
	if (!window || typeof action !== "string") return;
	switch (action) {
		case "minimize":
			window.minimize();
			break;
		case "maximize":
			window.isMaximized() ? window.unmaximize() : window.maximize();
			break;
		case "close":
			window.close();
			break;
	}
});

ipcMain.handle("host:restart", async (event) => {
	if (!isTrustedSender(event)) return;
	await nativeHost?.restart();
});

ipcMain.handle("host:get-state", (event) => {
	if (!isTrustedSender(event))
		return { status: "error", message: "Untrusted IPC sender." };
	return nativeHost?.getState() ?? { status: "stopped" };
});

ipcMain.handle("host:get-performance", (event) => {
	if (!isTrustedSender(event)) return undefined;
	return nativeHost?.getPerformance();
});

ipcMain.handle("game-host:restart", async (event) => {
	if (!isTrustedSender(event)) return;
	await gameHost?.restart();
});

ipcMain.handle("game-host:get-state", (event) => {
	if (!isTrustedSender(event))
		return { status: "error", message: "Untrusted IPC sender." };
	return gameHost?.getState() ?? { status: "stopped" };
});

ipcMain.handle("game-host:get-performance", (event) => {
	if (!isTrustedSender(event)) return undefined;
	return gameHost?.getPerformance();
});

// Prefix encoded arguments so even an empty string remains a token in the
// host's whitespace-delimited command protocol.
const encode = (value: unknown): string =>
	`~${encodeURIComponent(String(value))}`;
const sendEditorCommand = (command: string): void => {
	nativeHost?.send(command);
	const name = command.split(" ", 1)[0];
	const primaryOnly = new Set([
		"load_project",
		"create_project",
		"load_scene",
		"save_scene",
		"save_project",
		"refresh_assets",
		"editor_effect_save_preset",
		"camera_effect_save_preset",
		// Selection and editor-camera state belong only to the interactive Scene
		// View. Scene mutations are mirrored to keep the Game View's scene copy
		// current, including unsaved camera and component changes.
		"select",
		"gizmo_operation",
		"gizmo_space",
		"set_editor_camera",
		"reset_editor_camera",
		"frame_selected",
		"editor_effect_add",
		"editor_effect_remove",
		"editor_effect_move",
		"editor_effect_enabled",
		"editor_effect_parameter",
		"editor_effect_preset",
	]);
	if (!primaryOnly.has(name)) gameHost?.send(command);
};
const confirmDiscardUnsavedChanges = async (): Promise<boolean> => {
	if (!mainWindow || !nativeHost?.getEditorState()?.dirty) return true;
	const result = await dialog.showMessageBox(mainWindow, {
		type: "warning",
		title: "Unsaved scene",
		message: "Discard unsaved scene changes?",
		detail: "This action cannot be undone.",
		buttons: ["Cancel", "Discard"],
		defaultId: 0,
		cancelId: 0,
	});
	return result.response === 1;
};

ipcMain.handle("editor:get-state", (event) => {
	if (!isTrustedSender(event)) return undefined;
	return nativeHost?.getEditorState();
});

ipcMain.handle("editor:new-scene", async (event) => {
	if (isTrustedSender(event) && (await confirmDiscardUnsavedChanges()))
		sendEditorCommand("new_scene");
});

ipcMain.handle("editor:new-project", async (event) => {
	if (!isTrustedSender(event) || !mainWindow) return;
	if (!(await confirmDiscardUnsavedChanges())) return;
	const result = await dialog.showSaveDialog(mainWindow, {
		title: "Create PlutoGE Project",
		defaultPath: path.join(
			app.getPath("documents"),
			"NewProject",
			"NewProject.plutoproject",
		),
		buttonLabel: "Create Project",
		filters: [{ name: "PlutoGE Project", extensions: ["plutoproject"] }],
	});
	if (result.canceled || !result.filePath) return;
	const projectName = path.basename(
		result.filePath,
		path.extname(result.filePath),
	);
	sendEditorCommand(
		`create_project ${encode(result.filePath)} ${encode(projectName)}`,
	);
});

ipcMain.handle("editor:open-project", async (event) => {
	if (!isTrustedSender(event) || !mainWindow) return;
	if (!(await confirmDiscardUnsavedChanges())) return;
	const result = await dialog.showOpenDialog(mainWindow, {
		title: "Open PlutoGE Project",
		properties: ["openFile"],
		filters: [{ name: "PlutoGE Project", extensions: ["plutoproject"] }],
	});
	if (!result.canceled && result.filePaths[0])
		sendEditorCommand(`load_project ${encode(result.filePaths[0])}`);
});

ipcMain.handle("editor:open-scene", async (event) => {
	if (!isTrustedSender(event) || !mainWindow) return;
	if (!(await confirmDiscardUnsavedChanges())) return;
	const result = await dialog.showOpenDialog(mainWindow, {
		title: "Open PlutoGE Scene",
		properties: ["openFile"],
		filters: [{ name: "PlutoGE Scene", extensions: ["plutoscene"] }],
	});
	if (!result.canceled && result.filePaths[0])
		sendEditorCommand(`load_scene ${encode(result.filePaths[0])}`);
});

ipcMain.handle("editor:save-scene", async (event, saveAs: unknown) => {
	if (!isTrustedSender(event) || !mainWindow) return;
	const state = nativeHost?.getEditorState();
	let scenePath = saveAs === true ? "" : (state?.scenePath ?? "");
	if (!scenePath) {
		const defaultName = `${state?.projectName?.replace(/[^a-zA-Z0-9._-]+/g, "_") || "Untitled"}.plutoscene`;
		const result = await dialog.showSaveDialog(mainWindow, {
			title: "Save PlutoGE Scene",
			defaultPath: state?.assetDirectoryPath
				? path.join(state.assetDirectoryPath, "Scenes", defaultName)
				: defaultName,
			filters: [{ name: "PlutoGE Scene", extensions: ["plutoscene"] }],
		});
		if (result.canceled || !result.filePath) return;
		scenePath = result.filePath;
	}
	sendEditorCommand(`save_scene ${encode(scenePath)}`);
});

const isPathInside = (parent: string, candidate: string): boolean => {
	const relative = path.relative(parent, candidate);
	return (
		Boolean(relative) &&
		!relative.startsWith("..") &&
		!path.isAbsolute(relative)
	);
};

const resolveProjectAsset = (
	reference: unknown,
): { asset: EditorAsset; filePath: string } | undefined => {
	if (typeof reference !== "string" || !reference.startsWith("project://"))
		return undefined;
	const state = nativeHost?.getEditorState();
	const assetRoot = state?.assetDirectoryPath;
	const asset = state?.assets.find(
		(candidate) => candidate.reference === reference,
	);
	if (!assetRoot || !asset) return undefined;
	const filePath = path.resolve(
		assetRoot,
		reference.slice("project://".length),
	);
	return isPathInside(assetRoot, filePath) ? { asset, filePath } : undefined;
};

ipcMain.handle("editor:open-asset", async (event, reference: unknown) => {
	if (!isTrustedSender(event)) return;
	const resolved = resolveProjectAsset(reference);
	if (
		!resolved ||
		resolved.asset.type !== "Scene" ||
		!(await confirmDiscardUnsavedChanges())
	)
		return;
	sendEditorCommand(`load_scene ${encode(resolved.filePath)}`);
});

ipcMain.handle("assets:reveal", async (event, reference: unknown) => {
	if (!isTrustedSender(event)) return;
	const resolved = resolveProjectAsset(reference);
	if (resolved) shell.showItemInFolder(resolved.filePath);
});

ipcMain.handle("editor:save-project", async (event) => {
	if (!isTrustedSender(event) || !mainWindow) return;
	const state = nativeHost?.getEditorState();
	if (!state?.projectPath) return;

	let scenePath = state.scenePath;
	const sceneIsInProject = Boolean(
		scenePath &&
			state.assetDirectoryPath &&
			isPathInside(state.assetDirectoryPath, scenePath),
	);
	if (!sceneIsInProject) {
		const defaultName = `${state.projectName.replace(/[^a-zA-Z0-9._-]+/g, "_") || "Main"}.plutoscene`;
		const result = await dialog.showSaveDialog(mainWindow, {
			title: "Save Project Scene",
			defaultPath: path.join(state.assetDirectoryPath, "Scenes", defaultName),
			filters: [{ name: "PlutoGE Scene", extensions: ["plutoscene"] }],
		});
		if (result.canceled || !result.filePath) return;
		scenePath = result.filePath;
	}
	sendEditorCommand(`save_scene ${encode(scenePath)}`);
	sendEditorCommand("save_project");
});

ipcMain.handle("editor:save-project-settings", (event, value: unknown) => {
	if (!isTrustedSender(event) || !nativeHost?.getEditorState()?.projectPath)
		return false;
	if (!value || typeof value !== "object") return false;

	const candidate = value as Partial<ProjectSettings>;
	if (
		typeof candidate.name !== "string" ||
		typeof candidate.startupScene !== "string" ||
		typeof candidate.scriptAssembly !== "string" ||
		typeof candidate.windowTitle !== "string" ||
		typeof candidate.windowWidth !== "number" ||
		typeof candidate.windowHeight !== "number" ||
		typeof candidate.vSyncEnabled !== "boolean"
	)
		return false;

	const name = candidate.name.trim();
	if (!name) return false;
	const width = Math.max(
		64,
		Math.min(16384, Math.round(candidate.windowWidth)),
	);
	const height = Math.max(
		64,
		Math.min(16384, Math.round(candidate.windowHeight)),
	);
	const argumentsText = [
		encode(name),
		encode(candidate.startupScene),
		encode(candidate.scriptAssembly),
		encode(candidate.windowTitle),
		width,
		height,
		candidate.vSyncEnabled ? 1 : 0,
	].join(" ");
	nativeHost.send(`set_project_settings 1 ${argumentsText}`);
	gameHost?.send(`set_project_settings 0 ${argumentsText}`);
	return true;
});

const uniqueImportDirectory = (
	assetRoot: string,
	sourcePath: string,
): string => {
	const modelsRoot = path.join(assetRoot, "Models");
	const stem =
		path.parse(sourcePath).name.replace(/[^a-zA-Z0-9._-]+/g, "_") || "Model";
	let candidate = path.join(modelsRoot, stem);
	for (let suffix = 2; fs.existsSync(candidate); suffix += 1)
		candidate = path.join(modelsRoot, `${stem}_${suffix}`);
	return candidate;
};

const copyGltfPackage = (
	sourcePath: string,
	destinationDirectory: string,
	warnings: string[],
): string => {
	const sourceDirectory = path.dirname(sourcePath);
	const destinationPath = path.join(
		destinationDirectory,
		path.basename(sourcePath),
	);
	let document: {
		buffers?: Array<{ uri?: string }>;
		images?: Array<{ uri?: string }>;
	};
	try {
		document = JSON.parse(
			fs.readFileSync(sourcePath, "utf8"),
		) as typeof document;
	} catch {
		fs.copyFileSync(sourcePath, destinationPath);
		warnings.push(
			`${path.basename(sourcePath)} could not be parsed; external glTF resources were not copied.`,
		);
		return destinationPath;
	}

	const resources = [...(document.buffers ?? []), ...(document.images ?? [])];
	for (const resource of resources) {
		if (!resource.uri || /^(data:|https?:|blob:)/i.test(resource.uri)) continue;
		const cleanUri = decodeURIComponent(resource.uri.split(/[?#]/, 1)[0]);
		const dependencySource = path.resolve(sourceDirectory, cleanUri);
		if (
			!fs.existsSync(dependencySource) ||
			!fs.statSync(dependencySource).isFile()
		) {
			warnings.push(`Missing glTF dependency: ${resource.uri}`);
			continue;
		}
		let dependencyDestination = path.resolve(destinationDirectory, cleanUri);
		if (!isPathInside(destinationDirectory, dependencyDestination)) {
			dependencyDestination = path.join(
				destinationDirectory,
				"Dependencies",
				path.basename(cleanUri),
			);
			resource.uri = path
				.relative(destinationDirectory, dependencyDestination)
				.replace(/\\/g, "/");
		}
		fs.mkdirSync(path.dirname(dependencyDestination), { recursive: true });
		fs.copyFileSync(dependencySource, dependencyDestination);
	}
	fs.writeFileSync(
		destinationPath,
		`${JSON.stringify(document, null, 2)}\n`,
		"utf8",
	);
	return destinationPath;
};

ipcMain.handle(
	"assets:import-models",
	async (event): Promise<ModelImportResult> => {
		const result: ModelImportResult = { imported: [], warnings: [] };
		if (!isTrustedSender(event) || !mainWindow) return result;
		const assetRoot = nativeHost?.getEditorState()?.assetDirectoryPath;
		if (!assetRoot) {
			result.warnings.push("Open a project before importing models.");
			return result;
		}
		const selection = await dialog.showOpenDialog(mainWindow, {
			title: "Import 3D Models",
			properties: ["openFile", "multiSelections"],
			filters: [{ name: "3D Models", extensions: ["glb", "gltf", "fbx"] }],
		});
		if (selection.canceled) return result;

		for (const sourcePath of selection.filePaths) {
			try {
				const destinationDirectory = uniqueImportDirectory(
					assetRoot,
					sourcePath,
				);
				fs.mkdirSync(destinationDirectory, { recursive: true });
				const destinationPath =
					path.extname(sourcePath).toLowerCase() === ".gltf"
						? copyGltfPackage(sourcePath, destinationDirectory, result.warnings)
						: path.join(destinationDirectory, path.basename(sourcePath));
				if (path.extname(sourcePath).toLowerCase() !== ".gltf")
					fs.copyFileSync(sourcePath, destinationPath);
				const relative = path
					.relative(assetRoot, destinationPath)
					.replace(/\\/g, "/");
				result.imported.push(`project://${relative}`);
			} catch (error) {
				result.warnings.push(
					`${path.basename(sourcePath)}: ${error instanceof Error ? error.message : String(error)}`,
				);
			}
		}
		if (result.imported.length) sendEditorCommand("refresh_assets");
		return result;
	},
);

ipcMain.on("editor:command", (event, action: unknown, ...args: unknown[]) => {
	if (!isTrustedSender(event) || typeof action !== "string") return;
	const number = (value: unknown): number =>
		Number.isFinite(Number(value)) ? Number(value) : 0;
	switch (action) {
		case "undo":
		case "redo":
			sendEditorCommand(action);
			break;
		case "runtime":
			sendEditorCommand(`runtime ${args[0] === true ? 1 : 0}`);
			break;
		case "gizmo-operation":
			sendEditorCommand(
				`gizmo_operation ${args[0] === "rotate" || args[0] === "scale" ? args[0] : "translate"}`,
			);
			break;
		case "gizmo-space":
			sendEditorCommand(
				`gizmo_space ${args[0] === "world" ? "world" : "local"}`,
			);
			break;
		case "set-editor-camera": {
			const camera =
				args[0] && typeof args[0] === "object"
					? (args[0] as Partial<EditorCameraState>)
					: {};
			const position = Array.isArray(camera.position)
				? camera.position.map(number).slice(0, 3)
				: [0, 2, 6];
			while (position.length < 3) position.push(0);
			sendEditorCommand(
				`set_editor_camera ${position.join(" ")} ${number(camera.yawDegrees)} ${number(camera.pitchDegrees)} ${number(camera.fovY)} ${number(camera.nearPlane)} ${number(camera.farPlane)} ${number(camera.moveSpeed)} ${number(camera.speedAdjustment)} ${camera.gridVisible === false ? 0 : 1}`,
			);
			break;
		}
		case "reset-editor-camera":
			sendEditorCommand("reset_editor_camera");
			break;
		case "frame-selected":
			sendEditorCommand("frame_selected");
			break;
		case "editor-effect-add":
			sendEditorCommand(`editor_effect_add ${encode(args[0])}`);
			break;
		case "editor-effect-remove":
			sendEditorCommand(`editor_effect_remove ${number(args[0])}`);
			break;
		case "editor-effect-move":
			sendEditorCommand(
				`editor_effect_move ${number(args[0])} ${number(args[1])}`,
			);
			break;
		case "editor-effect-enabled":
			sendEditorCommand(
				`editor_effect_enabled ${number(args[0])} ${args[1] === true ? 1 : 0}`,
			);
			break;
		case "editor-effect-parameter":
			sendEditorCommand(
				`editor_effect_parameter ${number(args[0])} ${number(args[1])} ${encode(args[2])}`,
			);
			break;
		case "editor-effect-preset":
			sendEditorCommand(`editor_effect_preset ${encode(args[0])}`);
			break;
		case "editor-effect-save-preset":
			sendEditorCommand("editor_effect_save_preset");
			break;
		case "editor-effect-save-preset-as":
			sendEditorCommand(`editor_effect_save_preset_as ${encode(args[0])}`);
			break;
		case "save-project":
			sendEditorCommand("save_project");
			break;
		case "refresh-assets":
			sendEditorCommand("refresh_assets");
			break;
		case "create-asset":
			sendEditorCommand(`create_asset ${encode(args[0])} ${encode(args[1])}`);
			break;
		case "instantiate-asset":
			sendEditorCommand(`instantiate_asset ${encode(args[0])}`);
			break;
		case "select":
		case "delete":
		case "duplicate":
			sendEditorCommand(`${action} ${number(args[0])}`);
			break;
		case "create":
			sendEditorCommand(`create ${encode(args[0])} ${number(args[1])}`);
			break;
		case "reparent":
			sendEditorCommand(`reparent ${number(args[0])} ${number(args[1])}`);
			break;
		case "set-name":
			sendEditorCommand(`set_name ${number(args[0])} ${encode(args[1])}`);
			break;
		case "set-active":
			sendEditorCommand(
				`set_active ${number(args[0])} ${args[1] === true ? 1 : 0}`,
			);
			break;
		case "set-transform": {
			const values = [args[1], args[2], args[3]].flatMap((value) =>
				Array.isArray(value) ? value.map(number) : [0, 0, 0],
			);
			sendEditorCommand(`set_transform ${number(args[0])} ${values.join(" ")}`);
			break;
		}
		case "component-enabled":
			sendEditorCommand(
				`component_enabled ${number(args[0])} ${number(args[1])} ${args[2] === true ? 1 : 0}`,
			);
			break;
		case "set-property":
			sendEditorCommand(
				`set_property ${number(args[0])} ${number(args[1])} ${number(args[2])} ${encode(args[3])}`,
			);
			break;
		case "add-component":
			sendEditorCommand(`add_component ${number(args[0])} ${encode(args[1])}`);
			break;
		case "remove-component":
			sendEditorCommand(
				`remove_component ${number(args[0])} ${number(args[1])}`,
			);
			break;
		case "camera-effect-add":
			sendEditorCommand(
				`camera_effect_add ${number(args[0])} ${number(args[1])} ${encode(args[2])}`,
			);
			break;
		case "camera-effect-remove":
			sendEditorCommand(
				`camera_effect_remove ${number(args[0])} ${number(args[1])} ${number(args[2])}`,
			);
			break;
		case "camera-effect-move":
			sendEditorCommand(
				`camera_effect_move ${number(args[0])} ${number(args[1])} ${number(args[2])} ${number(args[3])}`,
			);
			break;
		case "camera-effect-enabled":
			sendEditorCommand(
				`camera_effect_enabled ${number(args[0])} ${number(args[1])} ${number(args[2])} ${args[3] === true ? 1 : 0}`,
			);
			break;
		case "camera-effect-parameter":
			sendEditorCommand(
				`camera_effect_parameter ${number(args[0])} ${number(args[1])} ${number(args[2])} ${number(args[3])} ${encode(args[4])}`,
			);
			break;
		case "camera-effect-preset":
			sendEditorCommand(
				`camera_effect_preset ${number(args[0])} ${number(args[1])} ${encode(args[2])}`,
			);
			break;
		case "camera-effect-save-preset":
			sendEditorCommand(
				`camera_effect_save_preset ${number(args[0])} ${number(args[1])}`,
			);
			break;
		case "camera-effect-save-preset-as":
			sendEditorCommand(
				`camera_effect_save_preset_as ${number(args[0])} ${number(args[1])} ${encode(args[2])}`,
			);
			break;
	}
});

app.whenReady().then(() => {
	createWindow();
	app.on("activate", () => {
		if (BrowserWindow.getAllWindows().length === 0) createWindow();
	});
});

app.on("before-quit", (event) => {
	const hosts = [nativeHost, gameHost].filter((host): host is NativeHost =>
		Boolean(host && host.getState().status !== "stopped"),
	);
	if (!hosts.length) return;
	event.preventDefault();
	void Promise.all(hosts.map((host) => host.stop())).finally(() => {
		nativeHost = undefined;
		gameHost = undefined;
		app.quit();
	});
});

app.on("window-all-closed", () => {
	if (process.platform !== "darwin") app.quit();
});
