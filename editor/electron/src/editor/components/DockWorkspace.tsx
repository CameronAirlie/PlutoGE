import React, { useEffect, useMemo, useRef, useState } from 'react';
import { panelTitles, type PanelId } from './PanelFrame';
import { ContentBrowserPanel } from '../panels/ContentBrowserPanel';
import { GameViewportPanel } from '../panels/GameViewportPanel';
import { HierarchyPanel } from '../panels/HierarchyPanel';
import { InspectorPanel } from '../panels/InspectorPanel';
import { ViewportPanel } from '../panels/ViewportPanel';

export type DockTabs = { type: 'tabs'; id: string; panels: PanelId[]; active: PanelId };
export type DockSplit = { type: 'split'; id: string; direction: 'horizontal' | 'vertical'; ratio: number; first: DockNode; second: DockNode };
export type DockNode = DockTabs | DockSplit;
export type DockDropPosition = 'center' | 'left' | 'right' | 'top' | 'bottom';

const panelIds: PanelId[] = ['hierarchy', 'viewport', 'game', 'inspector', 'content'];
const setPanelDragOccluded = (occluded: boolean): void => window.plutoEditor.setViewportOccluded('panel-drag', occluded);
let nextDockId = 1;
const createDockId = (): string => `dock-${Date.now().toString(36)}-${nextDockId++}`;
const tabs = (id: string, panels: PanelId[], active = panels[0]): DockTabs => ({ type: 'tabs', id, panels, active });

export const defaultDockLayout: DockNode = {
  type: 'split', id: 'root', direction: 'horizontal', ratio: 0.18,
  first: tabs('hierarchy-tabs', ['hierarchy']),
  second: {
    type: 'split', id: 'main-right', direction: 'horizontal', ratio: 0.72,
    first: {
      type: 'split', id: 'main-center', direction: 'vertical', ratio: 0.68,
      first: tabs('view-tabs', ['viewport', 'game'], 'viewport'),
      second: tabs('content-tabs', ['content']),
    },
    second: tabs('inspector-tabs', ['inspector']),
  },
};

const cloneDefaultLayout = (): DockNode => JSON.parse(JSON.stringify(defaultDockLayout)) as DockNode;
const isPanelId = (value: unknown): value is PanelId => typeof value === 'string' && panelIds.includes(value as PanelId);

const parseDockNode = (value: unknown, seenPanels: Set<PanelId>): DockNode | undefined => {
  if (!value || typeof value !== 'object') return undefined;
  const node = value as Record<string, unknown>;
  if (node.type === 'tabs' && typeof node.id === 'string' && Array.isArray(node.panels)) {
    const panels = node.panels.filter((panel): panel is PanelId => isPanelId(panel) && !seenPanels.has(panel));
    panels.forEach((panel) => seenPanels.add(panel));
    if (!panels.length) return undefined;
    return tabs(node.id, panels, isPanelId(node.active) && panels.includes(node.active) ? node.active : panels[0]);
  }
  if (node.type === 'split' && typeof node.id === 'string' && (node.direction === 'horizontal' || node.direction === 'vertical')) {
    const first = parseDockNode(node.first, seenPanels);
    const second = parseDockNode(node.second, seenPanels);
    if (!first) return second;
    if (!second) return first;
    const ratio = Number(node.ratio);
    return { type: 'split', id: node.id, direction: node.direction, ratio: Number.isFinite(ratio) ? Math.min(.85, Math.max(.15, ratio)) : .5, first, second };
  }
  return undefined;
};

const loadLayout = (): DockNode => {
  try {
    const parsed = parseDockNode(JSON.parse(localStorage.getItem('plutoge:dock-tree:v2') ?? ''), new Set());
    if (parsed) return parsed;
  } catch { /* Use the default layout. */ }
  return cloneDefaultLayout();
};

const collectPanels = (node: DockNode, result = new Set<PanelId>()): Set<PanelId> => {
  if (node.type === 'tabs') node.panels.forEach((panel) => result.add(panel));
  else { collectPanels(node.first, result); collectPanels(node.second, result); }
  return result;
};

