import type React from "react";
import { useEffect, useState } from "react";
import { PanelFrame } from "../components/PanelFrame";
import { GraphAssetEditor } from "./GraphAssetEditor";

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
	const isGraph = asset?.type === "Shader Graph" || asset?.type === "ShaderGraph" || asset?.type === "Animation Graph" || asset?.type === "AnimationGraph";
	const isMesh = asset?.type === "Mesh";

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
					{isMesh ? (
						<div className="mesh-editor"><div className="mesh-preview"><span>⬡</span><strong>Mesh Preview</strong><small>The renderer bridge does not expose an isolated preview target yet.</small></div><aside><section><h3>Preview</h3><p>Open the mesh on an entity to inspect it in the scene viewport.</p></section><section><h3>Materials</h3><p>Material slots are stored in the binary mesh asset.</p></section><section><h3>LOD Settings</h3><label><input type="checkbox" disabled /> Generate LODs</label><label><input type="checkbox" disabled /> Optimize vertex cache</label><label><input type="checkbox" disabled /> Optimize overdraw</label></section><section><h3>Humanoid Rig</h3><p>Rig mappings require the native importer data.</p></section><div className="import-message">{asset.message}</div></aside></div>
					) : isGraph ? (
						<GraphAssetEditor asset={asset} content={content} onChange={setContent} />
					) : asset.readOnly ? (
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
