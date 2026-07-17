import React from 'react';

export function Toolbar({ editor, running, showEditorCamera, onToggleCamera, onResetLayout }: {
  editor?: EditorState;
  running: boolean;
  showEditorCamera: boolean;
  onToggleCamera(): void;
  onResetLayout(): void;
}): React.JSX.Element {
  return <header className="toolbar">
    <div className="brand"><span className="brand-mark">P</span><span>PlutoGE</span></div>
    <nav className="menu-actions">
      <button onClick={() => void window.plutoEditor.newProject()}>New Project…</button>
      <button onClick={() => void window.plutoEditor.openProject()}>Open Project…</button>
      <button disabled={!editor?.projectPath} onClick={() => window.plutoEditor.saveProject()}>Save Project</button>
      <button onClick={() => void window.plutoEditor.newScene()}>New Scene</button>
      <button onClick={() => void window.plutoEditor.openScene()}>Open Scene</button>
      <button onClick={() => void window.plutoEditor.saveScene()}>Save Scene</button>
    </nav>
    <div className="toolbar-divider" />
    <div className="toolbar-group">
      <button className="tool-button" disabled={!editor?.canUndo || running} onClick={() => window.plutoEditor.undo()} title="Undo">↶</button>
      <button className="tool-button" disabled={!editor?.canRedo || running} onClick={() => window.plutoEditor.redo()} title="Redo">↷</button>
    </div>
    <div className="gizmo-toolbar" aria-label="Transform gizmo" title="Hold Ctrl while dragging to snap">
      <button className={editor?.gizmoOperation === 'translate' ? 'active' : ''} disabled={!editor || running} title="Move tool (W)" onClick={() => window.plutoEditor.setGizmoOperation('translate')}>W&nbsp; Move</button>
      <button className={editor?.gizmoOperation === 'rotate' ? 'active' : ''} disabled={!editor || running} title="Rotate tool (E)" onClick={() => window.plutoEditor.setGizmoOperation('rotate')}>E&nbsp; Rotate</button>
      <button className={editor?.gizmoOperation === 'scale' ? 'active' : ''} disabled={!editor || running} title="Scale tool (R)" onClick={() => window.plutoEditor.setGizmoOperation('scale')}>R&nbsp; Scale</button>
      <button className="gizmo-space" disabled={!editor || running} title="Toggle local/world space" onClick={() => window.plutoEditor.setGizmoSpace(editor?.gizmoSpace === 'world' ? 'local' : 'world')}>{editor?.gizmoSpace === 'world' ? 'World' : 'Local'}</button>
    </div>
    <button className={`camera-button ${showEditorCamera ? 'active' : ''}`} disabled={!editor} onClick={onToggleCamera}>Camera</button>
    <button className="layout-button" title="Restore the default dock layout" onClick={onResetLayout}>Reset Layout</button>
    <button className={`play-button ${running ? 'running' : ''}`} disabled={!editor} onClick={() => window.plutoEditor.setRuntime(!running)}>{running ? '■ Stop' : '▶ Play'}</button>
  </header>;
}