const removePanel = (node: DockNode, panel: PanelId): DockNode | undefined => {
  if (node.type === 'tabs') {
    if (!node.panels.includes(panel)) return node;
    const panels = node.panels.filter((candidate) => candidate !== panel);
    return panels.length ? { ...node, panels, active: node.active === panel ? panels[0] : node.active } : undefined;
  }
  const first = removePanel(node.first, panel);
  const second = removePanel(node.second, panel);
  if (!first) return second;
  if (!second) return first;
  return { ...node, first, second };
};

const updateTabs = (node: DockNode, id: string, update: (tabsNode: DockTabs) => DockNode): DockNode => {
  if (node.type === 'tabs') return node.id === id ? update(node) : node;
  return { ...node, first: updateTabs(node.first, id, update), second: updateTabs(node.second, id, update) };
};

const updateSplitRatio = (node: DockNode, id: string, ratio: number): DockNode => {
  if (node.type === 'tabs') return node;
  if (node.id === id) return { ...node, ratio: Math.min(.85, Math.max(.15, ratio)) };
  return { ...node, first: updateSplitRatio(node.first, id, ratio), second: updateSplitRatio(node.second, id, ratio) };
};

const findTabsWithPanel = (node: DockNode, panel: PanelId): string | undefined => {
  if (node.type === 'tabs') return node.panels.includes(panel) ? node.id : undefined;
  return findTabsWithPanel(node.first, panel) ?? findTabsWithPanel(node.second, panel);
};

const firstTabsId = (node: DockNode): string => node.type === 'tabs' ? node.id : firstTabsId(node.first);

const insertPanel = (node: DockNode, targetId: string, panel: PanelId, position: DockDropPosition): DockNode => updateTabs(node, targetId, (target) => {
  if (position === 'center') return { ...target, panels: [...target.panels, panel], active: panel };
  const incoming = tabs(createDockId(), [panel]);
  const before = position === 'left' || position === 'top';
  return {
    type: 'split', id: createDockId(),
    direction: position === 'left' || position === 'right' ? 'horizontal' : 'vertical',
    ratio: .5,
    first: before ? incoming : target,
    second: before ? target : incoming,
  };
});

const preferredTarget = (layout: DockNode, panel: PanelId): string => {
  const companion: Partial<Record<PanelId, PanelId>> = { game: 'viewport', viewport: 'game', hierarchy: 'inspector', inspector: 'hierarchy', content: 'viewport' };
  return (companion[panel] && findTabsWithPanel(layout, companion[panel]!)) || firstTabsId(layout);
};

export function useDockLayout(): {
  layout: DockNode;
  visiblePanels: Set<PanelId>;
  setLayout(layout: DockNode): void;
  togglePanel(panel: PanelId): void;
  reset(): void;
} {
  const [layout, setLayoutState] = useState(loadLayout);
  const setLayout = (next: DockNode): void => setLayoutState(next);
  useEffect(() => {
    const timeout = window.setTimeout(() => localStorage.setItem('plutoge:dock-tree:v2', JSON.stringify(layout)), 150);
    return () => window.clearTimeout(timeout);
  }, [layout]);
  const togglePanel = (panel: PanelId): void => {
    if (collectPanels(layout).has(panel)) {
      const next = removePanel(layout, panel);
      if (next) setLayout(next);
      return;
    }
    setLayout(insertPanel(layout, preferredTarget(layout, panel), panel, 'center'));
  };
  const reset = (): void => setLayout(cloneDefaultLayout());
  return { layout, visiblePanels: useMemo(() => collectPanels(layout), [layout]), setLayout, togglePanel, reset };
}

