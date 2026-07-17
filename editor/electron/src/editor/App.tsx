import React, { useEffect, useMemo, useState } from 'react';
import { DockWorkspace, useDockLayout } from './components/DockWorkspace';
import { StatusBar } from './components/StatusBar';
import { Toolbar } from './components/Toolbar';

export function App(): React.JSX.Element {
  const [host, setHost] = useState<HostState>({ status: 'starting' });
  const [editor, setEditor] = useState<EditorState>();
  const [showEditorCamera, setShowEditorCamera] = useState(false);
  const dock = useDockLayout();
  const selectedEntity = useMemo(() => editor?.entities.find((entity) => entity.id === editor.selectedEntityId), [editor]);

  useEffect(() => {
    const removeHostListener = window.plutoEditor.onHostState(setHost);
    const removeEditorListener = window.plutoEditor.onEditorState(setEditor);
    void window.plutoEditor.getHostState().then(setHost);
    void window.plutoEditor.getEditorState().then((state) => { if (state) setEditor(state); });
    return () => { removeHostListener(); removeEditorListener(); };
  }, []);

  useEffect(() => {
    const shortcuts = (event: KeyboardEvent): void => {
      const target = event.target as HTMLElement | null;
      const editingText = target?.matches('input, textarea, select, [contenteditable="true"]') ?? false;
      if (!(event.ctrlKey || event.metaKey) || !editor) {
        if (!editingText && event.key === 'Delete' && editor?.selectedEntityId && !editor.running) window.plutoEditor.deleteEntity(editor.selectedEntityId);
        return;
      }
      if (event.key.toLowerCase() === 's') { event.preventDefault(); void window.plutoEditor.saveScene(event.shiftKey); }
      if (event.key.toLowerCase() === 'z') { event.preventDefault(); event.shiftKey ? window.plutoEditor.redo() : window.plutoEditor.undo(); }
      if (event.key.toLowerCase() === 'y') { event.preventDefault(); window.plutoEditor.redo(); }
    };
    window.addEventListener('keydown', shortcuts);
    return () => window.removeEventListener('keydown', shortcuts);
  }, [editor]);

  const running = editor?.running ?? false;
  return <div className="editor-shell">
    <Toolbar editor={editor} running={running} showEditorCamera={showEditorCamera} onToggleCamera={() => setShowEditorCamera((visible) => !visible)} onResetLayout={dock.reset} />
    <DockWorkspace layout={dock.layout} sizes={dock.sizes} onLayoutChange={dock.setLayout} onSizesChange={dock.setSizes} host={host} editor={editor} selectedEntity={selectedEntity} showEditorCamera={showEditorCamera} />
    <StatusBar host={host} editor={editor} />
  </div>;
}
