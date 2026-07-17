import { app, BrowserWindow, dialog, ipcMain } from 'electron';
import fs from 'node:fs';
import path from 'node:path';
import { NativeHost } from './native-host';

let mainWindow: BrowserWindow | undefined;
let nativeHost: NativeHost | undefined;
let viewportBounds: [number, number, number, number] | undefined;
let viewportVisible = true;

const isTrustedSender = (event: Electron.IpcMainEvent | Electron.IpcMainInvokeEvent): boolean =>
  Boolean(mainWindow && event.sender.id === mainWindow.webContents.id);

const syncViewport = (): void => {
  if (!viewportBounds) {
    nativeHost?.send('visible 0');
    return;
  }
  nativeHost?.send(`bounds ${viewportBounds.join(' ')}`);
  nativeHost?.send(`visible ${viewportVisible ? 1 : 0}`);
};

const createWindow = (): void => {
  const editorWindow = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 960,
    minHeight: 640,
    backgroundColor: '#101319',
    title: 'PlutoGE Editor',
    webPreferences: {
      preload: MAIN_WINDOW_PRELOAD_WEBPACK_ENTRY,
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  mainWindow = editorWindow;

  editorWindow.removeMenu();
  void editorWindow.loadURL(MAIN_WINDOW_WEBPACK_ENTRY);

  nativeHost = new NativeHost(
    editorWindow,
    (state) => {
      if (!editorWindow.isDestroyed()) editorWindow.webContents.send('host:state', state);
      if (state.status === 'ready') syncViewport();
    },
    (state) => {
      if (!editorWindow.isDestroyed()) editorWindow.webContents.send('editor:state', state);
    },
  );
  editorWindow.webContents.once('did-finish-load', () => nativeHost?.start());
  editorWindow.on('move', syncViewport);
  editorWindow.on('minimize', () => nativeHost?.send('visible 0'));
  editorWindow.on('restore', syncViewport);
  editorWindow.on('hide', () => nativeHost?.send('visible 0'));
  editorWindow.on('show', syncViewport);
  editorWindow.on('closed', () => {
    if (mainWindow === editorWindow) mainWindow = undefined;
  });
};

ipcMain.on('viewport:set-bounds', (event, value: unknown) => {
  if (!isTrustedSender(event) || !value || typeof value !== 'object') return;
  const bounds = value as Record<string, unknown>;
  const numbers = ['x', 'y', 'width', 'height'].map((key) => Number(bounds[key]));
  if (!numbers.every(Number.isFinite)) return;
  const [x, y, width, height] = numbers.map(Math.round);
  if (width <= 0 || height <= 0) return;
  viewportBounds = [x, y, width, height];
  syncViewport();
});

ipcMain.on('viewport:set-visible', (event, visible: unknown) => {
  if (isTrustedSender(event) && typeof visible === 'boolean') {
    viewportVisible = visible;
    syncViewport();
  }
});

ipcMain.handle('host:restart', async (event) => {
  if (!isTrustedSender(event)) return;
  await nativeHost?.restart();
});

ipcMain.handle('host:get-state', (event) => {
  if (!isTrustedSender(event)) return { status: 'error', message: 'Untrusted IPC sender.' };
  return nativeHost?.getState() ?? { status: 'stopped' };
});

// Prefix encoded arguments so even an empty string remains a token in the
// host's whitespace-delimited command protocol.
const encode = (value: unknown): string => `~${encodeURIComponent(String(value))}`;
const sendEditorCommand = (command: string): void => nativeHost?.send(command);
const confirmDiscardUnsavedChanges = async (): Promise<boolean> => {
  if (!mainWindow || !nativeHost?.getEditorState()?.dirty) return true;
  const result = await dialog.showMessageBox(mainWindow, {
    type: 'warning',
    title: 'Unsaved scene',
    message: 'Discard unsaved scene changes?',
    detail: 'This action cannot be undone.',
    buttons: ['Cancel', 'Discard'],
    defaultId: 0,
    cancelId: 0,
  });
  return result.response === 1;
};

ipcMain.handle('editor:get-state', (event) => {
  if (!isTrustedSender(event)) return undefined;
  return nativeHost?.getEditorState();
});

ipcMain.handle('editor:new-scene', async (event) => {
  if (isTrustedSender(event) && await confirmDiscardUnsavedChanges()) sendEditorCommand('new_scene');
});

ipcMain.handle('editor:new-project', async (event) => {
  if (!isTrustedSender(event) || !mainWindow) return;
  if (!await confirmDiscardUnsavedChanges()) return;
  const result = await dialog.showSaveDialog(mainWindow, {
    title: 'Create PlutoGE Project',
    defaultPath: path.join(app.getPath('documents'), 'NewProject', 'NewProject.plutoproject'),
    buttonLabel: 'Create Project',
    filters: [{ name: 'PlutoGE Project', extensions: ['plutoproject'] }],
  });
  if (result.canceled || !result.filePath) return;
  const projectName = path.basename(result.filePath, path.extname(result.filePath));
  sendEditorCommand(`create_project ${encode(result.filePath)} ${encode(projectName)}`);
});

ipcMain.handle('editor:open-project', async (event) => {
  if (!isTrustedSender(event) || !mainWindow) return;
  if (!await confirmDiscardUnsavedChanges()) return;
  const result = await dialog.showOpenDialog(mainWindow, {
    title: 'Open PlutoGE Project',
    properties: ['openFile'],
    filters: [{ name: 'PlutoGE Project', extensions: ['plutoproject'] }],
  });
  if (!result.canceled && result.filePaths[0]) sendEditorCommand(`load_project ${encode(result.filePaths[0])}`);
});

ipcMain.handle('editor:open-scene', async (event) => {
  if (!isTrustedSender(event) || !mainWindow) return;
  if (!await confirmDiscardUnsavedChanges()) return;
  const result = await dialog.showOpenDialog(mainWindow, {
    title: 'Open PlutoGE Scene',
    properties: ['openFile'],
    filters: [{ name: 'PlutoGE Scene', extensions: ['plutoscene'] }],
  });
  if (!result.canceled && result.filePaths[0]) sendEditorCommand(`load_scene ${encode(result.filePaths[0])}`);
});

ipcMain.handle('editor:save-scene', async (event, saveAs: unknown) => {
  if (!isTrustedSender(event) || !mainWindow) return;
  let path = saveAs === true ? '' : nativeHost?.getEditorState()?.scenePath ?? '';
  if (!path) {
    const result = await dialog.showSaveDialog(mainWindow, {
      title: 'Save PlutoGE Scene',
      defaultPath: 'Untitled.plutoscene',
      filters: [{ name: 'PlutoGE Scene', extensions: ['plutoscene'] }],
    });
    if (result.canceled || !result.filePath) return;
    path = result.filePath;
  }
  sendEditorCommand(`save_scene ${encode(path)}`);
});

const isPathInside = (parent: string, candidate: string): boolean => {
  const relative = path.relative(parent, candidate);
  return Boolean(relative) && !relative.startsWith('..') && !path.isAbsolute(relative);
};

const uniqueImportDirectory = (assetRoot: string, sourcePath: string): string => {
  const modelsRoot = path.join(assetRoot, 'Models');
  const stem = path.parse(sourcePath).name.replace(/[^a-zA-Z0-9._-]+/g, '_') || 'Model';
  let candidate = path.join(modelsRoot, stem);
  for (let suffix = 2; fs.existsSync(candidate); suffix += 1) candidate = path.join(modelsRoot, `${stem}_${suffix}`);
  return candidate;
};

const copyGltfPackage = (sourcePath: string, destinationDirectory: string, warnings: string[]): string => {
  const sourceDirectory = path.dirname(sourcePath);
  const destinationPath = path.join(destinationDirectory, path.basename(sourcePath));
  let document: { buffers?: Array<{ uri?: string }>; images?: Array<{ uri?: string }> };
  try {
    document = JSON.parse(fs.readFileSync(sourcePath, 'utf8')) as typeof document;
  } catch {
    fs.copyFileSync(sourcePath, destinationPath);
    warnings.push(`${path.basename(sourcePath)} could not be parsed; external glTF resources were not copied.`);
    return destinationPath;
  }

  const resources = [...(document.buffers ?? []), ...(document.images ?? [])];
  for (const resource of resources) {
    if (!resource.uri || /^(data:|https?:|blob:)/i.test(resource.uri)) continue;
    const cleanUri = decodeURIComponent(resource.uri.split(/[?#]/, 1)[0]);
    const dependencySource = path.resolve(sourceDirectory, cleanUri);
    if (!fs.existsSync(dependencySource) || !fs.statSync(dependencySource).isFile()) {
      warnings.push(`Missing glTF dependency: ${resource.uri}`);
      continue;
    }
    let dependencyDestination = path.resolve(destinationDirectory, cleanUri);
    if (!isPathInside(destinationDirectory, dependencyDestination)) {
      dependencyDestination = path.join(destinationDirectory, 'Dependencies', path.basename(cleanUri));
      resource.uri = path.relative(destinationDirectory, dependencyDestination).replace(/\\/g, '/');
    }
    fs.mkdirSync(path.dirname(dependencyDestination), { recursive: true });
    fs.copyFileSync(dependencySource, dependencyDestination);
  }
  fs.writeFileSync(destinationPath, `${JSON.stringify(document, null, 2)}\n`, 'utf8');
  return destinationPath;
};

ipcMain.handle('assets:import-models', async (event): Promise<ModelImportResult> => {
  const result: ModelImportResult = { imported: [], warnings: [] };
  if (!isTrustedSender(event) || !mainWindow) return result;
  const assetRoot = nativeHost?.getEditorState()?.assetDirectoryPath;
  if (!assetRoot) {
    result.warnings.push('Open a project before importing models.');
    return result;
  }
  const selection = await dialog.showOpenDialog(mainWindow, {
    title: 'Import 3D Models',
    properties: ['openFile', 'multiSelections'],
    filters: [{ name: '3D Models', extensions: ['glb', 'gltf', 'fbx'] }],
  });
  if (selection.canceled) return result;

  for (const sourcePath of selection.filePaths) {
    try {
      const destinationDirectory = uniqueImportDirectory(assetRoot, sourcePath);
      fs.mkdirSync(destinationDirectory, { recursive: true });
      const destinationPath = path.extname(sourcePath).toLowerCase() === '.gltf'
        ? copyGltfPackage(sourcePath, destinationDirectory, result.warnings)
        : path.join(destinationDirectory, path.basename(sourcePath));
      if (path.extname(sourcePath).toLowerCase() !== '.gltf') fs.copyFileSync(sourcePath, destinationPath);
      const relative = path.relative(assetRoot, destinationPath).replace(/\\/g, '/');
      result.imported.push(`project://${relative}`);
    } catch (error) {
      result.warnings.push(`${path.basename(sourcePath)}: ${error instanceof Error ? error.message : String(error)}`);
    }
  }
  if (result.imported.length) sendEditorCommand('refresh_assets');
  return result;
});

ipcMain.on('editor:command', (event, action: unknown, ...args: unknown[]) => {
  if (!isTrustedSender(event) || typeof action !== 'string') return;
  const number = (value: unknown): number => Number.isFinite(Number(value)) ? Number(value) : 0;
  switch (action) {
    case 'undo': case 'redo': sendEditorCommand(action); break;
    case 'runtime': sendEditorCommand(`runtime ${args[0] === true ? 1 : 0}`); break;
    case 'set-editor-camera': {
      const camera = args[0] && typeof args[0] === 'object' ? args[0] as Partial<EditorCameraState> : {};
      const position = Array.isArray(camera.position) ? camera.position.map(number).slice(0, 3) : [0, 2, 6];
      while (position.length < 3) position.push(0);
      sendEditorCommand(`set_editor_camera ${position.join(' ')} ${number(camera.yawDegrees)} ${number(camera.pitchDegrees)} ${number(camera.fovY)} ${number(camera.nearPlane)} ${number(camera.farPlane)} ${number(camera.moveSpeed)} ${number(camera.speedAdjustment)} ${camera.gridVisible === false ? 0 : 1}`);
      break;
    }
    case 'reset-editor-camera': sendEditorCommand('reset_editor_camera'); break;
    case 'frame-selected': sendEditorCommand('frame_selected'); break;
    case 'editor-effect-add': sendEditorCommand(`editor_effect_add ${encode(args[0])}`); break;
    case 'editor-effect-remove': sendEditorCommand(`editor_effect_remove ${number(args[0])}`); break;
    case 'editor-effect-move': sendEditorCommand(`editor_effect_move ${number(args[0])} ${number(args[1])}`); break;
    case 'editor-effect-enabled': sendEditorCommand(`editor_effect_enabled ${number(args[0])} ${args[1] === true ? 1 : 0}`); break;
    case 'editor-effect-parameter': sendEditorCommand(`editor_effect_parameter ${number(args[0])} ${number(args[1])} ${encode(args[2])}`); break;
    case 'editor-effect-preset': sendEditorCommand(`editor_effect_preset ${encode(args[0])}`); break;
    case 'editor-effect-save-preset': sendEditorCommand('editor_effect_save_preset'); break;
    case 'save-project': sendEditorCommand('save_project'); break;
    case 'refresh-assets': sendEditorCommand('refresh_assets'); break;
    case 'instantiate-asset': sendEditorCommand(`instantiate_asset ${encode(args[0])}`); break;
    case 'select': case 'delete': sendEditorCommand(`${action} ${number(args[0])}`); break;
    case 'create': sendEditorCommand(`create ${encode(args[0])} ${number(args[1])}`); break;
    case 'reparent': sendEditorCommand(`reparent ${number(args[0])} ${number(args[1])}`); break;
    case 'set-name': sendEditorCommand(`set_name ${number(args[0])} ${encode(args[1])}`); break;
    case 'set-active': sendEditorCommand(`set_active ${number(args[0])} ${args[1] === true ? 1 : 0}`); break;
    case 'set-transform': {
      const values = [args[1], args[2], args[3]].flatMap((value) => Array.isArray(value) ? value.map(number) : [0, 0, 0]);
      sendEditorCommand(`set_transform ${number(args[0])} ${values.join(' ')}`);
      break;
    }
    case 'component-enabled': sendEditorCommand(`component_enabled ${number(args[0])} ${number(args[1])} ${args[2] === true ? 1 : 0}`); break;
    case 'set-property': sendEditorCommand(`set_property ${number(args[0])} ${number(args[1])} ${number(args[2])} ${encode(args[3])}`); break;
    case 'add-component': sendEditorCommand(`add_component ${number(args[0])} ${encode(args[1])}`); break;
    case 'remove-component': sendEditorCommand(`remove_component ${number(args[0])} ${number(args[1])}`); break;
    case 'camera-effect-add': sendEditorCommand(`camera_effect_add ${number(args[0])} ${number(args[1])} ${encode(args[2])}`); break;
    case 'camera-effect-remove': sendEditorCommand(`camera_effect_remove ${number(args[0])} ${number(args[1])} ${number(args[2])}`); break;
    case 'camera-effect-move': sendEditorCommand(`camera_effect_move ${number(args[0])} ${number(args[1])} ${number(args[2])} ${number(args[3])}`); break;
    case 'camera-effect-enabled': sendEditorCommand(`camera_effect_enabled ${number(args[0])} ${number(args[1])} ${number(args[2])} ${args[3] === true ? 1 : 0}`); break;
    case 'camera-effect-parameter': sendEditorCommand(`camera_effect_parameter ${number(args[0])} ${number(args[1])} ${number(args[2])} ${number(args[3])} ${encode(args[4])}`); break;
    case 'camera-effect-preset': sendEditorCommand(`camera_effect_preset ${number(args[0])} ${number(args[1])} ${encode(args[2])}`); break;
    case 'camera-effect-save-preset': sendEditorCommand(`camera_effect_save_preset ${number(args[0])} ${number(args[1])}`); break;
  }
});

app.whenReady().then(() => {
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('before-quit', (event) => {
  if (!nativeHost || nativeHost.getState().status === 'stopped') return;
  event.preventDefault();
  void nativeHost.stop().finally(() => {
    nativeHost = undefined;
    app.quit();
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
