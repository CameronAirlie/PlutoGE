import { contextBridge, ipcRenderer } from 'electron';

const api: PlutoEditorApi = {
  setViewportBounds: (bounds) => ipcRenderer.send('viewport:set-bounds', bounds),
  setViewportVisible: (visible) => ipcRenderer.send('viewport:set-visible', visible),
  getHostState: () => ipcRenderer.invoke('host:get-state'),
  restartHost: () => ipcRenderer.invoke('host:restart'),
  onHostState: (callback) => {
    const listener = (_event: Electron.IpcRendererEvent, state: HostState) => callback(state);
    ipcRenderer.on('host:state', listener);
    return () => ipcRenderer.removeListener('host:state', listener);
  },
};

contextBridge.exposeInMainWorld('plutoEditor', api);
