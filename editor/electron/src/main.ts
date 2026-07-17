import { app, BrowserWindow, ipcMain } from 'electron';
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

  nativeHost = new NativeHost(editorWindow, (state) => {
    if (!editorWindow.isDestroyed()) editorWindow.webContents.send('host:state', state);
    if (state.status === 'ready') syncViewport();
  });
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
