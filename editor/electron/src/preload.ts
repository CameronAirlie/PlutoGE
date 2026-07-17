import { contextBridge, ipcRenderer } from 'electron';

const api: PlutoEditorApi = {
  setViewportBounds: (bounds) => ipcRenderer.send('viewport:set-bounds', bounds),
  setViewportVisible: (visible) => ipcRenderer.send('viewport:set-visible', visible),
  setGameViewportBounds: (bounds) => ipcRenderer.send('game-viewport:set-bounds', bounds),
  setGameViewportVisible: (visible) => ipcRenderer.send('game-viewport:set-visible', visible),
  setViewportOccluded: (token, occluded) => ipcRenderer.send('viewport:set-occluded', token, occluded),
  getHostState: () => ipcRenderer.invoke('host:get-state'),
  restartHost: () => ipcRenderer.invoke('host:restart'),
  onHostState: (callback) => {
    const listener = (_event: Electron.IpcRendererEvent, state: HostState) => callback(state);
    ipcRenderer.on('host:state', listener);
    return () => ipcRenderer.removeListener('host:state', listener);
  },
  getGameHostState: () => ipcRenderer.invoke('game-host:get-state'),
  restartGameHost: () => ipcRenderer.invoke('game-host:restart'),
  onGameHostState: (callback) => {
    const listener = (_event: Electron.IpcRendererEvent, state: HostState) => callback(state);
    ipcRenderer.on('game-host:state', listener);
    return () => ipcRenderer.removeListener('game-host:state', listener);
  },
  getEditorState: () => ipcRenderer.invoke('editor:get-state'),
  onEditorState: (callback) => {
    const listener = (_event: Electron.IpcRendererEvent, state: EditorState) => callback(state);
    ipcRenderer.on('editor:state', listener);
    return () => ipcRenderer.removeListener('editor:state', listener);
  },
  newScene: () => ipcRenderer.invoke('editor:new-scene'),
  newProject: () => ipcRenderer.invoke('editor:new-project'),
  openProject: () => ipcRenderer.invoke('editor:open-project'),
  saveProject: () => ipcRenderer.invoke('editor:save-project'),
  openScene: () => ipcRenderer.invoke('editor:open-scene'),
  openAsset: (reference) => ipcRenderer.invoke('editor:open-asset', reference),
  revealAsset: (reference) => ipcRenderer.invoke('assets:reveal', reference),
  saveScene: (saveAs = false) => ipcRenderer.invoke('editor:save-scene', saveAs),
  importModels: () => ipcRenderer.invoke('assets:import-models'),
  refreshAssets: () => ipcRenderer.send('editor:command', 'refresh-assets'),
  createAsset: (type, reference) => ipcRenderer.send('editor:command', 'create-asset', type, reference),
  instantiateAsset: (reference) => ipcRenderer.send('editor:command', 'instantiate-asset', reference),
  undo: () => ipcRenderer.send('editor:command', 'undo'),
  redo: () => ipcRenderer.send('editor:command', 'redo'),
  setRuntime: (running) => ipcRenderer.send('editor:command', 'runtime', running),
  setGizmoOperation: (operation) => ipcRenderer.send('editor:command', 'gizmo-operation', operation),
  setGizmoSpace: (space) => ipcRenderer.send('editor:command', 'gizmo-space', space),
  setEditorCamera: (camera) => ipcRenderer.send('editor:command', 'set-editor-camera', camera),
  resetEditorCamera: () => ipcRenderer.send('editor:command', 'reset-editor-camera'),
  frameSelected: () => ipcRenderer.send('editor:command', 'frame-selected'),
  addEditorPostProcessEffect: (type) => ipcRenderer.send('editor:command', 'editor-effect-add', type),
  removeEditorPostProcessEffect: (index) => ipcRenderer.send('editor:command', 'editor-effect-remove', index),
  moveEditorPostProcessEffect: (from, to) => ipcRenderer.send('editor:command', 'editor-effect-move', from, to),
  setEditorPostProcessEffectEnabled: (index, enabled) => ipcRenderer.send('editor:command', 'editor-effect-enabled', index, enabled),
  setEditorPostProcessParameter: (effectIndex, parameterIndex, value) => ipcRenderer.send('editor:command', 'editor-effect-parameter', effectIndex, parameterIndex, value),
  setEditorPostProcessPreset: (reference) => ipcRenderer.send('editor:command', 'editor-effect-preset', reference),
  saveEditorPostProcessPreset: () => ipcRenderer.send('editor:command', 'editor-effect-save-preset'),
  saveEditorPostProcessPresetAs: (reference) => ipcRenderer.send('editor:command', 'editor-effect-save-preset-as', reference),
  selectEntity: (id) => ipcRenderer.send('editor:command', 'select', id),
  createEntity: (name, parentId = 0) => ipcRenderer.send('editor:command', 'create', name, parentId),
  deleteEntity: (id) => ipcRenderer.send('editor:command', 'delete', id),
  duplicateEntity: (id) => ipcRenderer.send('editor:command', 'duplicate', id),
  reparentEntity: (id, parentId) => ipcRenderer.send('editor:command', 'reparent', id, parentId),
  setEntityName: (id, name) => ipcRenderer.send('editor:command', 'set-name', id, name),
  setEntityActive: (id, active) => ipcRenderer.send('editor:command', 'set-active', id, active),
  setEntityTransform: (id, position, rotation, scale) => ipcRenderer.send('editor:command', 'set-transform', id, position, rotation, scale),
  setComponentEnabled: (entityId, componentIndex, enabled) => ipcRenderer.send('editor:command', 'component-enabled', entityId, componentIndex, enabled),
  setComponentProperty: (entityId, componentIndex, propertyIndex, value) => ipcRenderer.send('editor:command', 'set-property', entityId, componentIndex, propertyIndex, value),
  addComponent: (entityId, type) => ipcRenderer.send('editor:command', 'add-component', entityId, type),
  removeComponent: (entityId, componentIndex) => ipcRenderer.send('editor:command', 'remove-component', entityId, componentIndex),
  addCameraPostProcessEffect: (entityId, componentIndex, type) => ipcRenderer.send('editor:command', 'camera-effect-add', entityId, componentIndex, type),
  removeCameraPostProcessEffect: (entityId, componentIndex, index) => ipcRenderer.send('editor:command', 'camera-effect-remove', entityId, componentIndex, index),
  moveCameraPostProcessEffect: (entityId, componentIndex, from, to) => ipcRenderer.send('editor:command', 'camera-effect-move', entityId, componentIndex, from, to),
  setCameraPostProcessEffectEnabled: (entityId, componentIndex, index, enabled) => ipcRenderer.send('editor:command', 'camera-effect-enabled', entityId, componentIndex, index, enabled),
  setCameraPostProcessParameter: (entityId, componentIndex, effectIndex, parameterIndex, value) => ipcRenderer.send('editor:command', 'camera-effect-parameter', entityId, componentIndex, effectIndex, parameterIndex, value),
  setCameraPostProcessPreset: (entityId, componentIndex, reference) => ipcRenderer.send('editor:command', 'camera-effect-preset', entityId, componentIndex, reference),
  saveCameraPostProcessPreset: (entityId, componentIndex) => ipcRenderer.send('editor:command', 'camera-effect-save-preset', entityId, componentIndex),
  saveCameraPostProcessPresetAs: (entityId, componentIndex, reference) => ipcRenderer.send('editor:command', 'camera-effect-save-preset-as', entityId, componentIndex, reference),
};

contextBridge.exposeInMainWorld('plutoEditor', api);
