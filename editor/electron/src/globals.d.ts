declare const MAIN_WINDOW_WEBPACK_ENTRY: string;
declare const MAIN_WINDOW_PRELOAD_WEBPACK_ENTRY: string;

type HostStatus = "starting" | "ready" | "stopped" | "error";

interface HostState {
	status: HostStatus;
	message?: string;
}

interface EditorOperationState {
	busy: boolean;
	label: string;
	token?: string;
	message?: string;
	progress?: number;
	detail?: string;
	target?: string;
	startedAt?: number;
}

interface HostPerformance {
	fps: number;
	frameTimeMs: number;
	maxFrameTimeMs: number;
	commandMs?: number;
	eventPollingMs: number;
	interactionMs?: number;
	updateMs: number;
	renderMs: number;
	presentMs: number;
	waitMs?: number;
	overheadMs?: number;
	cpuPassMs: number;
	gpuPassMs: number;
	visible: boolean;
	vSync?: boolean;
	profiledRenderCount?: number;
	viewportWidth?: number;
	viewportHeight?: number;
	cpuPasses?: HostPassTiming[];
	gpuPasses?: HostPassTiming[];
	postProcessGpuPasses?: HostPassTiming[];
	lighting?: HostLightingPerformance;
	workload?: HostRendererWorkload;
}

interface HostPassTiming {
	name: string;
	timeMs: number;
	available?: boolean;
}

interface HostLightingPerformance {
	setupMs: number;
	setupAvailable: boolean;
	ambientMs: number;
	ambientAvailable: boolean;
	lightAccumulationMs: number;
	lightAccumulationAvailable: boolean;
	lightCount: number;
	shadowedLightCount: number;
}

interface HostRendererWorkload {
	submittedRenderCommands: number;
	submissionCulledRenderCommands: number;
	visibleRenderCommands: number;
	frustumCulledRenderCommands: number;
	visibleSingleLodCommands: number;
	visibleMultiLodCommands: number;
	renderCommandSorts: number;
	geometryLogicalBatches: number;
	geometryMaterialGroups: number;
	geometryApiDrawCalls: number;
	geometryInstances: number;
	geometryTriangles: number;
	geometryTrianglesByLod: [number, number, number, number];
	shadowUpdatedSurfaces: number;
	shadowUpdatedDirectionalCascades: number;
	shadowUpdatedPixels: number;
	shadowInstances: number;
	shadowLogicalBatches: number;
	shadowMaterialGroups: number;
	shadowApiDrawCalls: number;
	shadowTriangles: number;
	intermediateTargetResizeMs: number;
	intermediateTargetResizes: number;
	gBufferResizeMs: number;
	gBufferResizes: number;
}

interface ViewportBounds {
	x: number;
	y: number;
	width: number;
	height: number;
}

interface ViewportFrame {
	transport: "cpu" | "shared-texture";
	sequence: number;
	generation: number;
	width: number;
	height: number;
	sourceWidth: number;
	sourceHeight: number;
	pixels: Uint8Array;
}

type ViewportInputEvent =
	| {
			type: "pointer";
			x: number;
			y: number;
			deltaX: number;
			deltaY: number;
	  }
	| { type: "button"; button: number; down: boolean }
	| { type: "wheel"; deltaX: number; deltaY: number }
	| { type: "key"; key: number; down: boolean }
	| { type: "reset" };

type EditorPanelId =
	| "hierarchy"
	| "viewport"
	| "game"
	| "inspector"
	| "content"
	| "console"
	| "asset"
	| "performance";

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

interface EditorConsoleMessage {
	id: number;
	time: string;
	severity: "info" | "warning" | "error";
	source: string;
	text: string;
}

interface AssetDocument {
	reference: string;
	type: string;
	content: string;
	readOnly: boolean;
	message?: string;
}

interface MeshImportOptions {
	generateLods: boolean;
	optimizeVertexCache: boolean;
	optimizeOverdraw: boolean;
}

interface MeshLodState {
	index: number;
	triangles: number;
	maxScreenRadiusPixels: number;
}

interface MeshRigMapping {
	boneIndex: number;
	boneName: string;
	required: boolean;
	targetJointIndex: number;
	sourceBoneName: string;
	rotationOffsetDegrees: [number, number, number];
	copyTranslation: boolean;
	translationScale: number;
	duplicateTarget: boolean;
}

interface MeshAssetEditorState {
	reference: string;
	dirty: boolean;
	canReimport: boolean;
	vertexCount: number;
	triangleCount: number;
	submeshCount: number;
	lodCount: number;
	bounds: { center: [number, number, number]; radius: number };
	importOptions: MeshImportOptions;
	materials: string[];
	lods: MeshLodState[];
	skeleton: {
		joints: string[];
		enabled: boolean;
		mappings: MeshRigMapping[];
	};
}

