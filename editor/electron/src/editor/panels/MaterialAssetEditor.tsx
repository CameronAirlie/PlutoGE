import type React from "react";
import { useEffect, useId, useMemo, useState } from "react";
import { NumericInput } from "../components/NumericInput";

type MaterialVector = [number, number, number, number];

interface MaterialValues {
	color: MaterialVector;
	surfaceType: "Standard" | "Glass";
	alphaMode: "Opaque" | "Mask" | "Blend";
	alphaCutoff: number;
	castsShadow: boolean;
	uvScale: MaterialVector;
	metallic: number;
	roughness: number;
	emission: MaterialVector;
	transmission: number;
	ior: number;
	thickness: number;
	attenuationColor: MaterialVector;
	attenuationDistance: number;
	flipNormalY: boolean;
	albedoTexture: string;
	normalTexture: string;
	metallicTexture: string;
	metallicTextureChannel: number;
	roughnessTexture: string;
	roughnessTextureChannel: number;
	shaderGraph: string;
}

const defaultShaderGraph = "engine://builtin/shadergraph/default-lit";
const defaultUnlitShaderGraph = "engine://builtin/shadergraph/default-unlit";

const materialKeys = [
	"Color",
	"SurfaceType",
	"AlphaMode",
	"AlphaCutoff",
	"CastsShadow",
	"UvScale",
	"Metallic",
	"Roughness",
	"Emission",
	"Transmission",
	"Ior",
	"Thickness",
	"AttenuationColor",
	"AttenuationDistance",
	"FlipNormalY",
	"AlbedoTexture",
	"NormalTexture",
	"MetallicTexture",
	"MetallicTextureChannel",
	"RoughnessTexture",
	"RoughnessTextureChannel",
	"ShaderGraph",
] as const;

function materialFields(content: string): Map<string, string> {
	const fields = new Map<string, string>();
	for (const line of content.split(/\r?\n/)) {
		const delimiter = line.indexOf("=");
		if (delimiter < 0) continue;
		fields.set(line.slice(0, delimiter), line.slice(delimiter + 1));
	}
	return fields;
}

function numberValue(value: string | undefined, fallback: number): number {
	if (value === undefined || value.trim() === "") return fallback;
	const parsed = Number(value);
	return Number.isFinite(parsed) ? parsed : fallback;
}

function vectorValue(
	value: string | undefined,
	fallback: MaterialVector,
	length: number,
): MaterialVector {
	const result = [...fallback] as MaterialVector;
	const fields = value?.split(",") ?? [];
	for (let index = 0; index < length; index++)
		result[index] = numberValue(fields[index], fallback[index]);
	return result;
}

function parseMaterial(content: string): MaterialValues {
	const fields = materialFields(content);
	const surface = fields.get("SurfaceType")?.toLocaleLowerCase();
	const alpha = fields.get("AlphaMode")?.toLocaleLowerCase();
	return {
		color: vectorValue(fields.get("Color"), [1, 1, 1, 1], 4),
		surfaceType: surface === "glass" || surface === "1" ? "Glass" : "Standard",
		alphaMode:
			alpha === "blend" || alpha === "2"
				? "Blend"
				: alpha === "mask" || alpha === "1"
					? "Mask"
					: "Opaque",
		alphaCutoff: numberValue(fields.get("AlphaCutoff"), 0.5),
		castsShadow: ["true", "1"].includes(
			fields.get("CastsShadow")?.toLocaleLowerCase() ?? "true",
		),
		uvScale: vectorValue(fields.get("UvScale"), [1, 1, 0, 0], 2),
		metallic: numberValue(fields.get("Metallic"), 0),
		roughness: numberValue(fields.get("Roughness"), 1),
		emission: vectorValue(fields.get("Emission"), [0, 0, 0, 0], 3),
		transmission: numberValue(fields.get("Transmission"), 0),
		ior: numberValue(fields.get("Ior"), 1.45),
		thickness: numberValue(fields.get("Thickness"), 0.01),
		attenuationColor: vectorValue(
			fields.get("AttenuationColor"),
			[1, 1, 1, 0],
			3,
		),
		attenuationDistance: numberValue(fields.get("AttenuationDistance"), 1),
		flipNormalY: ["true", "1"].includes(
			fields.get("FlipNormalY")?.toLocaleLowerCase() ?? "false",
		),
		albedoTexture: fields.get("AlbedoTexture") ?? "",
		normalTexture: fields.get("NormalTexture") ?? "",
		metallicTexture: fields.get("MetallicTexture") ?? "",
		metallicTextureChannel: Math.max(
			0,
			Math.min(3, Math.round(numberValue(fields.get("MetallicTextureChannel"), 0))),
		),
		roughnessTexture: fields.get("RoughnessTexture") ?? "",
		roughnessTextureChannel: Math.max(
			0,
			Math.min(3, Math.round(numberValue(fields.get("RoughnessTextureChannel"), 0))),
		),
		shaderGraph: fields.get("ShaderGraph") || defaultShaderGraph,
	};
}

