import React, { useEffect, useLayoutEffect, useRef } from "react";

type ViewportKind = "scene" | "game";

const keyCodeForEvent = (code: string): number | undefined => {
	if (/^Key[A-Z]$/.test(code)) return code.charCodeAt(3);
	if (/^Digit[0-9]$/.test(code)) return code.charCodeAt(5);
	const keys: Record<string, number> = {
		Space: 32,
		Escape: 256,
		Enter: 257,
		Tab: 258,
		Backspace: 259,
		Insert: 260,
		Delete: 261,
		ArrowRight: 262,
		ArrowLeft: 263,
		ArrowDown: 264,
		ArrowUp: 265,
		ShiftLeft: 340,
		ControlLeft: 341,
		AltLeft: 342,
		MetaLeft: 343,
		ShiftRight: 344,
		ControlRight: 345,
		AltRight: 346,
		MetaRight: 347,
	};
	return keys[code];
};

const engineButton = (browserButton: number): number => {
	if (browserButton === 1) return 2;
	if (browserButton === 2) return 1;
	return browserButton;
};

export function EngineViewportCanvas({
	kind,
}: {
	kind: ViewportKind;
}): React.JSX.Element {
	const canvasRef = useRef<HTMLCanvasElement>(null);
	const sourceSize = useRef({ width: 1, height: 1 });
	const pointer = useRef({ x: 0, y: 0, deltaX: 0, deltaY: 0 });
	const pointerFrame = useRef(0);

	const sendInput = (input: ViewportInputEvent): void => {
		if (kind === "scene") window.plutoEditor.sendViewportInput(input);
		else window.plutoEditor.sendGameViewportInput(input);
	};

	const setVisible = (visible: boolean): void => {
		if (kind === "scene") window.plutoEditor.setViewportVisible(visible);
		else window.plutoEditor.setGameViewportVisible(visible);
	};

	const flushPointer = (): void => {
		pointerFrame.current = 0;
		const current = pointer.current;
		sendInput({
			type: "pointer",
			x: current.x,
			y: current.y,
			deltaX: current.deltaX,
			deltaY: current.deltaY,
		});
		current.deltaX = 0;
		current.deltaY = 0;
	};

	const schedulePointer = (): void => {
		if (!pointerFrame.current)
			pointerFrame.current = requestAnimationFrame(flushPointer);
	};

	const updatePointerPosition = (event: React.PointerEvent): void => {
		const canvas = canvasRef.current;
		if (!canvas) return;
		const bounds = canvas.getBoundingClientRect();
		if (bounds.width <= 0 || bounds.height <= 0) return;
		if (document.pointerLockElement !== canvas) {
			pointer.current.x =
				((event.clientX - bounds.left) * sourceSize.current.width) /
				bounds.width;
			pointer.current.y =
				((event.clientY - bounds.top) * sourceSize.current.height) /
				bounds.height;
		}
		pointer.current.deltaX += event.movementX;
		pointer.current.deltaY += event.movementY;
		schedulePointer();
	};

	useLayoutEffect(() => {
		const canvas = canvasRef.current;
		if (!canvas) return;
		let animationFrame = 0;
		let previousBounds = "";
		const updateBounds = (): void => {
			const bounds = canvas.getBoundingClientRect();
			const scale = window.devicePixelRatio;
			const next = [bounds.left, bounds.top, bounds.width, bounds.height].map(
				(value) => Math.round(value * scale),
			);
			const key = next.join(",");
			if (key !== previousBounds && next[2] > 0 && next[3] > 0) {
				previousBounds = key;
				sourceSize.current = { width: next[2], height: next[3] };
				const value = {
					x: next[0],
					y: next[1],
					width: next[2],
					height: next[3],
				};
				if (kind === "scene") window.plutoEditor.setViewportBounds(value);
				else window.plutoEditor.setGameViewportBounds(value);
			}
			animationFrame = requestAnimationFrame(updateBounds);
		};
		updateBounds();
		return () => {
			cancelAnimationFrame(animationFrame);
			setVisible(false);
		};
	}, [kind]);

	useEffect(() => {
		const updateVisibility = (): void => setVisible(!document.hidden);
		document.addEventListener("visibilitychange", updateVisibility);
		updateVisibility();
		return () =>
			document.removeEventListener("visibilitychange", updateVisibility);
	}, [kind]);

	useEffect(() => {
		const receiveFrame = (frame: ViewportFrame): void => {
			const canvas = canvasRef.current;
			if (!canvas || frame.transport !== "cpu") return;
			const expectedLength = frame.width * frame.height * 4;
			const bytes = frame.pixels;
			if (bytes.byteLength !== expectedLength) return;
			const pixels = new Uint8ClampedArray(expectedLength);
			pixels.set(bytes);
			canvas.width = frame.width;
			canvas.height = frame.height;
			sourceSize.current = {
				width: frame.sourceWidth,
				height: frame.sourceHeight,
			};
			canvas
				.getContext("2d", { alpha: false })
				?.putImageData(new ImageData(pixels, frame.width, frame.height), 0, 0);
		};
		return kind === "scene"
			? window.plutoEditor.onViewportFrame(receiveFrame)
			: window.plutoEditor.onGameViewportFrame(receiveFrame);
	}, [kind]);

	useEffect(() => {
		const reset = (): void => sendInput({ type: "reset" });
		const pointerLockChanged = (): void => {
			if (document.pointerLockElement !== canvasRef.current) reset();
		};
		window.addEventListener("blur", reset);
		document.addEventListener("pointerlockchange", pointerLockChanged);
		return () => {
			window.removeEventListener("blur", reset);
			document.removeEventListener("pointerlockchange", pointerLockChanged);
			if (pointerFrame.current) cancelAnimationFrame(pointerFrame.current);
			reset();
		};
	}, [kind]);

	return (
		<canvas
			ref={canvasRef}
			data-viewport-kind={kind}
			className="viewport-canvas"
			tabIndex={0}
			aria-label={kind === "scene" ? "Engine viewport canvas" : "Game viewport canvas"}
			onContextMenu={(event) => event.preventDefault()}
			onPointerMove={updatePointerPosition}
			onPointerDown={(event) => {
				const canvas = event.currentTarget;
				canvas.focus();
				updatePointerPosition(event);
				flushPointer();
				const button = engineButton(event.button);
				sendInput({ type: "button", button, down: true });
				if (event.button === 2 && kind === "scene") {
					void canvas.requestPointerLock();
				} else {
					canvas.setPointerCapture(event.pointerId);
				}
			}}
			onPointerUp={(event) => {
				updatePointerPosition(event);
				flushPointer();
				sendInput({
					type: "button",
					button: engineButton(event.button),
					down: false,
				});
				if (event.currentTarget.hasPointerCapture(event.pointerId))
					event.currentTarget.releasePointerCapture(event.pointerId);
				if (
					event.button === 2 &&
					document.pointerLockElement === event.currentTarget
				)
					document.exitPointerLock();
			}}
			onWheel={(event) => {
				event.preventDefault();
				const unit = event.deltaMode === WheelEvent.DOM_DELTA_PIXEL ? 100 : 1;
				sendInput({
					type: "wheel",
					deltaX: -event.deltaX / unit,
					deltaY: -event.deltaY / unit,
				});
			}}
			onKeyDown={(event) => {
				const key = keyCodeForEvent(event.code);
				if (key === undefined || event.repeat) return;
				sendInput({ type: "key", key, down: true });
				if (!event.ctrlKey && !event.metaKey) event.preventDefault();
			}}
			onKeyUp={(event) => {
				const key = keyCodeForEvent(event.code);
				if (key === undefined) return;
				sendInput({ type: "key", key, down: false });
			}}
			onBlur={() => sendInput({ type: "reset" })}
		/>
	);
}
