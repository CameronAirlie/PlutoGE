import React, { useEffect, useRef, useState } from "react";
import { Button, Modal, TextField } from "./ui";

export function NameDialog({
	title,
	label = "Name",
	confirmLabel = "Create",
	initialValue = "",
	onConfirm,
	onClose,
}: {
	title: string;
	label?: string;
	confirmLabel?: string;
	initialValue?: string;
	onConfirm(value: string): void;
	onClose(): void;
}): React.JSX.Element {
	const [value, setValue] = useState(initialValue);
	const input = useRef<HTMLInputElement>(null);
	useEffect(() => {
		input.current?.focus();
		window.plutoEditor.setViewportOccluded("name-dialog", true);
		return () => window.plutoEditor.setViewportOccluded("name-dialog", false);
	}, []);
	const submit = (): void => {
		if (value.trim()) onConfirm(value.trim());
	};
	return (
		<Modal title={title} onOpenChange={(open) => { if (!open) onClose(); }}>
			<form
				className="name-dialog"
				onSubmit={(event) => {
					event.preventDefault();
					submit();
				}}
			>
				<label>
					<span>{label}</span>
					<TextField
						ref={input}
						value={value}
						onChange={(event) => setValue(event.currentTarget.value)}
						onKeyDown={(event) => {
							if (event.key === "Escape") onClose();
						}}
					/>
				</label>
				<div className="dialog-actions">
					<Button onClick={onClose}>
						Cancel
					</Button>
					<Button type="submit" variant="primary" disabled={!value.trim()}>
						{confirmLabel}
					</Button>
				</div>
			</form>
		</Modal>
	);
}
