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
  getEditorState: () => ipcRenderer.invoke('editor:get-state'),
  onEditorState: (callback) => {
    const listener = (_event: Electron.IpcRendererEvent, state: EditorState) => callback(state);
    ipcRenderer.on('editor:state', listener);
    return () => ipcRenderer.removeListener('editor:state', listener);
  },
  newScene: () => ipcRenderer.invoke('editor:new-scene'),
  openProject: () => ipcRenderer.invoke('editor:open-project'),
  saveProject: () => ipcRenderer.send('editor:command', 'save-project'),
  openScene: () => ipcRenderer.invoke('editor:open-scene'),
  saveScene: (saveAs = false) => ipcRenderer.invoke('editor:save-scene', saveAs),
  undo: () => ipcRenderer.send('editor:command', 'undo'),
  redo: () => ipcRenderer.send('editor:command', 'redo'),
  setRuntime: (running) => ipcRenderer.send('editor:command', 'runtime', running),
  setEditorCamera: (camera) => ipcRenderer.send('editor:command', 'set-editor-camera', camera),
  resetEditorCamera: () => ipcRenderer.send('editor:command', 'reset-editor-camera'),
  frameSelected: () => ipcRenderer.send('editor:command', 'frame-selected'),
  selectEntity: (id) => ipcRenderer.send('editor:command', 'select', id),
  createEntity: (name, parentId = 0) => ipcRenderer.send('editor:command', 'create', name, parentId),
  deleteEntity: (id) => ipcRenderer.send('editor:command', 'delete', id),
  reparentEntity: (id, parentId) => ipcRenderer.send('editor:command', 'reparent', id, parentId),
  setEntityName: (id, name) => ipcRenderer.send('editor:command', 'set-name', id, name),
  setEntityActive: (id, active) => ipcRenderer.send('editor:command', 'set-active', id, active),
  setEntityTransform: (id, position, rotation, scale) => ipcRenderer.send('editor:command', 'set-transform', id, position, rotation, scale),
  setComponentEnabled: (entityId, componentIndex, enabled) => ipcRenderer.send('editor:command', 'component-enabled', entityId, componentIndex, enabled),
  setComponentProperty: (entityId, componentIndex, propertyIndex, value) => ipcRenderer.send('editor:command', 'set-property', entityId, componentIndex, propertyIndex, value),
  addComponent: (entityId, type) => ipcRenderer.send('editor:command', 'add-component', entityId, type),
  removeComponent: (entityId, componentIndex) => ipcRenderer.send('editor:command', 'remove-component', entityId, componentIndex),
};

contextBridge.exposeInMainWorld('plutoEditor', api);