function updateMaterial(
	content: string,
	updates: Record<string, string>,
): string {
	const trailingNewline = /\r?\n$/.test(content);
	const lines = content.split(/\r?\n/);
	if (trailingNewline) lines.pop();
	const found = new Set<string>();
	const changed = lines.map((line) => {
		const delimiter = line.indexOf("=");
		const key = delimiter < 0 ? "" : line.slice(0, delimiter);
		if (!(key in updates)) return line;
		found.add(key);
		return `${key}=${updates[key]}`;
	});
	for (const key of materialKeys) {
		if (key in updates && !found.has(key)) changed.push(`${key}=${updates[key]}`);
	}
	return `${changed.join("\n")}\n`;
}

function formatNumber(value: number): string {
	return String(Number(value.toFixed(6)));
}

function formatVector(value: MaterialVector, length: number): string {
	return value.slice(0, length).map(formatNumber).join(",");
}

function VectorInput({
	labels,
	value,
	disabled,
	min,
	max,
	step = 0.01,
	onChange,
}: {
	labels: string;
	value: MaterialVector;
	disabled: boolean;
	min?: number;
	max?: number;
	step?: number;
	onChange(value: MaterialVector): void;
}): React.JSX.Element {
	return (
		<div className="material-vector">
			{[...labels].map((label, index) => (
				<label key={label}>
					<i>{label}</i>
					<NumericInput
						disabled={disabled}
						value={value[index]}
						min={min}
						max={max}
						step={step}
						onCommit={(next) => {
							const changed = [...value] as MaterialVector;
							changed[index] = next;
							onChange(changed);
						}}
					/>
				</label>
			))}
		</div>
	);
}

function AssetReferenceInput({
	value,
	type,
	assets,
	disabled,
	onChange,
}: {
	value: string;
	type: "Texture" | "Shader Graph";
	assets: EditorAsset[];
	disabled: boolean;
	onChange(value: string): void;
}): React.JSX.Element {
	const id = useId();
	const options = assets.filter((asset) => asset.type === type);
	return (
		<div className="material-reference-input">
			<input
				disabled={disabled}
				list={id}
				value={value}
				onChange={(event) => onChange(event.currentTarget.value)}
				placeholder={type === "Texture" ? "None" : defaultShaderGraph}
			/>
			<datalist id={id}>
				{type === "Shader Graph" && (
					<>
						<option value={defaultShaderGraph}>Default Lit</option>
						<option value={defaultUnlitShaderGraph}>Default Unlit</option>
					</>
				)}
				{options.map((asset) => (
					<option key={asset.reference} value={asset.reference} />
				))}
			</datalist>
			<button
				type="button"
				disabled={disabled || !value}
				onClick={() => onChange("")}
			>
				Clear
			</button>
		</div>
	);
}

function ScalarInput({
	value,
	disabled,
	min,
	max,
	step,
	onChange,
}: {
	value: number;
	disabled: boolean;
	min: number;
	max: number;
	step?: number;
	onChange(value: number): void;
}): React.JSX.Element {
	return (
		<div className="material-scalar">
			<input
				disabled={disabled}
				type="range"
				min={min}
				max={max}
				step={step ?? 0.01}
				value={value}
				onChange={(event) => onChange(Number(event.currentTarget.value))}
			/>
			<NumericInput
				disabled={disabled}
				value={value}
				min={min}
				max={max}
				step={step ?? 0.01}
				onCommit={onChange}
			/>
		</div>
	);
}

function colorHex(value: MaterialVector): string {
	return `#${value
		.slice(0, 3)
		.map((component) =>
			Math.round(Math.max(0, Math.min(1, component)) * 255)
				.toString(16)
				.padStart(2, "0"),
		)
		.join("")}`;
}

