import React, { useMemo, useRef, useState } from 'react';
import type { PanelId } from './PanelFrame';
import { ContentBrowserPanel } from '../panels/ContentBrowserPanel';
import { HierarchyPanel } from '../panels/HierarchyPanel';
import { InspectorPanel } from '../panels/InspectorPanel';
import { ViewportPanel } from '../panels/ViewportPanel';

type DockSlot = 'left' | 'center' | 'right' | 'bottom';
export type DockLayout = Record<DockSlot, PanelId>;
export type DockSizes = { left: number; right: number; bottom: number };

export const defaultDockLayout: DockLayout = { left: 'hierarchy', center: 'viewport', right: 'inspector', bottom: 'content' };
export const defaultDockSizes: DockSizes = { left: 230, right: 300, bottom: 260 };
const slots: DockSlot[] = ['left', 'center', 'right', 'bottom'];

const loadLayout = (): DockLayout => {
  try {
    const value = JSON.parse(localStorage.getItem('plutoge:dock-layout') ?? '') as Partial<DockLayout>;
    const panels = slots.map((slot) => value[slot]);
    if (new Set(panels).size === 4 && panels.every((panel) => ['hierarchy', 'viewport', 'inspector', 'content'].includes(panel ?? ''))) return value as DockLayout;
  } catch { /* Use the default layout. */ }
  return defaultDockLayout;
};

const loadSizes = (): DockSizes => {
  try {
    const value = JSON.parse(localStorage.getItem('plutoge:dock-sizes') ?? '') as Partial<DockSizes>;
    if (Number.isFinite(value.left) && Number.isFinite(value.right) && Number.isFinite(value.bottom)) {
      return { left: Number(value.left), right: Number(value.right), bottom: Number(value.bottom) };
    }
  } catch { /* Use the default sizes. */ }
  return defaultDockSizes;
};

export function useDockLayout(): { layout: DockLayout; sizes: DockSizes; setLayout(layout: DockLayout): void; setSizes(sizes: DockSizes): void; reset(): void } {
  const [layout, setLayoutState] = useState(loadLayout);
  const [sizes, setSizesState] = useState(loadSizes);
  const setLayout = (next: DockLayout): void => { setLayoutState(next); localStorage.setItem('plutoge:dock-layout', JSON.stringify(next)); };
  const setSizes = (next: DockSizes): void => { setSizesState(next); localStorage.setItem('plutoge:dock-sizes', JSON.stringify(next)); };
  const reset = (): void => { setLayout(defaultDockLayout); setSizes(defaultDockSizes); };
  return { layout, sizes, setLayout, setSizes, reset };
}

function ResizeHandle({ orientation, value, direction, onChange, className }: {
  orientation: 'vertical' | 'horizontal';
  value: number;
  direction: 1 | -1;
  onChange(value: number): void;
  className: string;
}): React.JSX.Element {
  const drag = useRef<{ pointerId: number; coordinate: number; value: number } | undefined>(undefined);
  const coordinate = (event: React.PointerEvent): number => orientation === 'vertical' ? event.clientX : event.clientY;
  return <div
    className={`dock-resizer ${className}`}
    role="separator"
    aria-orientation={orientation}
    aria-valuenow={Math.round(value)}
    tabIndex={0}
    onPointerDown={(event) => {
      event.preventDefault();
      event.currentTarget.setPointerCapture(event.pointerId);
      drag.current = { pointerId: event.pointerId, coordinate: coordinate(event), value };
    }}
    onPointerMove={(event) => {
      if (!drag.current || drag.current.pointerId !== event.pointerId) return;
      onChange(drag.current.value + ((coordinate(event) - drag.current.coordinate) * direction));
    }}
    onPointerUp={(event) => {
      if (drag.current?.pointerId === event.pointerId) drag.current = undefined;
      event.currentTarget.releasePointerCapture(event.pointerId);
    }}
    onKeyDown={(event) => {
      const decrease = orientation === 'vertical' ? event.key === 'ArrowLeft' : event.key === 'ArrowUp';
      const increase = orientation === 'vertical' ? event.key === 'ArrowRight' : event.key === 'ArrowDown';
      if (!decrease && !increase) return;
      event.preventDefault();
      onChange(value + (increase ? 10 : -10) * direction);
    }}
  />;
}

export function DockWorkspace({ layout, sizes, onLayoutChange, onSizesChange, host, editor, selectedEntity, showEditorCamera }: {
  layout: DockLayout;
  sizes: DockSizes;
  onLayoutChange(layout: DockLayout): void;
  onSizesChange(sizes: DockSizes): void;
  host: HostState;
  editor?: EditorState;
  selectedEntity?: EditorEntity;
  showEditorCamera: boolean;
}): React.JSX.Element {
  const workspace = useRef<HTMLElement>(null);
  const panels = useMemo<Record<PanelId, React.JSX.Element>>(() => ({
    hierarchy: <HierarchyPanel editor={editor} selectedEntity={selectedEntity} />,
    viewport: <ViewportPanel host={host} />,
    inspector: <InspectorPanel editor={editor} selectedEntity={selectedEntity} showEditorCamera={showEditorCamera} />,
    content: <ContentBrowserPanel editor={editor} />,
  }), [editor, host, selectedEntity, showEditorCamera]);

  const movePanel = (source: PanelId, targetSlot: DockSlot): void => {
    const sourceSlot = slots.find((slot) => layout[slot] === source);
    if (!sourceSlot || sourceSlot === targetSlot) return;
    onLayoutChange({ ...layout, [sourceSlot]: layout[targetSlot], [targetSlot]: source });
  };

  const resize = (field: keyof DockSizes, requested: number): void => {
    const bounds = workspace.current?.getBoundingClientRect();
    if (!bounds) return;
    const maximum = field === 'bottom'
      ? Math.max(130, bounds.height - 220)
      : Math.max(160, bounds.width - (field === 'left' ? sizes.right : sizes.left) - 360);
    onSizesChange({ ...sizes, [field]: Math.round(Math.min(maximum, Math.max(field === 'bottom' ? 130 : 160, requested))) });
  };

  return <main ref={workspace} className="dock-workspace" style={{
    '--dock-left': `${sizes.left}px`,
    '--dock-right': `${sizes.right}px`,
    '--dock-bottom': `${sizes.bottom}px`,
  } as React.CSSProperties}>
    {slots.map((slot) => <div
      className={`dock-slot dock-${slot}`}
      key={slot}
      onDragOver={(event) => {
        if (event.dataTransfer.types.includes('application/x-plutoge-panel')) {
          event.preventDefault();
          event.dataTransfer.dropEffect = 'move';
          event.currentTarget.classList.add('drop-target');
        }
      }}
      onDragLeave={(event) => event.currentTarget.classList.remove('drop-target')}
      onDrop={(event) => {
        event.currentTarget.classList.remove('drop-target');
        const source = event.dataTransfer.getData('application/x-plutoge-panel') as PanelId;
        if (source) { event.preventDefault(); movePanel(source, slot); }
      }}
    >{panels[layout[slot]]}</div>)}
    <ResizeHandle orientation="vertical" className="dock-resizer-left" value={sizes.left} direction={1} onChange={(value) => resize('left', value)} />
    <ResizeHandle orientation="vertical" className="dock-resizer-right" value={sizes.right} direction={-1} onChange={(value) => resize('right', value)} />
    <ResizeHandle orientation="horizontal" className="dock-resizer-bottom" value={sizes.bottom} direction={-1} onChange={(value) => resize('bottom', value)} />
  </main>;
}
