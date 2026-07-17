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
      <button onClick={() => void window.plutoEditor.openProject()}>Project…</button>
      <button disabled={!editor?.projectPath} onClick={() => window.plutoEditor.saveProject()}>Save Project</button>
      <button onClick={() => void window.plutoEditor.newScene()}>New</button>
      <button onClick={() => void window.plutoEditor.openScene()}>Open</button>
      <button onClick={() => void window.plutoEditor.saveScene()}>Save</button>
    </nav>
    <div className="toolbar-divider" />
    <div className="toolbar-group">
      <button className="tool-button" disabled={!editor?.canUndo || running} onClick={() => window.plutoEditor.undo()} title="Undo">↶</button>
      <button className="tool-button" disabled={!editor?.canRedo || running} onClick={() => window.plutoEditor.redo()} title="Redo">↷</button>
    </div>
    <button className={`camera-button ${showEditorCamera ? 'active' : ''}`} disabled={!editor} onClick={onToggleCamera}>Camera</button>
    <button className="layout-button" title="Restore the default dock layout" onClick={onResetLayout}>Reset Layout</button>
    <button className={`play-button ${running ? 'running' : ''}`} disabled={!editor} onClick={() => window.plutoEditor.setRuntime(!running)}>{running ? '■ Stop' : '▶ Play'}</button>
  </header>;
}