function hexColor(value: string, previous: MaterialVector): MaterialVector {
	return [
		Number.parseInt(value.slice(1, 3), 16) / 255,
		Number.parseInt(value.slice(3, 5), 16) / 255,
		Number.parseInt(value.slice(5, 7), 16) / 255,
		previous[3],
	];
}

export function MaterialAssetEditor({
	content,
	readOnly,
	onChange,
}: {
	content: string;
	readOnly: boolean;
	onChange(content: string): void;
}): React.JSX.Element {
	const material = useMemo(() => parseMaterial(content), [content]);
	const [assets, setAssets] = useState<EditorAsset[]>([]);
	useEffect(() => {
		const apply = (state: EditorState | undefined): void =>
			setAssets(state?.assets ?? []);
		void window.plutoEditor.getEditorState().then(apply);
		return window.plutoEditor.onEditorState(apply);
	}, []);
	const set = (key: string, value: string): void =>
		onChange(updateMaterial(content, { [key]: value }));
	const setVector = (key: string, value: MaterialVector, length: number): void =>
		set(key, formatVector(value, length));

	return (
		<div className="material-editor">
			<div className="material-preview-pane">
				<div className="material-preview-card">
					<div
						className="material-preview-swatch"
						style={{
							backgroundColor: `rgba(${material.color
								.slice(0, 3)
								.map((value) => Math.round(Math.max(0, Math.min(1, value)) * 255))
								.join(",")},${Math.max(0, Math.min(1, material.color[3]))})`,
						}}
					/>
					<strong>{material.surfaceType} Material</strong>
					<small>
						{material.alphaMode} · Metallic {material.metallic.toFixed(2)} ·
						 Roughness {material.roughness.toFixed(2)}
					</small>
					<small>
						{[
							material.albedoTexture && "Albedo",
							material.normalTexture && "Normal",
							material.metallicTexture && "Metallic",
							material.roughnessTexture && "Roughness",
						]
							.filter(Boolean)
							.join(", ") || "No textures"}
					</small>
				</div>
			</div>

			<div className="material-properties">
				<section>
					<h3>Shader</h3>
					<label className="material-property">
						<span>Shader Graph</span>
						<AssetReferenceInput
							value={material.shaderGraph}
							type="Shader Graph"
							assets={assets}
							disabled={readOnly}
							onChange={(value) => set("ShaderGraph", value || defaultShaderGraph)}
						/>
					</label>
				</section>

				<section>
					<h3>Surface</h3>
					<div className="material-property">
						<span>Base Color</span>
						<div className="material-color">
							<input
								disabled={readOnly}
								type="color"
								value={colorHex(material.color)}
								onChange={(event) =>
									setVector("Color", hexColor(event.currentTarget.value, material.color), 4)
								}
							/>
							<VectorInput
								labels="RGBA"
								value={material.color}
								disabled={readOnly}
								min={0}
								max={1}
								onChange={(value) => setVector("Color", value, 4)}
							/>
						</div>
					</div>
					<label className="material-property">
						<span>Surface Type</span>
						<select
							disabled={readOnly}
							value={material.surfaceType}
							onChange={(event) => {
								const surfaceType = event.currentTarget.value;
								if (surfaceType === "Glass") {
									onChange(
										updateMaterial(content, {
											SurfaceType: "Glass",
											AlphaMode: "Blend",
											CastsShadow: "false",
											Metallic: "0",
											Transmission: "1",
											Roughness: formatNumber(Math.min(material.roughness, 0.15)),
										}),
									);
								} else set("SurfaceType", "Standard");
							}}
						>
							<option>Standard</option>
							<option>Glass</option>
						</select>
					</label>
					<label className="material-property">
						<span>Alpha Mode</span>
						<select
							disabled={readOnly}
							value={material.alphaMode}
							onChange={(event) => set("AlphaMode", event.currentTarget.value)}
						>
							<option>Opaque</option>
							<option>Mask</option>
							<option>Blend</option>
						</select>
					</label>
					{material.alphaMode === "Mask" && (
						<div className="material-property">
							<span>Alpha Cutoff</span>
							<ScalarInput
								value={material.alphaCutoff}
								disabled={readOnly}
								min={0}
								max={1}
								onChange={(value) => set("AlphaCutoff", formatNumber(value))}
							/>
						</div>
					)}
					<label className="material-property material-checkbox">
						<span>Casts Shadow</span>
						<input
							disabled={readOnly}
							type="checkbox"
							checked={material.castsShadow}
							onChange={(event) => set("CastsShadow", String(event.currentTarget.checked))}
						/>
					</label>
				</section>

				<section>
					<h3>Textures</h3>
					{([
						["Albedo", "AlbedoTexture", material.albedoTexture],
						["Normal", "NormalTexture", material.normalTexture],
						["Metallic", "MetallicTexture", material.metallicTexture],
						["Roughness", "RoughnessTexture", material.roughnessTexture],
					] as const).map(([label, key, value]) => (
						<label className="material-property" key={key}>
							<span>{label}</span>
							<AssetReferenceInput
								value={value}
								type="Texture"
								assets={assets}
								disabled={readOnly}
								onChange={(next) => set(key, next)}
							/>
						</label>
					))}
					<label className="material-property material-checkbox">
						<span>Flip Normal Y</span>
						<input
							disabled={readOnly}
							type="checkbox"
							checked={material.flipNormalY}
							onChange={(event) => set("FlipNormalY", String(event.currentTarget.checked))}
						/>
					</label>
				</section>

				<section>
					<h3>Shading</h3>
					{([
						["Metallic", "Metallic", material.metallic, 0, 1],
						["Roughness", "Roughness", material.roughness, 0.04, 1],
					] as const).map(([label, key, value, min, max]) => (
						<div className="material-property" key={key}>
							<span>{label}</span>
							<ScalarInput
								value={value}
								disabled={readOnly}
								min={min}
								max={max}
								onChange={(next) => set(key, formatNumber(next))}
							/>
						</div>
					))}
					{([
						["Metallic Channel", "MetallicTextureChannel", material.metallicTextureChannel],
						["Roughness Channel", "RoughnessTextureChannel", material.roughnessTextureChannel],
					] as const).map(([label, key, value]) => (
						<label className="material-property" key={key}>
							<span>{label}</span>
							<select
								disabled={readOnly}
								value={value}
								onChange={(event) => set(key, event.currentTarget.value)}
							>
								<option value={0}>Red</option>
								<option value={1}>Green</option>
								<option value={2}>Blue</option>
								<option value={3}>Alpha</option>
							</select>
						</label>
					))}
					<div className="material-property">
						<span>Emission</span>
						<VectorInput
							labels="RGB"
							value={material.emission}
							disabled={readOnly}
							min={0}
							onChange={(value) => setVector("Emission", value, 3)}
						/>
					</div>
					<div className="material-property">
						<span>UV Scale</span>
						<VectorInput
							labels="XY"
							value={material.uvScale}
							disabled={readOnly}
							min={0.0001}
							step={0.05}
							onChange={(value) => setVector("UvScale", value, 2)}
						/>
					</div>
				</section>

				{material.surfaceType === "Glass" && (
					<section>
						<h3>Glass</h3>
						{([
							["Transmission", "Transmission", material.transmission, 0, 1, 0.01],
							["Index of Refraction", "Ior", material.ior, 1, 2.5, 0.01],
						] as const).map(([label, key, value, min, max, step]) => (
							<div className="material-property" key={key}>
								<span>{label}</span>
								<ScalarInput
									value={value}
									disabled={readOnly}
									min={min}
									max={max}
									step={step}
									onChange={(next) => set(key, formatNumber(next))}
								/>
							</div>
						))}
						<div className="material-property">
							<span>Thickness</span>
							<NumericInput
								disabled={readOnly}
								value={material.thickness}
								min={0}
								step={0.001}
								onCommit={(value) => set("Thickness", formatNumber(value))}
							/>
						</div>
						<div className="material-property">
							<span>Attenuation Color</span>
							<VectorInput
								labels="RGB"
								value={material.attenuationColor}
								disabled={readOnly}
								min={0}
								max={1}
								onChange={(value) => setVector("AttenuationColor", value, 3)}
							/>
						</div>
						<div className="material-property">
							<span>Attenuation Distance</span>
							<NumericInput
								disabled={readOnly}
								value={material.attenuationDistance}
								min={0.0001}
								step={0.01}
								onCommit={(value) => set("AttenuationDistance", formatNumber(value))}
							/>
						</div>
					</section>
				)}

				<details className="material-source">
					<summary>Advanced source</summary>
					<textarea
						disabled={readOnly}
						spellCheck={false}
						value={content}
						onChange={(event) => onChange(event.currentTarget.value)}
					/>
				</details>
			</div>
		</div>
	);
}
