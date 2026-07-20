import type React from "react";
import { useEffect, useState } from "react";
import { PanelFrame } from "../components/PanelFrame";

export function AssetEditorPanel(): React.JSX.Element {
	const [asset, setAsset] = useState<AssetDocument>();
	const [content, setContent] = useState("");
	const [savedContent, setSavedContent] = useState("");
	const [message, setMessage] = useState("");

	useEffect(() => {
		const apply = (next: AssetDocument | undefined): void => {
			if (!next) {
				setAsset(undefined);
				setContent("");
				setSavedContent("");
				setMessage("");
				return;
			}
			setAsset(next);
			setContent(next.content);
			setSavedContent(next.content);
			setMessage(next.message ?? "");
		};
		void window.plutoEditor.getActiveAsset().then(apply);
		return window.plutoEditor.onAssetOpened(apply);
	}, []);
	useEffect(() => {
		window.plutoEditor.setAssetDirty(
			Boolean(asset && content !== savedContent),
		);
	}, [asset, content, savedContent]);

	const save = async (): Promise<void> => {
		if (!asset) return;
		const saved = await window.plutoEditor.saveAsset(asset.reference, content);
		setMessage(saved ? "Saved." : "Could not save this asset.");
		if (saved) setSavedContent(content);
	};

	return (
		<PanelFrame
			id="asset"
			title={asset ? `${asset.type} Editor` : "Asset Editor"}
			actions={
				asset ? (
					<>
						<button
							type="button"
							disabled={asset.readOnly || content === savedContent}
							onClick={() => void save()}
						>
							Save
						</button>
						<button
							type="button"
							disabled={content === savedContent}
							onClick={() => setContent(savedContent)}
						>
							Revert
						</button>
						<button
							type="button"
							onClick={() =>
								void window.plutoEditor.revealAsset(asset.reference)
							}
						>
							Show in Explorer
						</button>
					</>
				) : undefined
			}
			className="asset-editor-panel"
		>
			{!asset ? (
				<div className="empty-state">
					Open an asset from the Content Browser.
				</div>
			) : (
				<>
					<div className="asset-editor-heading">
						<strong>{asset.reference}</strong>
						{content !== savedContent && <span>Modified</span>}
					</div>
					{message && <div className="import-message">{message}</div>}
					{asset.readOnly ? (
						<div className="empty-state">{asset.message}</div>
					) : (
						<textarea
							className="asset-source-editor"
							spellCheck={false}
							value={content}
							onChange={(event) => setContent(event.currentTarget.value)}
							onKeyDown={(event) => {
								if (
									(event.ctrlKey || event.metaKey) &&
									event.key.toLowerCase() === "s"
								) {
									event.preventDefault();
									void save();
								}
							}}
						/>
					)}
				</>
			)}
		</PanelFrame>
	);
}
