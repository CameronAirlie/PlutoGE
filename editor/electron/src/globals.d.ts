declare const MAIN_WINDOW_WEBPACK_ENTRY: string;
declare const MAIN_WINDOW_PRELOAD_WEBPACK_ENTRY: string;

type HostStatus = "starting" | "ready" | "stopped" | "error";

interface HostState {
	status: HostStatus;
	message?: string;
}

interface HostPerformance {
	fps: number;
	frameTimeMs: number;
	maxFrameTimeMs: number;
	eventPollingMs: number;
	updateMs: number;
	renderMs: number;
	presentMs: number;
	cpuPassMs: number;
	gpuPassMs: number;
	visible: boolean;
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

interface PostProcessEffectState {
	typeName: string;
	displayName: string;
	enabled: boolean;
	parameters: EditorProperty[];
}

interface EditorComponent {
	type: string;
	enabled: boolean;
	properties: EditorProperty[];
	postProcessPresetReference?: string;
	postProcessEffects?: PostProcessEffectState[];
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
	postProcessEffects: PostProcessEffectState[];
}

interface ViewportStats {
	submittedRenderCommands: number;
	visibleRenderCommands: number;
	registeredMeshComponents: number;
	renderableMeshComponents: number;
}

interface EditorAsset {
	reference: string;
	size: number;
	type: string;
}

interface ModelImportResult {
	imported: string[];
	warnings: string[];
}

interface ProjectSettings {
	name: string;
	startupScene: string;
	scriptAssembly: string;
	windowTitle: string;
	windowWidth: number;
	windowHeight: number;
	vSyncEnabled: boolean;
}

interface EditorState {
	projectPath: string;
	projectName: string;
	assetDirectoryPath: string;
	projectSettings: ProjectSettings;
	assets: EditorAsset[];
	scenePath: string;
	dirty: boolean;
	postProcessEffectTypes: string[];
	running: boolean;
	gizmoOperation: "translate" | "rotate" | "scale";
	gizmoSpace: "local" | "world";
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
	setGameViewportBounds(bounds: ViewportBounds): void;
	setGameViewportVisible(visible: boolean): void;
	setViewportOccluded(token: string, occluded: boolean): void;
	getHostState(): Promise<HostState>;
	restartHost(): Promise<void>;
	onHostState(callback: (state: HostState) => void): () => void;
	getHostPerformance(): Promise<HostPerformance | undefined>;
	onHostPerformance(
		callback: (performance: HostPerformance) => void,
	): () => void;
	getGameHostState(): Promise<HostState>;
	restartGameHost(): Promise<void>;
	onGameHostState(callback: (state: HostState) => void): () => void;
	getGameHostPerformance(): Promise<HostPerformance | undefined>;
	onGameHostPerformance(
		callback: (performance: HostPerformance) => void,
	): () => void;
	getEditorState(): Promise<EditorState | undefined>;
	onEditorState(callback: (state: EditorState) => void): () => void;
	newScene(): Promise<void>;
	newProject(): Promise<void>;
	openProject(): Promise<void>;
	saveProject(): Promise<void>;
	saveProjectSettings(settings: ProjectSettings): Promise<boolean>;
	openScene(): Promise<void>;
	openAsset(reference: string): Promise<void>;
	revealAsset(reference: string): Promise<void>;
	saveScene(saveAs?: boolean): Promise<void>;
	importModels(): Promise<ModelImportResult>;
	refreshAssets(): void;
	createAsset(type: "material" | "post-process", reference: string): void;
	instantiateAsset(reference: string): void;
	undo(): void;
	redo(): void;
	setRuntime(running: boolean): void;
	setGizmoOperation(operation: "translate" | "rotate" | "scale"): void;
	setGizmoSpace(space: "local" | "world"): void;
	setEditorCamera(camera: EditorCameraState): void;
	resetEditorCamera(): void;
	frameSelected(): void;
	addEditorPostProcessEffect(type: string): void;
	removeEditorPostProcessEffect(index: number): void;
	moveEditorPostProcessEffect(from: number, to: number): void;
	setEditorPostProcessEffectEnabled(index: number, enabled: boolean): void;
	setEditorPostProcessParameter(
		effectIndex: number,
		parameterIndex: number,
		value: string,
	): void;
	setEditorPostProcessPreset(reference: string): void;
	saveEditorPostProcessPreset(): void;
	saveEditorPostProcessPresetAs(reference: string): void;
	selectEntity(id: number): void;
	createEntity(name: string, parentId?: number): void;
	deleteEntity(id: number): void;
	duplicateEntity(id: number): void;
	reparentEntity(id: number, parentId: number): void;
	setEntityName(id: number, name: string): void;
	setEntityActive(id: number, active: boolean): void;
	setEntityTransform(
		id: number,
		position: Vec3,
		rotation: Vec3,
		scale: Vec3,
	): void;
	setComponentEnabled(
		entityId: number,
		componentIndex: number,
		enabled: boolean,
	): void;
	setComponentProperty(
		entityId: number,
		componentIndex: number,
		propertyIndex: number,
		value: string,
	): void;
	addComponent(entityId: number, type: string): void;
	removeComponent(entityId: number, componentIndex: number): void;
	addCameraPostProcessEffect(
		entityId: number,
		componentIndex: number,
		type: string,
	): void;
	removeCameraPostProcessEffect(
		entityId: number,
		componentIndex: number,
		index: number,
	): void;
	moveCameraPostProcessEffect(
		entityId: number,
		componentIndex: number,
		from: number,
		to: number,
	): void;
	setCameraPostProcessEffectEnabled(
		entityId: number,
		componentIndex: number,
		index: number,
		enabled: boolean,
	): void;
	setCameraPostProcessParameter(
		entityId: number,
		componentIndex: number,
		effectIndex: number,
		parameterIndex: number,
		value: string,
	): void;
	setCameraPostProcessPreset(
		entityId: number,
		componentIndex: number,
		reference: string,
	): void;
	saveCameraPostProcessPreset(entityId: number, componentIndex: number): void;
	saveCameraPostProcessPresetAs(
		entityId: number,
		componentIndex: number,
		reference: string,
	): void;
}

interface Window {
	plutoEditor: PlutoEditorApi;
}
