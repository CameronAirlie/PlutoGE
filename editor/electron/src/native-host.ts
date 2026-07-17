import { app, BrowserWindow } from 'electron';
import { ChildProcessWithoutNullStreams, spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';

export type HostState = {
  status: 'starting' | 'ready' | 'stopped' | 'error';
  message?: string;
};

export type NativeEditorState = EditorState;

export class NativeHost {
  private process?: ChildProcessWithoutNullStreams;
  private state: HostState = { status: 'stopped' };
  private editorState?: NativeEditorState;
  private stopping = false;

  public constructor(
    private readonly window: BrowserWindow,
    private readonly onState: (state: HostState) => void,
    private readonly onEditorState: (state: NativeEditorState) => void,
  ) {}

  public getState(): HostState {
    return this.state;
  }

  public getEditorState(): NativeEditorState | undefined {
    return this.editorState;
  }

  public start(): void {
    if (this.process) return;
    if (process.platform !== 'win32') {
      this.updateState({ status: 'error', message: 'The embedded viewport currently requires Windows.' });
      return;
    }

    const executable = this.resolveExecutable();
    if (!executable) {
      this.updateState({
        status: 'error',
        message: 'Native host not found. Run tools/Start-ElectronEditor.ps1 from the repository root.',
      });
      return;
    }

    const nativeHandle = this.window.getNativeWindowHandle();
    const handle = nativeHandle.length >= 8
      ? nativeHandle.readBigUInt64LE(0)
      : BigInt(nativeHandle.readUInt32LE(0));

    this.stopping = false;
    this.updateState({ status: 'starting' });
    const child = spawn(executable, ['--parent-hwnd', `0x${handle.toString(16)}`], {
      cwd: path.dirname(executable),
      windowsHide: true,
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    this.process = child;

    const output = readline.createInterface({ input: child.stdout });
    output.on('line', (line) => {
      try {
        const event = JSON.parse(line) as { type?: string; message?: string } & Partial<NativeEditorState>;
        if (event.type === 'ready') this.updateState({ status: 'ready' });
        if (event.type === 'error') this.updateState({ status: 'error', message: event.message });
        if (event.type === 'editor-error' && event.message) console.warn(`[PlutoGEEditorHost] ${event.message}`);
        if (event.type === 'editor-state') {
          this.editorState = event as NativeEditorState;
          this.onEditorState(this.editorState);
        }
      } catch {
        // Engine diagnostics also use stdout; keep the IPC parser tolerant.
      }
    });

    const diagnostics = readline.createInterface({ input: child.stderr });
    diagnostics.on('line', (line) => {
      if (line.trim()) console.warn(`[PlutoGEEditorHost] ${line}`);
    });
    child.on('error', (error) => this.updateState({ status: 'error', message: error.message }));
    child.on('exit', (code) => {
      if (this.process !== child) return;
      this.process = undefined;
      if (!this.stopping && code !== 0) {
        this.updateState({ status: 'error', message: `Native host exited with code ${code ?? 'unknown'}.` });
      } else {
        this.updateState({ status: 'stopped' });
      }
    });
  }

  public send(command: string): void {
    if (this.process?.stdin.writable) this.process.stdin.write(`${command}\n`);
  }

  public async restart(): Promise<void> {
    await this.stop();
    this.start();
  }

  public async stop(): Promise<void> {
    const child = this.process;
    if (!child) return;

    this.stopping = true;
    this.send('shutdown');
    await new Promise<void>((resolve) => {
      let settled = false;
      let forceFinishTimeout: NodeJS.Timeout;
      const finish = (): void => {
        if (settled) return;
        settled = true;
        clearTimeout(killTimeout);
        clearTimeout(forceFinishTimeout);
        if (this.process === child) this.process = undefined;
        resolve();
      };
      const killTimeout = setTimeout(() => {
        if (!child.killed) child.kill();
      }, 1500);
      child.once('exit', () => {
        finish();
      });
      forceFinishTimeout = setTimeout(finish, 2500);
    });
  }

  private resolveExecutable(): string | undefined {
    const configured = process.env.PLUTOGE_ENGINE_HOST;
    const candidates = [
      configured,
      app.isPackaged ? path.join(process.resourcesPath, 'engine', 'PlutoGEEditorHost.exe') : undefined,
      path.resolve(app.getAppPath(), '../../out/build/electron-editor/bin/Debug/PlutoGEEditorHost.exe'),
      path.resolve(app.getAppPath(), '../../out/build/msvc-shared-engine/bin/Debug/PlutoGEEditorHost.exe'),
    ];
    return candidates.find((candidate): candidate is string => Boolean(candidate && fs.existsSync(candidate)));
  }

  private updateState(state: HostState): void {
    this.state = state;
    this.onState(state);
  }
}
