import type React from "react";
import { useMemo, useState } from "react";
import { type GraphLink, type GraphNode, type GraphPaletteItem, NodeGraph } from "../components/NodeGraph";
import { Button, ScrollBox } from "../components/ui";

interface Props { asset: AssetDocument; content: string; onChange(value: string): void; }
interface RecordLine { key: string; fields: string[]; }
const split = (value: string): string[] => value.split("|");
const bool = (value?: string): boolean => value === "1" || value?.toLowerCase() === "true";
const vec2 = (value = "0,0"): [number, number] => { const p = value.split(",").map(Number); return [p[0] || 0, p[1] || 0]; };
const clean = (value: string): string => value.replace(/[|\r\n]/g, " ");
const shaderPins: Record<string, { inputs: string[]; outputs: string[] }> = {
	MaterialInput: { inputs: [], outputs: ["Out"] }, Float: { inputs: [], outputs: ["Out"] }, Vec2: { inputs: [], outputs: ["Out"] }, Vec3: { inputs: [], outputs: ["Out"] }, Color: { inputs: [], outputs: ["Out"] }, MeshUV: { inputs: [], outputs: ["Out"] },
	Add: { inputs: ["A", "B"], outputs: ["Out"] }, Subtract: { inputs: ["A", "B"], outputs: ["Out"] }, Multiply: { inputs: ["A", "B"], outputs: ["Out"] }, Divide: { inputs: ["A", "B"], outputs: ["Out"] }, Lerp: { inputs: ["A", "B", "T"], outputs: ["Out"] }, Clamp: { inputs: ["In", "Min", "Max"], outputs: ["Out"] }, Normalize: { inputs: ["In"], outputs: ["Out"] }, NoiseTexture: { inputs: ["UV", "Scale", "Strength"], outputs: ["Out"] },
	Output: { inputs: ["Albedo", "Normal", "Metallic", "Roughness", "Opacity", "Emission"], outputs: [] },
};
const shaderNodeKinds = [["Float", "Float"], ["Vec2", "Vector 2"], ["Vec3", "Vector 3"], ["Color", "Color"], ["Add", "Add"], ["Subtract", "Subtract"], ["Multiply", "Multiply"], ["Divide", "Divide"], ["Lerp", "Lerp"], ["Clamp", "Clamp"], ["Normalize", "Normalize"], ["NoiseTexture", "Noise Texture"], ["MeshUV", "Mesh UV"], ["MaterialInput", "Material Input"], ["Output", "Geometry Output"]] as const;
const materialInputs = ["Color", "Normal", "Metallic", "Roughness", "Opacity", "UV", "Emission"];
const parameterTypes = ["Float", "Int", "Bool", "Trigger"];
const conditionModes = ["If", "IfNot", "Greater", "Less", "Equals", "NotEqual"];

function Setting({ label, children }: { label: string; children: React.ReactNode }): React.JSX.Element {
	return <label className="graph-setting"><span>{label}</span>{children}</label>;
}

