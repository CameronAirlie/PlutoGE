import { app, BrowserWindow } from "electron";
import { ChildProcessWithoutNullStreams, spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";

export type HostState = {
	status: "starting" | "ready" | "stopped" | "error";
	message?: string;
};

export type NativeEditorState = EditorState;

export type HostPerformance = {
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
};

export class NativeHost {
	private static readonly heartbeatIntervalMs = 2_000;
	private static readonly unresponsiveTimeoutMs = 30_000;

	private process?: ChildProcessWithoutNullStreams;
	private state: HostState = { status: "stopped" };
	private editorState?: NativeEditorState;
	private performance?: HostPerformance;
	private stopping = false;
	private heartbeatTimer?: NodeJS.Timeout;
	private lastHeartbeatResponse = 0;
	private heartbeatSequence = 0;
	private terminatedAsUnresponsive = false;
	private readonly diagnostics: string[] = [];

	public constructor(
		private window: BrowserWindow,
		private readonly onState: (state: HostState) => void,
		private readonly onEditorState: (state: NativeEditorState) => void,
		private readonly onPerformance: (performance: HostPerformance) => void,
		private readonly extraArguments: string[] = [],
		private readonly diagnosticLabel = "PlutoGEEditorHost",
	) {}

	public getState(): HostState {
		return this.state;
	}

	public getEditorState(): NativeEditorState | undefined {
		return this.editorState;
	}

	public getPerformance(): HostPerformance | undefined {
		return this.performance;
	}

	public start(): void {
		if (this.process) return;
		if (process.platform !== "win32") {
			this.updateState({
				status: "error",
				message: "The embedded viewport currently requires Windows.",
			});
			return;
		}

		const executable = this.resolveExecutable();
		if (!executable) {
			this.updateState({
				status: "error",
				message:
					"Native host not found. Run tools/Start-ElectronEditor.ps1 from the repository root.",
			});
			return;
		}

		const nativeHandle = this.window.getNativeWindowHandle();
		const handle =
			nativeHandle.length >= 8
				? nativeHandle.readBigUInt64LE(0)
				: BigInt(nativeHandle.readUInt32LE(0));

		this.stopping = false;
		this.terminatedAsUnresponsive = false;
		this.performance = undefined;
		this.stopHeartbeat();
		this.diagnostics.length = 0;
		this.updateState({ status: "starting" });
		const child = spawn(
			executable,
			["--parent-hwnd", `0x${handle.toString(16)}`, ...this.extraArguments],
			{
				cwd: path.dirname(executable),
				windowsHide: true,
				stdio: ["pipe", "pipe", "pipe"],
			},
		);
		this.process = child;

		const output = readline.createInterface({ input: child.stdout });
		output.on("line", (line) => {
			try {
				const event = JSON.parse(line) as {
					type?: string;
					message?: string;
				} & Partial<NativeEditorState> &
					Partial<HostPerformance>;
				if (event.type === "ready") {
					this.updateState({ status: "ready" });
					this.startHeartbeat(child);
				}
				if (event.type === "pong") this.lastHeartbeatResponse = Date.now();
				if (event.type === "error")
					this.updateState({ status: "error", message: event.message });
				if (event.type === "editor-error" && event.message)
					console.warn(`[${this.diagnosticLabel}] ${event.message}`);
				if (event.type === "editor-state") {
					this.editorState = event as NativeEditorState;
					this.onEditorState(this.editorState);
				}
				if (event.type === "performance") {
					this.performance = event as HostPerformance;
					this.onPerformance(this.performance);
				}
			} catch {
				// Engine diagnostics also use stdout; keep the IPC parser tolerant.
			}
		});

		const diagnostics = readline.createInterface({ input: child.stderr });
		diagnostics.on("line", (line) => {
			if (line.trim()) {
				this.diagnostics.push(line.trim());
				if (this.diagnostics.length > 40) this.diagnostics.shift();
				console.warn(`[${this.diagnosticLabel}] ${line}`);
			}
		});
		child.on("error", (error) =>
			this.updateState({ status: "error", message: error.message }),
		);
		child.on("exit", (code) => {
			if (this.process !== child) return;
			this.stopHeartbeat();
			this.process = undefined;
			if (this.terminatedAsUnresponsive) {
				this.updateState({
					status: "error",
					message: `${this.diagnosticLabel} stopped responding and was terminated. Unsaved native scene changes may be lost; you can restart just this viewport instead of closing the editor.`,
				});
			} else if (!this.stopping && code !== 0) {
				const detail = this.diagnostics.length
					? `\n${this.diagnostics.join("\n")}`
					: "";
				this.updateState({
					status: "error",
					message: `Native host exited with code ${code ?? "unknown"}.${detail}`,
				});
			} else {
				this.updateState({ status: "stopped" });
			}
		});
	}

	public send(command: string): void {
		if (this.process?.stdin.writable) this.process.stdin.write(`${command}\n`);
	}

	public setOwnerWindow(window: BrowserWindow): void {
		if (this.window === window) return;
		this.window = window;
		if (!this.process) return;
		const nativeHandle = window.getNativeWindowHandle();
		const handle =
			nativeHandle.length >= 8
				? nativeHandle.readBigUInt64LE(0)
				: BigInt(nativeHandle.readUInt32LE(0));
		this.send(`owner 0x${handle.toString(16)}`);
	}

	public async restart(): Promise<void> {
		await this.stop();
		this.start();
	}

	public async stop(): Promise<void> {
		const child = this.process;
		if (!child) return;

		this.stopping = true;
		this.stopHeartbeat();
		this.send("shutdown");
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
			child.once("exit", () => {
				finish();
			});
			forceFinishTimeout = setTimeout(finish, 2500);
		});
	}

	private resolveExecutable(): string | undefined {
		const configured = process.env.PLUTOGE_ENGINE_HOST;
		const candidates = [
			configured,
			app.isPackaged
				? path.join(process.resourcesPath, "engine", "PlutoGEEditorHost.exe")
				: undefined,
			path.resolve(
				app.getAppPath(),
				"../../out/build/electron-editor/bin/Debug/PlutoGEEditorHost.exe",
			),
			path.resolve(
				app.getAppPath(),
				"../../out/build/msvc-shared-engine/bin/Debug/PlutoGEEditorHost.exe",
			),
		];
		return candidates.find((candidate): candidate is string =>
			Boolean(candidate && fs.existsSync(candidate)),
		);
	}

	private startHeartbeat(child: ChildProcessWithoutNullStreams): void {
		this.stopHeartbeat();
		this.lastHeartbeatResponse = Date.now();
		this.heartbeatTimer = setInterval(() => {
			if (this.process !== child || this.stopping) {
				this.stopHeartbeat();
				return;
			}

			if (
				Date.now() - this.lastHeartbeatResponse >=
				NativeHost.unresponsiveTimeoutMs
			) {
				this.terminatedAsUnresponsive = true;
				this.stopHeartbeat();
				if (!child.killed) child.kill();
				return;
			}

			this.send(`ping ${++this.heartbeatSequence}`);
		}, NativeHost.heartbeatIntervalMs);
		this.heartbeatTimer.unref();
	}

	private stopHeartbeat(): void {
		if (!this.heartbeatTimer) return;
		clearInterval(this.heartbeatTimer);
		this.heartbeatTimer = undefined;
	}

	private updateState(state: HostState): void {
		this.state = state;
		this.onState(state);
	}
}
