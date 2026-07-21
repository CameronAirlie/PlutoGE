import type React from "react";
import { NumericInput } from "../components/NumericInput";

function formatCount(value: number): string {
	return new Intl.NumberFormat().format(value);
}

function MeshRigMappingRow({
	mapping,
	joints,
}: {
	mapping: MeshRigMapping;
	joints: string[];
}): React.JSX.Element {
	const update = (changes: Partial<MeshRigMapping>): void => {
		void window.plutoEditor.setMeshRigMapping({ ...mapping, ...changes });
	};
	return (
		<div className={`mesh-rig-row${mapping.duplicateTarget ? " warning" : ""}`}>
		<strong title={mapping.required ? "Required bone" : undefined}>
			{mapping.boneName}{mapping.required ? " *" : ""}
		</strong>
		<select
			value={mapping.targetJointIndex}
			onChange={(event) => update({ targetJointIndex: Number(event.currentTarget.value) })}
		>
			<option value={-1}>Unassigned</option>
			{joints.map((joint, index) => (
				<option key={`${joint}-${index}`} value={index}>{joint}</option>
			))}
		</select>
		<input
			defaultValue={mapping.sourceBoneName}
			key={mapping.sourceBoneName}
			placeholder="Auto aliases"
			onBlur={(event) => {
				if (event.currentTarget.value !== mapping.sourceBoneName)
					update({ sourceBoneName: event.currentTarget.value });
			}}
		/>
		<div className="mesh-rig-offset" title="Rotation offset in degrees">
			{mapping.rotationOffsetDegrees.map((value, index) => (
				<NumericInput
					key={index}
					value={value}
					min={-180}
					max={180}
					step={0.25}
					onCommit={(next) => {
						const rotation = [...mapping.rotationOffsetDegrees] as [number, number, number];
						rotation[index] = next;
						update({ rotationOffsetDegrees: rotation });
					}}
				/>
			))}
		</div>
		<label className="mesh-inline-check">
			<input
				type="checkbox"
				checked={mapping.copyTranslation}
				onChange={(event) => update({ copyTranslation: event.currentTarget.checked })}
			/>
			Copy translation
		</label>
		<NumericInput
			value={mapping.translationScale}
			disabled={!mapping.copyTranslation}
			min={0}
			max={10}
			step={0.01}
			onCommit={(translationScale) => update({ translationScale })}
		/>
	</div>
	);
}

