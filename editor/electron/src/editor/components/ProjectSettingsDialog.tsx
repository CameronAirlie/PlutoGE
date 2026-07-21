import React, { useEffect, useMemo, useState } from "react";
import { createPortal } from "react-dom";

const displayReference = (reference: string): string =>
	reference.replace(/^project:\/\//, "");

export function ProjectSettingsDialog({
	editor,
	onClose,
}: {
	editor: EditorState;
	onClose(): void;
}): React.JSX.Element {
	const [settings, setSettings] = useState<ProjectSettings>({
		...editor.projectSettings,
	});
	const [saving, setSaving] = useState(false);
	const [error, setError] = useState("");
	const sceneReferences = useMemo(
		() =>
			editor.assets
				.filter((asset) => asset.type === "Scene")
				.map((asset) => asset.reference)
				.sort((left, right) => left.localeCompare(right)),
		[editor.assets],
	);
	const assemblyReferences = useMemo(
		() =>
			editor.assets
				.filter((asset) => asset.type === "Assembly")
				.map((asset) => asset.reference)
				.sort((left, right) => left.localeCompare(right)),
		[editor.assets],
	);

	useEffect(() => {
		window.plutoEditor.setViewportOccluded("project-settings-dialog", true);
		return () =>
			window.plutoEditor.setViewportOccluded("project-settings-dialog", false);
	}, []);

	const valid =
		settings.name.trim().length > 0 &&
		Number.isFinite(settings.windowWidth) &&
		Number.isFinite(settings.windowHeight) &&
		settings.windowWidth >= 64 &&
		settings.windowWidth <= 16384 &&
		settings.windowHeight >= 64 &&
		settings.windowHeight <= 16384;

	const save = async (): Promise<void> => {
		if (!valid || saving) return;
		setSaving(true);
		setError("");
		const saved = await window.plutoEditor.saveProjectSettings({
			...settings,
			name: settings.name.trim(),
			windowWidth: Math.round(settings.windowWidth),
			windowHeight: Math.round(settings.windowHeight),
		});
		if (saved) onClose();
		else {
			setError("Could not save the project settings.");
			setSaving(false);
		}
	};

	return createPortal(
		<div
			className="dialog-backdrop"
			onMouseDown={() => {
				if (!saving) onClose();
			}}
		>
			<form
				className="project-settings-dialog"
				onMouseDown={(event) => event.stopPropagation()}
				onKeyDown={(event) => {
					if (event.key === "Escape" && !saving) onClose();
				}}
				onSubmit={(event) => {
					event.preventDefault();
					void save();
				}}
			>
				<header>
					<div>
						<h3>Project Settings</h3>
						<p>Configure the project and its standalone runtime.</p>
					</div>
					<button
						type="button"
						className="dialog-close"
						disabled={saving}
						aria-label="Close project settings"
						onClick={onClose}
					>
						×
					</button>
				</header>

				<div className="project-settings-body">
					<section>
						<h4>General</h4>
						<label className="settings-field">
							<span>Project name</span>
							<input
								autoFocus
								value={settings.name}
								onChange={(event) =>
									setSettings((current) => ({
										...current,
										name: event.currentTarget.value,
									}))
								}
							/>
						</label>
						<label className="settings-field">
							<span>Startup scene</span>
							<select
								value={settings.startupScene}
								onChange={(event) =>
									setSettings((current) => ({
										...current,
										startupScene: event.currentTarget.value,
									}))
								}
							>
								<option value="">None</option>
								{settings.startupScene &&
								!sceneReferences.includes(settings.startupScene) ? (
									<option value={settings.startupScene}>
										{displayReference(settings.startupScene)} (current)
									</option>
								) : null}
								{sceneReferences.map((reference) => (
									<option key={reference} value={reference}>
										{displayReference(reference)}
									</option>
								))}
							</select>
						</label>
						<label className="settings-field">
							<span>Script assembly</span>
							<select
								value={settings.scriptAssembly}
								onChange={(event) =>
									setSettings((current) => ({
										...current,
										scriptAssembly: event.currentTarget.value,
									}))
								}
							>
								<option value="">None</option>
								{settings.scriptAssembly &&
								!assemblyReferences.includes(settings.scriptAssembly) ? (
									<option value={settings.scriptAssembly}>
										{displayReference(settings.scriptAssembly)} (current)
									</option>
								) : null}
								{assemblyReferences.map((reference) => (
									<option key={reference} value={reference}>
										{displayReference(reference)}
									</option>
								))}
							</select>
						</label>
					</section>

					<section>
						<h4>Runtime Window</h4>
						<label className="settings-field">
							<span>Title</span>
							<input
								value={settings.windowTitle}
								onChange={(event) =>
									setSettings((current) => ({
										...current,
										windowTitle: event.currentTarget.value,
									}))
								}
							/>
						</label>
						<div className="settings-size-row">
							<label className="settings-field">
								<span>Width</span>
								<input
									type="number"
									min={64}
									max={16384}
									step={1}
									value={settings.windowWidth}
									onChange={(event) =>
										setSettings((current) => ({
											...current,
											windowWidth: event.currentTarget.valueAsNumber,
										}))
									}
								/>
							</label>
							<label className="settings-field">
								<span>Height</span>
								<input
									type="number"
									min={64}
									max={16384}
									step={1}
									value={settings.windowHeight}
									onChange={(event) =>
										setSettings((current) => ({
											...current,
											windowHeight: event.currentTarget.valueAsNumber,
										}))
									}
								/>
							</label>
						</div>
						<label className="settings-toggle">
							<span>
								<strong>Vertical sync</strong>
								<small>
									Synchronize presentation with the display refresh rate.
								</small>
							</span>
							<input
								type="checkbox"
								checked={settings.vSyncEnabled}
								onChange={(event) =>
									setSettings((current) => ({
										...current,
										vSyncEnabled: event.currentTarget.checked,
									}))
								}
							/>
						</label>
					</section>
				</div>

				<div className="settings-project-path" title={editor.projectPath}>
					{editor.projectPath}
				</div>
				{error ? <p className="dialog-error">{error}</p> : null}
				<div className="dialog-actions">
					<button type="button" disabled={saving} onClick={onClose}>
						Cancel
					</button>
					<button type="submit" disabled={!valid || saving}>
						{saving ? "Saving…" : "Save Settings"}
					</button>
				</div>
			</form>
		</div>,
		document.body,
	);
}
