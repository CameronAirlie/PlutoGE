import React from "react";
import { useCallback, useEffect, useMemo } from "react";
import {
	Background,
	BackgroundVariant,
	Controls,
	type Edge,
	Handle,
	MiniMap,
	type Node,
	type NodeProps,
	type OnNodesChange,
	Position,
	ReactFlow,
	type ReactFlowInstance,
	useNodesState,
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import { DropdownMenu } from "radix-ui";

export interface GraphNode {
	id: number;
	title: string;
	x: number;
	y: number;
	inputs?: string[];
	outputs?: string[];
	accent?: string;
	subtitle?: string;
}

export interface GraphLink {
	id: number;
	from: number;
	to: number;
	fromPin?: string;
	toPin?: string;
}

export interface GraphPaletteItem { kind: string; label: string; category?: string; connectable?: boolean; }

interface GraphNodeData extends Record<string, unknown> {
	model: GraphNode;
}

type FlowNode = Node<GraphNodeData, "pluto">;

interface Props {
	nodes: GraphNode[];
	links: GraphLink[];
	selectedId?: number;
	selectedLinkId?: number;
	onSelect(id?: number): void;
	onSelectLink?(id?: number): void;
	onMove(id: number, x: number, y: number): void;
	onLink?(from: number, fromPin: string, to: number, toPin: string): void;
	onDelete?(id: number): void;
	palette?: GraphPaletteItem[];
	onCreateNode?(kind: string, x: number, y: number, connectFrom?: { nodeId: number; pin: string }): void;
}

function PlutoNode({ data, selected }: NodeProps<FlowNode>): React.JSX.Element {
	const node = data.model;
	return (
		<div
			className={`graph-node ${selected ? "selected" : ""}`}
			style={{ borderColor: node.accent }}
		>
			<div className="graph-node-title" style={{ background: node.accent }}>
				<span>{node.title}</span><small>{node.subtitle}</small>
			</div>
			<div className="graph-node-pins">
				<div>{(node.inputs ?? []).map((pin) => (
					<div className="node-pin input" key={pin}>
						<Handle id={pin} type="target" position={Position.Left} style={{ top: "50%" }} />
						{pin}
					</div>
				))}</div>
				<div>{(node.outputs ?? []).map((pin) => (
					<div className="node-pin output" key={pin}>
						{pin}
						<Handle id={pin} type="source" position={Position.Right} style={{ top: "50%" }} />
					</div>
				))}</div>
			</div>
		</div>
	);
}

const nodeTypes = { pluto: PlutoNode };

export function NodeGraph({ nodes, links, selectedId, selectedLinkId, onSelect, onSelectLink, onMove, onLink, onDelete, palette = [], onCreateNode }: Props): React.JSX.Element {
	const [instance, setInstance] = React.useState<ReactFlowInstance<FlowNode, Edge>>();
	const [createMenu, setCreateMenu] = React.useState<{ clientX: number; clientY: number; x: number; y: number; from?: { nodeId: number; pin: string } }>();
	const incomingNodes = useMemo<FlowNode[]>(() => nodes.map((node) => ({
		id: String(node.id),
		type: "pluto",
		position: { x: node.x, y: node.y },
		selected: node.id === selectedId,
		data: { model: node },
	})), [nodes, selectedId]);
	const [flowNodes, setFlowNodes, applyNodeChanges] = useNodesState<FlowNode>(incomingNodes);
	useEffect(() => setFlowNodes(incomingNodes), [incomingNodes, setFlowNodes]);
	const flowEdges = useMemo<Edge[]>(() => links.map((link) => ({
		id: String(link.id),
		source: String(link.from),
		target: String(link.to),
		sourceHandle: link.fromPin,
		targetHandle: link.toPin,
		type: "smoothstep",
		selected: link.id === selectedLinkId,
		animated: false,
	})), [links]);
	const onNodesChange: OnNodesChange<FlowNode> = useCallback((changes) => {
		applyNodeChanges(changes);
		for (const change of changes) {
			// React Flow owns the live drag position. Persist only the final
			// coordinate so serialization does not rebuild nodes while its
			// ResizeObserver is measuring them.
			if (change.type === "position" && change.position && change.dragging === false)
				onMove(Number(change.id), change.position.x, change.position.y);
			if (change.type === "select" && change.selected) onSelect(Number(change.id));
		}
	}, [applyNodeChanges, onMove, onSelect]);
	return (
		<div className="node-graph">
			<ReactFlow<FlowNode, Edge>
				onInit={setInstance}
				nodes={flowNodes}
				edges={flowEdges}
				nodeTypes={nodeTypes}
				onNodesChange={onNodesChange}
				onPaneClick={() => { onSelect(undefined); setCreateMenu(undefined); }}
				onEdgeClick={(_, edge) => { onSelect(undefined); onSelectLink?.(Number(edge.id)); }}
				onConnect={(connection) => {
					if (connection.source && connection.target)
						onLink?.(Number(connection.source), connection.sourceHandle ?? "Out", Number(connection.target), connection.targetHandle ?? "In");
				}}
				onConnectEnd={(event, state) => {
					if (state.isValid || !state.fromNode || !state.fromHandle || !state.pointer || !instance || !palette.length) return;
					const position = instance.screenToFlowPosition(state.pointer);
					setCreateMenu({ clientX: state.pointer.x, clientY: state.pointer.y, x: position.x, y: position.y, from: { nodeId: Number(state.fromNode.id), pin: state.fromHandle.id ?? "Out" } });
				}}
				onDragOver={(event) => { if (event.dataTransfer.types.includes("application/x-plutoge-node")) { event.preventDefault(); event.dataTransfer.dropEffect = "copy"; } }}
				onDrop={(event) => { const kind = event.dataTransfer.getData("application/x-plutoge-node"); if (!kind || !instance) return; event.preventDefault(); const position = instance.screenToFlowPosition({ x: event.clientX, y: event.clientY }); onCreateNode?.(kind, position.x, position.y); }}
				onNodesDelete={(deleted) => deleted.forEach((node) => onDelete?.(Number(node.id)))}
				fitView
				minZoom={0.25}
				maxZoom={2.5}
				deleteKeyCode={["Backspace", "Delete"]}
				connectionRadius={24}
			>
				<Background variant={BackgroundVariant.Dots} gap={20} size={1.2} />
				<MiniMap pannable zoomable nodeColor="#556a91" />
				<Controls showInteractive={false} />
			</ReactFlow>
			<div className="node-graph-help">Drag between ports to connect · Wheel to zoom · Drag background to pan</div>
			{createMenu && <DropdownMenu.Root open modal={false} onOpenChange={(open) => { if (!open) setCreateMenu(undefined); }}><DropdownMenu.Trigger aria-hidden tabIndex={-1} style={{ position: "fixed", left: createMenu.clientX, top: createMenu.clientY, width: 1, height: 1, opacity: 0 }} /><DropdownMenu.Portal><DropdownMenu.Content className="graph-create-menu" side="bottom" align="start" sideOffset={6} collisionPadding={8}><DropdownMenu.Label className="graph-create-label">Create connected node</DropdownMenu.Label>{palette.filter((item) => item.connectable !== false).map((item) => <DropdownMenu.Item className="graph-create-item" key={item.kind} onSelect={() => { onCreateNode?.(item.kind, createMenu.x, createMenu.y, createMenu.from); setCreateMenu(undefined); }}><span>{item.label}</span><small>{item.category}</small></DropdownMenu.Item>)}</DropdownMenu.Content></DropdownMenu.Portal></DropdownMenu.Root>}
		</div>
	);
}
