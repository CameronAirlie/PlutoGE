import type React from "react";
import { useEffect, useState } from "react";
import { PanelFrame } from "../components/PanelFrame";
import { GraphAssetEditor } from "./GraphAssetEditor";
import { MaterialAssetEditor } from "./MaterialAssetEditor";
import { MeshAssetEditor } from "./MeshAssetEditor";

export function AssetEditorPanel(): React.JSX.Element {
	const [asset, setAsset] = useState<AssetDocument>();
	const [content, setContent] = useState("");
	const [savedContent, setSavedContent] = useState("");
	const [message, setMessage] = useState("");
	const [meshState, setMeshState] = useState<MeshAssetEditorState>();

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
		const apply = (state: EditorState | undefined): void => setMeshState(state?.meshAsset ?? undefined);
		void window.plutoEditor.getEditorState().then(apply);
		return window.plutoEditor.onEditorState(apply);
	}, []);
	useEffect(() => {
		window.plutoEditor.setAssetDirty(
			Boolean(asset && (asset.type === "Mesh" ? meshState?.reference === asset.reference && meshState.dirty : content !== savedContent)),
		);
	}, [asset, content, savedContent, meshState]);

	const save = async (): Promise<void> => {
		if (!asset) return;
		const saved = await window.plutoEditor.saveAsset(asset.reference, content);
		setMessage(saved ? "Saved." : "Could not save this asset.");
		if (saved) setSavedContent(content);
	};
	const isGraph =
		asset?.type === "Shader Graph" ||
		asset?.type === "ShaderGraph" ||
		asset?.type === "Animation Graph" ||
		asset?.type === "AnimationGraph";
	const isMesh = asset?.type === "Mesh";
	const isMaterial = asset?.type === "Material";
	const activeMeshState = isMesh && meshState?.reference === asset.reference ? meshState : undefined;
	const dirty = isMesh ? Boolean(activeMeshState?.dirty) : content !== savedContent;

	return (
		<PanelFrame
			id="asset"
			title={asset ? `${asset.type} Editor` : "Asset Editor"}
			actions={
				asset ? (
					<>
						<button
							type="button"
							disabled={(asset.readOnly && !isMesh) || !dirty}
							onClick={() => isMesh ? void window.plutoEditor.saveMesh() : void save()}
						>
							Save
						</button>
						<button
							type="button"
							disabled={!dirty}
							onClick={() => isMesh ? void window.plutoEditor.revertMesh() : setContent(savedContent)}
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
						{dirty && <span>Modified</span>}
					</div>
					{message && <div className="import-message">{message}</div>}
					{isMaterial ? (
						<MaterialAssetEditor
							content={content}
							readOnly={asset.readOnly}
							onChange={setContent}
						/>
					) : isMesh ? (
						<MeshAssetEditor state={activeMeshState} />
					) : isGraph ? (
						<GraphAssetEditor
							asset={asset}
							content={content}
							onChange={setContent}
						/>
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