function SplitHandle({ node, onRatioChange }: { node: DockSplit; onRatioChange(ratio: number): void }): React.JSX.Element {
  const drag = useRef<{ pointerId: number; start: number; ratio: number; size: number } | undefined>(undefined);
  const horizontal = node.direction === 'horizontal';
  return <div
    className={`dock-splitter ${horizontal ? 'vertical' : 'horizontal'}`}
    role="separator"
    aria-orientation={horizontal ? 'vertical' : 'horizontal'}
    tabIndex={0}
    onPointerDown={(event) => {
      const parent = event.currentTarget.parentElement?.getBoundingClientRect();
      if (!parent) return;
      event.currentTarget.setPointerCapture(event.pointerId);
      drag.current = { pointerId: event.pointerId, start: horizontal ? event.clientX : event.clientY, ratio: node.ratio, size: horizontal ? parent.width : parent.height };
    }}
    onPointerMove={(event) => {
      if (!drag.current || drag.current.pointerId !== event.pointerId) return;
      const coordinate = horizontal ? event.clientX : event.clientY;
      onRatioChange(drag.current.ratio + (coordinate - drag.current.start) / Math.max(1, drag.current.size));
    }}
    onPointerUp={(event) => {
      drag.current = undefined;
      event.currentTarget.releasePointerCapture(event.pointerId);
    }}
    onKeyDown={(event) => {
      const decrease = horizontal ? event.key === 'ArrowLeft' : event.key === 'ArrowUp';
      const increase = horizontal ? event.key === 'ArrowRight' : event.key === 'ArrowDown';
      if (!decrease && !increase) return;
      event.preventDefault();
      onRatioChange(node.ratio + (increase ? .03 : -.03));
    }}
  />;
}

function DockTabsView({ node, panels, onActivate, onClose, onDropPanel }: {
  node: DockTabs;
  panels: Record<PanelId, React.JSX.Element>;
  onActivate(panel: PanelId): void;
  onClose(panel: PanelId): void;
  onDropPanel(source: PanelId, targetId: string, position: DockDropPosition): void;
}): React.JSX.Element {
  const [dropPosition, setDropPosition] = useState<DockDropPosition>();
  const positionForEvent = (event: React.DragEvent): DockDropPosition => {
    if ((event.target as Element).closest('.dock-tab-strip')) return 'center';
    const bounds = event.currentTarget.getBoundingClientRect();
    const x = (event.clientX - bounds.left) / Math.max(1, bounds.width);
    const y = (event.clientY - bounds.top) / Math.max(1, bounds.height);
    if (x < .24) return 'left';
    if (x > .76) return 'right';
    if (y < .24) return 'top';
    if (y > .76) return 'bottom';
    return 'center';
  };
  return <section
    className="dock-tabs"
    onDragOver={(event) => {
      if (!event.dataTransfer.types.includes('application/x-plutoge-panel')) return;
      event.preventDefault();
      event.dataTransfer.dropEffect = 'move';
      setDropPosition(positionForEvent(event));
    }}
    onDragLeave={(event) => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setDropPosition(undefined); }}
    onDrop={(event) => {
      const source = event.dataTransfer.getData('application/x-plutoge-panel') as PanelId;
      if (isPanelId(source) && dropPosition) {
        event.preventDefault();
        // Splitting can unmount the dragged tab before its dragend event fires.
        setPanelDragOccluded(false);
        onDropPanel(source, node.id, dropPosition);
      }
      setDropPosition(undefined);
    }}
  >
    <div className="dock-tab-strip" role="tablist">
      {node.panels.map((panel) => <div className={`dock-tab ${node.active === panel ? 'active' : ''}`} key={panel}>
        <button
          className="dock-tab-title"
          role="tab"
          aria-selected={node.active === panel}
          draggable
          onDragStart={(event) => {
            event.dataTransfer.effectAllowed = 'move';
            event.dataTransfer.setData('application/x-plutoge-panel', panel);
            setPanelDragOccluded(true);
          }}
          onDragEnd={() => setPanelDragOccluded(false)}
          onClick={() => onActivate(panel)}
        >{panelTitles[panel]}</button>
        <button className="dock-tab-close" title={`Close ${panelTitles[panel]}`} onClick={() => onClose(panel)}>×</button>
      </div>)}
    </div>
    <div className="dock-tab-content">{panels[node.active]}</div>
    {dropPosition && <div className={`dock-drop-preview ${dropPosition}`}><span>{dropPosition === 'center' ? 'Add as tab' : `Split ${dropPosition}`}</span></div>}
  </section>;
}

