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

interface PlutoEditorApi {
  setViewportBounds(bounds: ViewportBounds): void;
  setViewportVisible(visible: boolean): void;
  getHostState(): Promise<HostState>;
  restartHost(): Promise<void>;
  onHostState(callback: (state: HostState) => void): () => void;
}

interface Window {
  plutoEditor: PlutoEditorApi;
}
