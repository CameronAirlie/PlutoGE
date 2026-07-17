declare const MAIN_WINDOW_WEBPACK_ENTRY: string;
declare const MAIN_WINDOW_PRELOAD_WEBPACK_ENTRY: string;

type HostStatus = 'starting' | 'ready' | 'stopped' | 'error';

interface HostState {
  status: HostStatus;
  message?: string;
}

interface ViewportBounds {
  x: number;
  y: number;
  width: number;
  height: number;
}

type Vec3 = [number, number, number];

interface EditorProperty {
  name: string;
  type: number;
  value: string;
  enumOptions: string[];
}

interface EditorComponent {
  type: string;
  enabled: boolean;
  properties: EditorProperty[];
}

interface EditorEntity {
  id: number;
  parentId: number;
  name: string;
  active: boolean;
  position: Vec3;
  rotation: Vec3;
  scale: Vec3;
  components: EditorComponent[];
}

interface EditorCameraState {
  position: Vec3;
  yawDegrees: number;
  pitchDegrees: number;
  fovY: number;
  nearPlane: number;
  farPlane: number;
  moveSpeed: number;
  speedAdjustment: number;
  gridVisible: boolean;
  postProcessEffectCount: number;
  postProcessPresetReference: string;
}

interface ViewportStats {
  submittedRenderCommands: number;
  visibleRenderCommands: number;
  registeredMeshComponents: number;
  renderableMeshComponents: number;
}

interface EditorState {
  projectPath: string;
  projectName: string;
  scenePath: string;
  dirty: boolean;
  running: boolean;
  selectedEntityId: number;
  canUndo: boolean;
  canRedo: boolean;
  editorCamera: EditorCameraState;
  viewportStats: ViewportStats;
  entities: EditorEntity[];
}

interface PlutoEditorApi {
  setViewportBounds(bounds: ViewportBounds): void;
  setViewportVisible(visible: boolean): void;
  getHostState(): Promise<HostState>;
  restartHost(): Promise<void>;
  onHostState(callback: (state: HostState) => void): () => void;
  getEditorState(): Promise<EditorState | undefined>;
  onEditorState(callback: (state: EditorState) => void): () => void;
  newScene(): Promise<void>;
  openProject(): Promise<void>;
  saveProject(): void;
  openScene(): Promise<void>;
  saveScene(saveAs?: boolean): Promise<void>;
  undo(): void;
  redo(): void;
  setRuntime(running: boolean): void;
  setEditorCamera(camera: EditorCameraState): void;
  resetEditorCamera(): void;
  frameSelected(): void;
  selectEntity(id: number): void;
  createEntity(name: string, parentId?: number): void;
  deleteEntity(id: number): void;
  reparentEntity(id: number, parentId: number): void;
  setEntityName(id: number, name: string): void;
  setEntityActive(id: number, active: boolean): void;
  setEntityTransform(id: number, position: Vec3, rotation: Vec3, scale: Vec3): void;
  setComponentEnabled(entityId: number, componentIndex: number, enabled: boolean): void;
  setComponentProperty(entityId: number, componentIndex: number, propertyIndex: number, value: string): void;
  addComponent(entityId: number, type: string): void;
  removeComponent(entityId: number, componentIndex: number): void;
}

interface Window {
  plutoEditor: PlutoEditorApi;
}