export function GraphAssetEditor({ asset, content, onChange }: Props): React.JSX.Element {
	const shader = asset.type === "Shader Graph" || asset.type === "ShaderGraph";
	const [selected, setSelected] = useState<number>();
	const [selectedLink, setSelectedLink] = useState<number>();
	const parsed = useMemo(() => {
		const nodes: GraphNode[] = []; const links: GraphLink[] = []; const records: RecordLine[] = [];
		for (const line of content.split(/\r?\n/)) {
			const at = line.indexOf("="); if (at < 0) continue;
			const key = line.slice(0, at); const fields = split(line.slice(at + 1)); records.push({ key, fields });
			if (shader && key === "Node" && fields.length >= 4) { const [x, y] = vec2(fields[3]); const pins = shaderPins[fields[1]] ?? { inputs: ["In"], outputs: ["Out"] }; nodes.push({ id: Number(fields[0]), title: fields[2] || fields[1], subtitle: fields[1], x, y, ...pins, accent: fields[1] === "Output" ? "#8c4d55" : "#405b83" }); }
			if (shader && key === "Link") links.push({ id: Number(fields[0]), from: Number(fields[1]), fromPin: fields[2], to: Number(fields[3]), toPin: fields[4] });
			if (!shader && key === "State" && fields.length >= 8) nodes.push({ id: Number(fields[0]), title: fields[1], subtitle: bool(fields[7]) ? "Loop" : "Once", x: Number(fields[4]) || 0, y: Number(fields[5]) || 0, inputs: ["In"], outputs: ["Out"], accent: "#496b57" });
			if (!shader && key === "Transition") links.push({ id: Number(fields[0]), from: Number(fields[1]), to: Number(fields[2]), fromPin: "Out", toPin: "In" });
		}
		if (!shader) { const defaultId = Number(records.find((item) => item.key === "DefaultStateId")?.fields[0]); for (const node of nodes) if (node.id === defaultId) { node.subtitle = `${node.subtitle} · Default`; node.accent = "#557b5e"; } }
		return { nodes, links, records };
	}, [content, shader]);
	const record = (key: string, id: number): string[] | undefined => parsed.records.find((item) => item.key === key && Number(item.fields[0]) === id)?.fields;
	const replaceLine = (predicate: (key: string, fields: string[]) => boolean, mutate: (fields: string[]) => void): void => onChange(content.split(/\r?\n/).map((line) => { const at = line.indexOf("="); if (at < 0) return line; const key = line.slice(0, at); const fields = split(line.slice(at + 1)); if (!predicate(key, fields)) return line; mutate(fields); return `${key}=${fields.join("|")}`; }).join("\n"));
	const update = (key: string, id: number, index: number, value: string): void => replaceLine((candidate, fields) => candidate === key && Number(fields[0]) === id, (fields) => { while (fields.length <= index) fields.push(""); fields[index] = value; });
	const append = (line: string): void => onChange(`${content.trimEnd()}\n${line}\n`);
	const removeLines = (predicate: (key: string, fields: string[]) => boolean): void => onChange(content.split(/\r?\n/).filter((line) => { const at = line.indexOf("="); return at < 0 || !predicate(line.slice(0, at), split(line.slice(at + 1))); }).join("\n"));
	const move = (id: number, x: number, y: number): void => { if (shader) update("Node", id, 3, `${x.toFixed(1)},${y.toFixed(1)}`); else { replaceLine((key, f) => key === "State" && Number(f[0]) === id, (f) => { f[4] = x.toFixed(1); f[5] = y.toFixed(1); }); } };
	const palette: GraphPaletteItem[] = shader ? shaderNodeKinds.map(([kind, label]) => ({ kind, label, category: ["Float", "Vec2", "Vec3", "Color"].includes(kind) ? "Values" : ["Add", "Subtract", "Multiply", "Divide", "Lerp", "Clamp", "Normalize"].includes(kind) ? "Math" : kind === "Output" ? "Output" : "Input", connectable: (shaderPins[kind]?.inputs.length ?? 0) > 0 })) : [{ kind: "State", label: "Animation State", category: "Animation", connectable: true }];
	const addNode = (kind = shader ? "Float" : "State", x = 80, y = 80, connectFrom?: { nodeId: number; pin: string }): void => {
		const id = Math.max(0, ...parsed.nodes.map((node) => node.id)) + 1;
		const title = shaderNodeKinds.find(([candidate]) => candidate === kind)?.[1] ?? `State ${id}`;
		const nodeLine = shader ? `Node=${id}|${kind}|${title}|${x.toFixed(1)},${y.toFixed(1)}|1,1,1,1|Color|0|0,0|0` : `State=${id}|State ${id}|Idle|0|${x.toFixed(1)}|${y.toFixed(1)}|1|1|`;
		let next = `${content.trimEnd()}\n${nodeLine}\n`;
		if (connectFrom) { const linkId = Math.max(0, ...parsed.links.map((link) => link.id)) + 1; const input = shader ? (shaderPins[kind]?.inputs[0] ?? "In") : "In"; next += shader ? `Link=${linkId}|${connectFrom.nodeId}|${connectFrom.pin}|${id}|${input}\n` : `Transition=${linkId}|${connectFrom.nodeId}|${id}|0.2|0|0.9\n`; }
		onChange(next); setSelected(id); setSelectedLink(undefined);
	};
	const addLink = (from: number, fromPin: string, to: number, toPin: string): void => {
		if (from === to || parsed.links.some((link) => link.from === from && link.to === to && (!shader || link.toPin === toPin))) return;
		const id = Math.max(0, ...parsed.links.map((link) => link.id)) + 1;
		if (shader) {
			const retained = content.split(/\r?\n/).filter((line) => { const at = line.indexOf("="); if (at < 0 || line.slice(0, at) !== "Link") return true; const f = split(line.slice(at + 1)); return !(Number(f[3]) === to && f[4] === toPin); }).join("\n");
			onChange(`${retained.trimEnd()}\nLink=${id}|${from}|${fromPin}|${to}|${toPin}\n`);
		} else append(`Transition=${id}|${from}|${to}|0.2|0|0.9`);
		setSelectedLink(id);
	};
	const removeNode = (id: number): void => { const nodeKey = shader ? "Node" : "State"; const linkKey = shader ? "Link" : "Transition"; removeLines((key, f) => (key === nodeKey && Number(f[0]) === id) || (key === linkKey && (Number(f[1]) === id || Number(f[shader ? 3 : 2]) === id)) || (!shader && (key === "BlendSpace" || key === "BlendSpacePoint") && Number(f[0]) === id)); setSelected(undefined); };
	const removeLink = (id: number): void => { removeLines((key, f) => (key === (shader ? "Link" : "Transition") || (!shader && key === "Condition")) && Number(f[0]) === id); setSelectedLink(undefined); };
	const selectedNode = selected === undefined ? undefined : record(shader ? "Node" : "State", selected);
	const transition = !shader && selectedLink !== undefined ? record("Transition", selectedLink) : undefined;
	const parameters = parsed.records.filter((item) => item.key === "Parameter");
	const variables = parsed.records.filter((item) => item.key === "Variable");
	const layers = parsed.records.filter((item) => item.key === "Layer");
	const boneMasks = parsed.records.filter((item) => item.key === "BoneMask");
	const conditions = selectedLink === undefined ? [] : parsed.records.filter((item) => item.key === "Condition" && Number(item.fields[0]) === selectedLink);
	const blendSpace = !shader && selected !== undefined ? record("BlendSpace", selected) : undefined;
	const blendPoints = !shader && selected !== undefined ? parsed.records.filter((item) => item.key === "BlendSpacePoint" && Number(item.fields[0]) === selected) : [];
	const nextParameterId = Math.max(0, ...parameters.map((item) => Number(item.fields[0]))) + 1;
	const nextLayerId = Math.max(0, ...layers.map((item) => Number(item.fields[0]))) + 1;
	const nextMaskId = Math.max(0, ...boneMasks.map((item) => Number(item.fields[0]))) + 1;
	return <div className="graph-editor">
		<aside className="graph-sidebar">
			<h3>{shader ? "Shader Graph" : "Animation Graph"}</h3>
			<div className="node-palette"><header><strong>Nodes</strong><small>Drag onto canvas</small></header><ScrollBox className="node-palette-scroll">{palette.map((item) => <Button variant="ghost" draggable key={item.kind} onDragStart={(event) => { event.dataTransfer.setData("application/x-plutoge-node", item.kind); event.dataTransfer.effectAllowed = "copy"; }} onDoubleClick={() => addNode(item.kind)}><span>{item.label}</span><small>{item.category}</small></Button>)}</ScrollBox></div>
			<div className="graph-list">{parsed.nodes.map((node) => <button type="button" className={selected === node.id ? "active" : ""} key={node.id} onClick={() => { setSelected(node.id); setSelectedLink(undefined); }}>{node.title}<small>{node.subtitle}</small></button>)}</div>
			{selectedNode && <section className="graph-inspector"><h4>{shader ? "Node Settings" : "State Settings"}</h4>
				<Setting label="Name"><input value={selectedNode[shader ? 2 : 1]} onChange={(e) => update(shader ? "Node" : "State", selected as number, shader ? 2 : 1, clean(e.currentTarget.value))} /></Setting>
				{shader ? <>
					<Setting label="Type"><input disabled value={selectedNode[1]} /></Setting>
					{["Float", "Vec2", "Vec3", "Color", "NoiseTexture"].includes(selectedNode[1]) && <div className="graph-vector"><span>Value</span>{(selectedNode[4] || "0,0,0,0").split(",").map((value, index, values) => <input key={index} type="number" step="0.01" value={value} onChange={(e) => { const next = [...values]; next[index] = e.currentTarget.value; update("Node", selected as number, 4, next.join(",")); }} />)}</div>}
					{selectedNode[1] === "MaterialInput" && <Setting label="Input"><select value={selectedNode[5]} onChange={(e) => update("Node", selected as number, 5, e.currentTarget.value)}>{materialInputs.map((input) => <option key={input}>{input}</option>)}</select></Setting>}
					<Setting label="Component Pins"><input type="checkbox" checked={bool(selectedNode[8])} onChange={(e) => update("Node", selected as number, 8, e.currentTarget.checked ? "1" : "0")} /></Setting>
				</> : <>
					<Setting label="Clip asset"><input value={selectedNode[8] ?? ""} placeholder="project://..." onChange={(e) => update("State", selected as number, 8, clean(e.currentTarget.value))} /></Setting>
					<Setting label="Clip name"><input value={selectedNode[2]} onChange={(e) => update("State", selected as number, 2, clean(e.currentTarget.value))} /></Setting>
					<Setting label="Clip index"><input type="number" min="0" value={selectedNode[3]} onChange={(e) => update("State", selected as number, 3, e.currentTarget.value)} /></Setting>
					<Setting label="Speed"><input type="number" min="0" step="0.05" value={selectedNode[6]} onChange={(e) => update("State", selected as number, 6, e.currentTarget.value)} /></Setting>
					<Setting label="Loop"><input type="checkbox" checked={bool(selectedNode[7])} onChange={(e) => update("State", selected as number, 7, e.currentTarget.checked ? "1" : "0")} /></Setting>
					<button type="button" onClick={() => replaceLine((key) => key === "DefaultStateId", (f) => { f[0] = String(selected); })}>Make default state</button>
					<h5>Blend Space</h5>{blendSpace ? <><Setting label="Parameter X"><select value={blendSpace[1]} onChange={(e) => update("BlendSpace", selected as number, 1, e.currentTarget.value)}><option value="">None</option>{parameters.filter((p) => p.fields[2] === "Float").map((p) => <option key={p.fields[0]}>{p.fields[1]}</option>)}</select></Setting><Setting label="Parameter Y"><select value={blendSpace[2]} onChange={(e) => update("BlendSpace", selected as number, 2, e.currentTarget.value)}><option value="">1D</option>{parameters.filter((p) => p.fields[2] === "Float").map((p) => <option key={p.fields[0]}>{p.fields[1]}</option>)}</select></Setting>{blendPoints.map((point, index) => <div className="blend-point" key={`${point.fields.join("|")}-${index}`}><input type="number" step="0.1" title="X" value={point.fields[1]} onChange={(e) => { let occurrence = -1; replaceLine((key, f) => key === "BlendSpacePoint" && Number(f[0]) === selected && ++occurrence === index, (f) => { f[1] = e.currentTarget.value; }); }} /><input type="number" step="0.1" title="Y" value={point.fields[2]} onChange={(e) => { let occurrence = -1; replaceLine((key, f) => key === "BlendSpacePoint" && Number(f[0]) === selected && ++occurrence === index, (f) => { f[2] = e.currentTarget.value; }); }} /><input title="Clip" value={point.fields[3]} onChange={(e) => { let occurrence = -1; replaceLine((key, f) => key === "BlendSpacePoint" && Number(f[0]) === selected && ++occurrence === index, (f) => { f[3] = clean(e.currentTarget.value); }); }} /></div>)}<button type="button" onClick={() => append(`BlendSpacePoint=${selected}|0|0|Idle|0|0`)}>+ Blend point</button><button type="button" onClick={() => removeLines((key, f) => (key === "BlendSpace" || key === "BlendSpacePoint") && Number(f[0]) === selected)}>Disable blend space</button></> : <button type="button" onClick={() => append(`BlendSpace=${selected}|${parameters.find((p) => p.fields[2] === "Float")?.fields[1] ?? ""}|`)}>Enable blend space</button>}
				</>}
				<button className="danger" type="button" onClick={() => removeNode(selected as number)}>Delete node</button>
			</section>}
			{transition && <section className="graph-inspector"><h4>Transition Settings</h4>
				<Setting label="Duration"><input type="number" min="0" step="0.01" value={transition[3]} onChange={(e) => update("Transition", selectedLink as number, 3, e.currentTarget.value)} /></Setting>
				<Setting label="Exit time"><input type="checkbox" checked={bool(transition[4])} onChange={(e) => update("Transition", selectedLink as number, 4, e.currentTarget.checked ? "1" : "0")} /></Setting>
				<Setting label="Normalized time"><input type="number" min="0" step="0.05" value={transition[5]} disabled={!bool(transition[4])} onChange={(e) => update("Transition", selectedLink as number, 5, e.currentTarget.value)} /></Setting>
				<h5>Conditions</h5>{conditions.map((condition, index) => <div className="condition-row" key={`${condition.fields.join("|")}-${index}`}><select value={condition.fields[1]} onChange={(e) => { let occurrence = -1; replaceLine((key, f) => key === "Condition" && Number(f[0]) === selectedLink && ++occurrence === index, (f) => { f[1] = e.currentTarget.value; }); }}>{parameters.map((parameter) => <option key={parameter.fields[0]}>{parameter.fields[1]}</option>)}</select><select value={condition.fields[2]} onChange={(e) => { let occurrence = -1; replaceLine((key, f) => key === "Condition" && Number(f[0]) === selectedLink && ++occurrence === index, (f) => { f[2] = e.currentTarget.value; }); }}>{conditionModes.map((mode) => <option key={mode}>{mode}</option>)}</select><input type="number" step="0.05" value={condition.fields[3]} onChange={(e) => { let occurrence = -1; replaceLine((key, f) => key === "Condition" && Number(f[0]) === selectedLink && ++occurrence === index, (f) => { f[3] = e.currentTarget.value; }); }} /></div>)}
				<button type="button" disabled={!parameters.length} onClick={() => append(`Condition=${selectedLink}|${parameters[0]?.fields[1] ?? ""}|If|0`)}>+ Condition</button><button className="danger" type="button" onClick={() => removeLink(selectedLink as number)}>Delete transition</button>
			</section>}
			{shader && selectedLink !== undefined && <section className="graph-inspector"><h4>Connection</h4><button className="danger" type="button" onClick={() => removeLink(selectedLink)}>Delete connection</button></section>}
			<section className="graph-inspector"><h4>{shader ? "Variables" : "Parameters"}</h4>
				{(shader ? variables : parameters).map((item) => shader ? <div className="graph-record" key={item.fields[0]}><input value={item.fields[0]} onChange={(e) => replaceLine((key, f) => key === "Variable" && f[0] === item.fields[0], (f) => { f[0] = clean(e.currentTarget.value); })} /><select value={item.fields[1]} onChange={(e) => replaceLine((key, f) => key === "Variable" && f[0] === item.fields[0], (f) => { f[1] = e.currentTarget.value; })}>{["0", "1", "2", "3"].map((type) => <option key={type}>{type}</option>)}</select></div> : <div className="graph-record" key={item.fields[0]}><input value={item.fields[1]} onChange={(e) => update("Parameter", Number(item.fields[0]), 1, clean(e.currentTarget.value))} /><select value={item.fields[2]} onChange={(e) => update("Parameter", Number(item.fields[0]), 2, e.currentTarget.value)}>{parameterTypes.map((type) => <option key={type}>{type}</option>)}</select><input type={item.fields[2] === "Bool" || item.fields[2] === "Trigger" ? "checkbox" : "number"} checked={item.fields[2] === "Bool" || item.fields[2] === "Trigger" ? bool(item.fields[5]) : undefined} value={item.fields[2] === "Float" ? item.fields[3] : item.fields[4]} onChange={(e) => update("Parameter", Number(item.fields[0]), item.fields[2] === "Float" ? 3 : item.fields[2] === "Int" ? 4 : 5, e.currentTarget.type === "checkbox" ? (e.currentTarget.checked ? "1" : "0") : e.currentTarget.value)} /></div>)}
				<button type="button" onClick={() => append(shader ? `Variable=Variable${variables.length + 1}|0|0,0,0,0` : `Parameter=${nextParameterId}|Parameter ${nextParameterId}|Float|0|0|0`)}>+ {shader ? "Variable" : "Parameter"}</button>
			</section>
			{!shader && <section className="graph-inspector"><h4>Animation Layers</h4>{layers.map((layer) => <details className="graph-subrecord" key={layer.fields[0]}><summary>{layer.fields[1]}</summary><Setting label="Name"><input value={layer.fields[1]} onChange={(e) => update("Layer", Number(layer.fields[0]), 1, clean(e.currentTarget.value))} /></Setting><Setting label="Graph"><input value={layer.fields[16] === "0" ? "" : layer.fields[16]} placeholder="project://..." onChange={(e) => update("Layer", Number(layer.fields[0]), 16, clean(e.currentTarget.value) || "0")} /></Setting><Setting label="Clip"><input value={layer.fields[3]} onChange={(e) => update("Layer", Number(layer.fields[0]), 3, clean(e.currentTarget.value))} /></Setting><Setting label="Mask"><select value={layer.fields[5]} onChange={(e) => update("Layer", Number(layer.fields[0]), 5, e.currentTarget.value)}><option value="0">Full body</option>{boneMasks.map((mask) => <option key={mask.fields[0]} value={mask.fields[0]}>{mask.fields[1]}</option>)}</select></Setting><Setting label="Blend"><select value={layer.fields[6]} onChange={(e) => update("Layer", Number(layer.fields[0]), 6, e.currentTarget.value)}><option value="0">Override</option><option value="1">Additive</option></select></Setting><Setting label="Weight"><input type="number" min="0" max="1" step="0.05" value={layer.fields[7]} onChange={(e) => update("Layer", Number(layer.fields[0]), 7, e.currentTarget.value)} /></Setting><Setting label="Speed"><input type="number" min="0" step="0.05" value={layer.fields[10]} onChange={(e) => update("Layer", Number(layer.fields[0]), 10, e.currentTarget.value)} /></Setting><Setting label="Fade in"><input type="number" min="0" step="0.01" value={layer.fields[11]} onChange={(e) => update("Layer", Number(layer.fields[0]), 11, e.currentTarget.value)} /></Setting><Setting label="Fade out"><input type="number" min="0" step="0.01" value={layer.fields[12]} onChange={(e) => update("Layer", Number(layer.fields[0]), 12, e.currentTarget.value)} /></Setting><Setting label="Enabled"><input type="checkbox" checked={bool(layer.fields[15])} onChange={(e) => update("Layer", Number(layer.fields[0]), 15, e.currentTarget.checked ? "1" : "0")} /></Setting><button className="danger" type="button" onClick={() => removeLines((key, f) => key === "Layer" && f[0] === layer.fields[0])}>Delete layer</button></details>)}<button type="button" onClick={() => append(`Layer=${nextLayerId}|Layer ${nextLayerId}||Idle|0|0|0|1|||1|0.08|0.12|1|1|1|0`)}>+ Layer</button></section>}
			{!shader && <section className="graph-inspector"><h4>Bone Masks</h4>{boneMasks.map((mask) => <div className="graph-record" key={mask.fields[0]}><input value={mask.fields[1]} onChange={(e) => update("BoneMask", Number(mask.fields[0]), 1, clean(e.currentTarget.value))} /><input type="number" min="0" max="1" step="0.05" value={mask.fields[2]} onChange={(e) => update("BoneMask", Number(mask.fields[0]), 2, e.currentTarget.value)} /><button type="button" className="danger" onClick={() => removeLines((key, f) => (key === "BoneMask" || key === "BoneMaskEntry") && f[0] === mask.fields[0])}>×</button></div>)}<button type="button" onClick={() => append(`BoneMask=${nextMaskId}|Mask ${nextMaskId}|0`)}>+ Bone mask</button></section>}
			<details><summary>Source</summary><textarea value={content} spellCheck={false} onChange={(e) => onChange(e.currentTarget.value)} /></details>
		</aside>
		<NodeGraph nodes={parsed.nodes} links={parsed.links} selectedId={selected} selectedLinkId={selectedLink} onSelect={(id) => { setSelected(id); if (id !== undefined) setSelectedLink(undefined); }} onSelectLink={setSelectedLink} onMove={move} onLink={addLink} onDelete={removeNode} palette={palette} onCreateNode={addNode} />
	</div>;
}
