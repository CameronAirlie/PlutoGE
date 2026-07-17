import React, { useMemo, useState } from 'react';
import type { PanelId } from './PanelFrame';
import { ContentBrowserPanel } from '../panels/ContentBrowserPanel';
import { HierarchyPanel } from '../panels/HierarchyPanel';
import { InspectorPanel } from '../panels/InspectorPanel';
import { ViewportPanel } from '../panels/ViewportPanel';

type DockSlot = 'left' | 'center' | 'right' | 'bottom';
export type DockLayout = Record<DockSlot, PanelId>;

export const defaultDockLayout: DockLayout = { left: 'hierarchy', center: 'viewport', right: 'inspector', bottom: 'content' };
const slots: DockSlot[] = ['left', 'center', 'right', 'bottom'];

const loadLayout = (): DockLayout => {
  try {
    const value = JSON.parse(localStorage.getItem('plutoge:dock-layout') ?? '') as Partial<DockLayout>;
    const panels = slots.map((slot) => value[slot]);
    if (new Set(panels).size === 4 && panels.every((panel) => ['hierarchy', 'viewport', 'inspector', 'content'].includes(panel ?? ''))) return value as DockLayout;
  } catch { /* Use the default layout. */ }
  return defaultDockLayout;
};

export function useDockLayout(): { layout: DockLayout; setLayout(layout: DockLayout): void; reset(): void } {
  const [layout, setLayoutState] = useState(loadLayout);
  const setLayout = (next: DockLayout): void => { setLayoutState(next); localStorage.setItem('plutoge:dock-layout', JSON.stringify(next)); };
  const reset = (): void => setLayout(defaultDockLayout);
  return { layout, setLayout, reset };
}

export function DockWorkspace({ layout, onLayoutChange, host, editor, selectedEntity, showEditorCamera }: {
  layout: DockLayout;
  onLayoutChange(layout: DockLayout): void;
  host: HostState;
  editor?: EditorState;
  selectedEntity?: EditorEntity;
  showEditorCamera: boolean;
}): React.JSX.Element {
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

  return <main className="dock-workspace">
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
  </main>;
}