interface SceneBakeSettings {
	lightmapResolution: number;
	lightmapTileSize: number;
	probeDirectionCount: number;
	indirectBounceSampleCount: number;
	bakeIndirectBounce: boolean;
	probeBounceStrength: number;
	lightmapBounceStrength: number;
	bakeProbeVolume: boolean;
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
	environmentPath: string;
	environmentIntensity: number;
	bakeRunning: boolean;
	bakeStatus: string;
	postProcessEffectTypes: string[];
	scriptClassNames: string[];
	scriptableObjectClassNames: string[];
	running: boolean;
	gizmoOperation: "translate" | "rotate" | "scale";
	gizmoSpace: "local" | "world";
	selectedEntityId: number;
	canUndo: boolean;
	canRedo: boolean;
	editorCamera: EditorCameraState;
	viewportStats: ViewportStats;
	viewportSettings: {
		debugView: number;
		debugShapes: boolean;
		snapEnabled: boolean;
		translateSnap: number;
		rotateSnap: number;
		scaleSnap: number;
	};
	meshAsset?: MeshAssetEditorState | null;
	entities: EditorEntity[];
}

interface PlutoEditorApi {
	detachPanel(
		panel: EditorPanelId,
		position: { x: number; y: number },
	): Promise<boolean>;
	dockPanel(panel: EditorPanelId): Promise<void>;
	minimizeWindow(): void;
	toggleMaximizeWindow(): void;
	closeWindow(): void;
	onPanelDockHover(
		callback: (panel: EditorPanelId, hovered: boolean) => void,
	): () => void;
	onPanelWindowClosed(callback: (panel: EditorPanelId) => void): () => void;
	setViewportBounds(bounds: ViewportBounds): void;
	setViewportVisible(visible: boolean): void;
	setGameViewportBounds(bounds: ViewportBounds): void;
	setGameViewportVisible(visible: boolean): void;
	sendViewportInput(input: ViewportInputEvent): void;
	sendGameViewportInput(input: ViewportInputEvent): void;
	onViewportFrame(callback: (frame: ViewportFrame) => void): () => void;
	onGameViewportFrame(callback: (frame: ViewportFrame) => void): () => void;
	setViewportOccluded(token: string, occluded: boolean): void;
	getHostState(): Promise<HostState>;
	restartHost(): Promise<void>;
	onHostState(callback: (state: HostState) => void): () => void;
	getHostPerformance(): Promise<HostPerformance | undefined>;
	onHostPerformance(
		callback: (performance: HostPerformance) => void,
	): () => void;
	getEditorOperation(): Promise<EditorOperationState>;
	onEditorOperation(
		callback: (operation: EditorOperationState) => void,
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
	getConsoleMessages(): Promise<EditorConsoleMessage[]>;
	onConsoleMessage(
		callback: (message: EditorConsoleMessage) => void,
	): () => void;
	onConsoleCleared(callback: () => void): () => void;
	clearConsole(): void;
	newScene(): Promise<void>;
	newProject(): Promise<void>;
	openProject(): Promise<void>;
	saveProject(): Promise<void>;
	saveProjectSettings(settings: ProjectSettings): Promise<boolean>;
	openScene(): Promise<void>;
	chooseEnvironmentMap(): Promise<string | undefined>;
	openAsset(reference: string): Promise<AssetDocument | undefined>;
	getActiveAsset(): Promise<AssetDocument | undefined>;
	onAssetOpened(
		callback: (asset: AssetDocument | undefined) => void,
	): () => void;
	saveAsset(reference: string, content: string): Promise<boolean>;
	setMeshImportOptions(options: MeshImportOptions): Promise<boolean>;
	reimportMesh(): Promise<boolean>;
	setMeshLodThreshold(index: number, maxScreenRadiusPixels: number): Promise<boolean>;
	autoMapMeshRig(): Promise<boolean>;
	disableMeshRetargeting(): Promise<boolean>;
	setMeshRigMapping(mapping: MeshRigMapping): Promise<boolean>;
	saveMesh(): Promise<boolean>;
	revertMesh(): Promise<boolean>;
	setAssetDirty(dirty: boolean): void;
	revealAsset(reference: string): Promise<void>;
	saveScene(saveAs?: boolean): Promise<void>;
	importModels(): Promise<ModelImportResult>;
	refreshAssets(): void;
	createAsset(
		type:
			| "material"
			| "post-process"
			| "particle-system"
			| "shader-graph"
			| "animation-graph"
			| "scriptable-object",
		reference: string,
		className?: string,
	): void;
	buildProject(runAfterBuild?: boolean): Promise<void>;
	bakeScene(
		preset: "fast" | "balanced" | "final" | "custom",
		settings?: SceneBakeSettings,
	): void;
	cancelBake(): void;
	buildScripts(): void;
	reloadScripts(): void;
	createScript(name: string): void;
	setForceShowCursor(enabled: boolean): void;
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
	copyEntity(id: number): void;
	pasteEntity(parentId?: number): void;
	saveEntityAsPrefab(id: number): void;
	createSkeletonAttachments(id: number): void;
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
	componentAction(
		entityId: number,
		componentIndex: number,
		action: string,
		index?: number,
	): void;
	setSceneEnvironment(path: string, intensity: number): void;
	setViewportDebugView(view: number): void;
	setViewportSettings(settings: EditorState["viewportSettings"]): void;
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
