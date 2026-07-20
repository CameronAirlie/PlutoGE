import { type ChildProcessWithoutNullStreams, spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";
import { app, type BrowserWindow } from "electron";

export type HostState = {
	status: "starting" | "ready" | "stopped" | "error";
	message?: string;
};

export type NativeEditorState = EditorState;

export type HostOperationResult = {
	token: string;
	success: boolean;
	message?: string;
};

export type HostPerformance = {
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
};

export class NativeHost {
	private static readonly heartbeatIntervalMs = 2_000;
	private static readonly unresponsiveTimeoutMs = 30_000;
	private static readonly operationTimeoutMs = 5 * 60_000;

	private process?: ChildProcessWithoutNullStreams;
	private state: HostState = { status: "stopped" };
	private editorState?: NativeEditorState;
	private performance?: HostPerformance;
	private stopping = false;
	private heartbeatTimer?: NodeJS.Timeout;
	private lastHeartbeatResponse = 0;
	private heartbeatSequence = 0;
	private terminatedAsUnresponsive = false;
	private activeOperationToken?: string;
	private operationStartedAt = 0;
	private writeBlocked = false;
	private readonly pendingCommands: string[] = [];
	private readonly diagnostics: string[] = [];

	public constructor(
		private window: BrowserWindow,
		private readonly onState: (state: HostState) => void,
		private readonly onEditorState: (state: NativeEditorState) => void,
		private readonly onPerformance: (performance: HostPerformance) => void,
		private readonly extraArguments: string[] = [],
		private readonly diagnosticLabel = "PlutoGEEditorHost",
		private readonly onOperationComplete: (
			result: HostOperationResult,
		) => void = () => {},
		private readonly onDiagnostic: (
			severity: "info" | "warning" | "error",
			message: string,
		) => void = () => {},
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

	public getProcessId(): number | undefined {
		return this.process?.pid;
	}

	public hasActiveOperation(): boolean {
		return this.activeOperationToken !== undefined;
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
		this.activeOperationToken = undefined;
		this.operationStartedAt = 0;
		this.performance = undefined;
		this.writeBlocked = false;
		this.pendingCommands.length = 0;
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
		child.stdin.on("error", (error) => {
			// A viewport can exit between an IPC handler checking `writable` and
			// Node completing the pipe write. Handle that race locally instead of
			// letting an unhandled EPIPE take down Electron's main process.
			if (this.process === child && !this.stopping)
				console.warn(
					`[${this.diagnosticLabel}] command pipe: ${error.message}`,
				);
		});

		const output = readline.createInterface({ input: child.stdout });
		output.on("line", (line) => {
			try {
				const event = JSON.parse(line) as {
					type?: string;
					message?: string;
					token?: string;
					success?: boolean;
				} & Partial<NativeEditorState> &
					Partial<HostPerformance>;
				if (event.type === "ready") {
					this.updateState({ status: "ready" });
					this.startHeartbeat(child);
				}
				if (event.type === "pong") this.lastHeartbeatResponse = Date.now();
				if (event.type === "error")
					this.updateState({ status: "error", message: event.message });
				if (event.type === "editor-error" && event.message) {
					console.warn(`[${this.diagnosticLabel}] ${event.message}`);
					this.onDiagnostic("error", event.message);
				}
				if (event.type === "editor-log" && event.message)
					this.onDiagnostic(
						event.success === false ? "warning" : "info",
						event.message,
					);
				if (event.type === "editor-state") {
					this.editorState = event as NativeEditorState;
					this.onEditorState(this.editorState);
				}
				if (event.type === "performance") {
					this.performance = event as HostPerformance;
					this.onPerformance(this.performance);
				}
				if (
					event.type === "operation-complete" &&
					typeof event.token === "string" &&
					typeof event.success === "boolean"
				) {
					if (event.token === this.activeOperationToken) {
						this.activeOperationToken = undefined;
						this.operationStartedAt = 0;
						this.lastHeartbeatResponse = Date.now();
					}
					this.onOperationComplete({
						token: event.token,
						success: event.success,
						message: event.message,
					});
				}
			} catch {
				// Engine diagnostics also use stdout; keep the IPC parser tolerant.
				const diagnostic = line.trim();
				if (diagnostic) {
					const severity =
						/: error (?:CS|MSB|NU)\d+|\bBuild FAILED\b/i.test(diagnostic)
							? "error"
							: /: warning (?:CS|MSB|NU)\d+|\bwarning\b/i.test(diagnostic)
								? "warning"
								: "info";
					this.onDiagnostic(severity, diagnostic);
				}
			}
		});

		const diagnostics = readline.createInterface({ input: child.stderr });
		diagnostics.on("line", (line) => {
			if (line.trim()) {
				this.diagnostics.push(line.trim());
				if (this.diagnostics.length > 40) this.diagnostics.shift();
				console.warn(`[${this.diagnosticLabel}] ${line}`);
				this.onDiagnostic("warning", line.trim());
			}
		});
		child.on("error", (error) =>
			this.updateState({ status: "error", message: error.message }),
		);
		child.on("exit", (code) => {
			if (this.process !== child) return;
			this.stopHeartbeat();
			this.process = undefined;
			this.writeBlocked = false;
			this.pendingCommands.length = 0;
			this.activeOperationToken = undefined;
			this.operationStartedAt = 0;
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
		const child = this.process;
		if (!child?.stdin.writable) return;
		if (this.writeBlocked) {
			this.enqueueCommand(command);
			return;
		}

		try {
			if (!child.stdin.write(`${command}\n`)) {
				this.writeBlocked = true;
				child.stdin.once("drain", () => this.flushCommands(child));
			}
		} catch (error) {
			if (!this.stopping)
				console.warn(
					`[${this.diagnosticLabel}] command write failed: ${error instanceof Error ? error.message : String(error)}`,
				);
		}
	}

	public sendOperation(command: string | string[], token: string): boolean {
		if (!this.process?.stdin.writable || this.activeOperationToken)
			return false;
		const commands = Array.isArray(command) ? command : [command];
		if (!commands.length) return false;
		this.activeOperationToken = token;
		this.operationStartedAt = Date.now();
		this.lastHeartbeatResponse = this.operationStartedAt;
		commands.forEach((item, index) => {
			this.send(
				index === commands.length - 1 ? `${item} request=${token}` : item,
			);
		});
		return true;
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

			const now = Date.now();
			const timedOut = this.activeOperationToken
				? now - this.operationStartedAt >= NativeHost.operationTimeoutMs
				: now - this.lastHeartbeatResponse >= NativeHost.unresponsiveTimeoutMs;
			if (timedOut) {
				this.terminatedAsUnresponsive = true;
				this.stopHeartbeat();
				if (!child.killed) child.kill();
				return;
			}

			this.send(`ping ${++this.heartbeatSequence}`);
		}, NativeHost.heartbeatIntervalMs);
		this.heartbeatTimer.unref();
	}

	private enqueueCommand(command: string): void {
		const commandName = command.split(" ", 1)[0];
		// These commands describe current transport/window state. When a slow
		// native frame applies backpressure, only their newest value matters.
		// Coalescing them prevents resize, visibility and heartbeat traffic from
		// growing an unbounded queue while preserving every editor mutation.
		if (["bounds", "visible", "owner", "ping"].includes(commandName)) {
			const existing = this.pendingCommands.findIndex(
				(candidate) => candidate.split(" ", 1)[0] === commandName,
			);
			if (existing >= 0) this.pendingCommands.splice(existing, 1);
		}
		this.pendingCommands.push(command);
	}

	private flushCommands(child: ChildProcessWithoutNullStreams): void {
		if (this.process !== child || this.stopping || !child.stdin.writable) {
			this.writeBlocked = false;
			this.pendingCommands.length = 0;
			return;
		}

		this.writeBlocked = false;
		while (this.pendingCommands.length) {
			const command = this.pendingCommands.shift();
			if (command === undefined) break;
			try {
				if (!child.stdin.write(`${command}\n`)) {
					this.writeBlocked = true;
					child.stdin.once("drain", () => this.flushCommands(child));
					return;
				}
			} catch (error) {
				if (!this.stopping)
					console.warn(
						`[${this.diagnosticLabel}] command write failed: ${error instanceof Error ? error.message : String(error)}`,
					);
				this.pendingCommands.length = 0;
				return;
			}
		}
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