function DockNodeView({ node, panels, onLayoutChange, onClose, onDropPanel }: {
  node: DockNode;
  panels: Record<PanelId, React.JSX.Element>;
  onLayoutChange(node: DockNode): void;
  onClose(panel: PanelId): void;
  onDropPanel(source: PanelId, targetId: string, position: DockDropPosition): void;
}): React.JSX.Element {
  if (node.type === 'tabs') return <DockTabsView
    node={node}
    panels={panels}
    onActivate={(panel) => onLayoutChange({ ...node, active: panel })}
    onClose={onClose}
    onDropPanel={onDropPanel}
  />;
  return <div className={`dock-split ${node.direction}`}>
    <div className="dock-split-child" style={{ flexBasis: `${node.ratio * 100}%` }}><DockNodeView node={node.first} panels={panels} onLayoutChange={(first) => onLayoutChange({ ...node, first })} onClose={onClose} onDropPanel={onDropPanel} /></div>
    <SplitHandle node={node} onRatioChange={(ratio) => onLayoutChange(updateSplitRatio(node, node.id, ratio))} />
    <div className="dock-split-child" style={{ flexBasis: `${(1 - node.ratio) * 100}%` }}><DockNodeView node={node.second} panels={panels} onLayoutChange={(second) => onLayoutChange({ ...node, second })} onClose={onClose} onDropPanel={onDropPanel} /></div>
  </div>;
}

export function DockWorkspace({ layout, onLayoutChange, onTogglePanel, host, gameHost, editor, selectedEntity, showEditorCamera }: {
  layout: DockNode;
  onLayoutChange(layout: DockNode): void;
  onTogglePanel(panel: PanelId): void;
  host: HostState;
  gameHost: HostState;
  editor?: EditorState;
  selectedEntity?: EditorEntity;
  showEditorCamera: boolean;
}): React.JSX.Element {
  useEffect(() => {
    // Also recover from cancelled or external drops whose source tab disappears.
    const finishPanelDrag = (): void => setPanelDragOccluded(false);
    window.addEventListener('dragend', finishPanelDrag);
    window.addEventListener('drop', finishPanelDrag);
    return () => {
      window.removeEventListener('dragend', finishPanelDrag);
      window.removeEventListener('drop', finishPanelDrag);
      finishPanelDrag();
    };
  }, []);

  const panels = useMemo<Record<PanelId, React.JSX.Element>>(() => ({
    hierarchy: <HierarchyPanel editor={editor} selectedEntity={selectedEntity} />,
    viewport: <ViewportPanel host={host} />,
    game: <GameViewportPanel host={gameHost} editor={editor} />,
    inspector: <InspectorPanel editor={editor} selectedEntity={selectedEntity} showEditorCamera={showEditorCamera} />,
    content: <ContentBrowserPanel editor={editor} />,
  }), [editor, gameHost, host, selectedEntity, showEditorCamera]);

  const movePanel = (source: PanelId, targetId: string, position: DockDropPosition): void => {
    const sourceTabsId = findTabsWithPanel(layout, source);
    if (!sourceTabsId || (sourceTabsId === targetId && position === 'center')) return;
    const withoutSource = removePanel(layout, source);
    if (!withoutSource) return;
    const targetStillExists = (() => {
      const visit = (node: DockNode): boolean => node.type === 'tabs' ? node.id === targetId : visit(node.first) || visit(node.second);
      return visit(withoutSource);
    })();
    if (!targetStillExists) return;
    onLayoutChange(insertPanel(withoutSource, targetId, source, position));
  };

  return <main className="dock-workspace">
    <DockNodeView node={layout} panels={panels} onLayoutChange={onLayoutChange} onClose={onTogglePanel} onDropPanel={movePanel} />
  </main>;
}