export function MeshAssetEditor({ state }: { state?: MeshAssetEditorState }): React.JSX.Element {
	if (!state) {
		return <div className="empty-state">Loading mesh settings from the native asset pipeline…</div>;
	}

	const setImportOption = (key: keyof MeshImportOptions, value: boolean): void => {
		void window.plutoEditor.setMeshImportOptions({ ...state.importOptions, [key]: value });
	};
	const lod0Triangles = state.lods[0]?.triangles ?? state.triangleCount;
	const mappedRequired = state.skeleton.mappings.filter(
		(mapping) => mapping.required && mapping.targetJointIndex >= 0,
	).length;
	const requiredCount = state.skeleton.mappings.filter((mapping) => mapping.required).length;
	const hasDuplicates = state.skeleton.mappings.some((mapping) => mapping.duplicateTarget);

	return (
		<div className="mesh-editor">
			<div className="mesh-preview">
				<span className="mesh-preview-icon">⬡</span>
				<strong>Mesh Preview</strong>
				<small>Open the mesh on an entity to inspect it in the scene viewport.</small>
				<div className="mesh-stat-grid">
					<span>Vertices<strong>{formatCount(state.vertexCount)}</strong></span>
					<span>Triangles<strong>{formatCount(state.triangleCount)}</strong></span>
					<span>Submeshes<strong>{formatCount(state.submeshCount)}</strong></span>
					<span>LOD levels<strong>{state.lodCount}</strong></span>
				</div>
				<small>Bounds center: {state.bounds.center.map((value) => value.toFixed(2)).join(", ")} · radius: {state.bounds.radius.toFixed(2)}</small>
			</div>
			<aside>
				<section>
					<h3>Materials</h3>
					{state.materials.length === 0 ? (
						<p>No material slots saved on this mesh asset.</p>
					) : state.materials.map((reference, index) => (
						<div className="mesh-material-slot" key={`${reference}-${index}`}>
							<span>Slot {index}</span>
							<code>{reference || "(empty)"}</code>
							{reference.startsWith("project://") && (
								<button type="button" onClick={() => void window.plutoEditor.openAsset(reference)}>Open</button>
							)}
						</div>
					))}
				</section>

				<section>
					<h3>LOD & Import Settings</h3>
					<label><input type="checkbox" checked={state.importOptions.generateLods} onChange={(event) => setImportOption("generateLods", event.currentTarget.checked)} /> Generate LODs</label>
					<label><input type="checkbox" checked={state.importOptions.optimizeVertexCache} onChange={(event) => setImportOption("optimizeVertexCache", event.currentTarget.checked)} /> Optimize vertex cache</label>
					<label><input type="checkbox" checked={state.importOptions.optimizeOverdraw} onChange={(event) => setImportOption("optimizeOverdraw", event.currentTarget.checked)} /> Optimize overdraw</label>
					<button type="button" disabled={!state.canReimport} onClick={() => void window.plutoEditor.reimportMesh()}>Reimport with settings</button>
					{!state.canReimport && <p>Reimport needs a matching source model asset.</p>}
					{state.submeshCount > 500 && <div className="mesh-warning">Large draw count: {formatCount(state.submeshCount)} submeshes (recommended maximum: 500).</div>}
					{lod0Triangles > 1_000_000 && <div className="mesh-warning">Large LOD 0: {formatCount(lod0Triangles)} triangles.</div>}
					{state.lodCount <= 1 && lod0Triangles > 100_000 && <div className="mesh-warning">This large mesh has no simplified LODs.</div>}
					<div className="mesh-lod-list">
						{state.lods.map((lod) => (
							<div key={lod.index}>
								<span>LOD {lod.index}</span>
								<strong>{formatCount(lod.triangles)} triangles</strong>
								{lod.index === 0 ? <small>Full detail</small> : (
									<label><NumericInput value={lod.maxScreenRadiusPixels} min={1} max={2000} step={1} onCommit={(value) => void window.plutoEditor.setMeshLodThreshold(lod.index, value)} /> px</label>
								)}
							</div>
						))}
					</div>
				</section>

				<section>
					<h3>Humanoid Rig</h3>
					{state.skeleton.joints.length === 0 ? (
						<p>This mesh has no skeleton.</p>
					) : !state.skeleton.enabled ? (
						<>
							<p>Configure a humanoid map to retarget clips whose bone names or joint order differ from this mesh.</p>
							<button type="button" onClick={() => void window.plutoEditor.autoMapMeshRig()}>Configure and auto map</button>
						</>
					) : (
						<>
							<div className="mesh-rig-actions">
								<button type="button" onClick={() => void window.plutoEditor.autoMapMeshRig()}>Auto map missing</button>
								<button type="button" onClick={() => void window.plutoEditor.disableMeshRetargeting()}>Disable retargeting</button>
							</div>
							<div className={mappedRequired === requiredCount && !hasDuplicates ? "mesh-rig-ready" : "mesh-warning"}>
								{mappedRequired === requiredCount && !hasDuplicates ? "Rig ready" : "Needs attention"} ({mappedRequired}/{requiredCount} required bones){hasDuplicates ? " — duplicate target joints" : ""}
							</div>
							<p>Source override is optional. Rotation offset values are in degrees.</p>
							<div className="mesh-rig-list">
								{state.skeleton.mappings.map((mapping) => <MeshRigMappingRow key={mapping.boneIndex} mapping={mapping} joints={state.skeleton.joints} />)}
							</div>
						</>
					)}
				</section>
			</aside>
		</div>
	);
}
