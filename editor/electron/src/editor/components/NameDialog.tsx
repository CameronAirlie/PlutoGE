import React, { useEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";

export function NameDialog({
	title,
	label = "Name",
	confirmLabel = "Create",
	onConfirm,
	onClose,
}: {
	title: string;
	label?: string;
	confirmLabel?: string;
	onConfirm(value: string): void;
	onClose(): void;
}): React.JSX.Element {
	const [value, setValue] = useState("");
	const input = useRef<HTMLInputElement>(null);
	useEffect(() => {
		input.current?.focus();
		window.plutoEditor.setViewportOccluded("name-dialog", true);
		return () => window.plutoEditor.setViewportOccluded("name-dialog", false);
	}, []);
	const submit = (): void => {
		if (value.trim()) onConfirm(value.trim());
	};
	return createPortal(
		<div className="dialog-backdrop" onMouseDown={onClose}>
			<form
				className="name-dialog"
				onMouseDown={(event) => event.stopPropagation()}
				onSubmit={(event) => {
					event.preventDefault();
					submit();
				}}
			>
				<h3>{title}</h3>
				<label>
					<span>{label}</span>
					<input
						ref={input}
						value={value}
						onChange={(event) => setValue(event.currentTarget.value)}
						onKeyDown={(event) => {
							if (event.key === "Escape") onClose();
						}}
					/>
				</label>
				<div className="dialog-actions">
					<button type="button" onClick={onClose}>
						Cancel
					</button>
					<button type="submit" disabled={!value.trim()}>
						{confirmLabel}
					</button>
				</div>
			</form>
		</div>,
		document.body,
	);
}
